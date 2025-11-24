/**
 * @file ring_queue_impl.hpp
 * @brief RingQueue 模板实现
 * 
 * 高性能多生产者多消费者环形队列实现
 * 使用 Boost.Interprocess 共享内存
 */

#pragma once

#include <multiqueue/ring_queue.hpp>
#include <multiqueue/mp_logger.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <chrono>
#include <thread>
#include <cstring>

namespace multiqueue {

namespace bip = boost::interprocess;

/**
 * @brief RingQueue 实现
 */
template<typename T>
class RingQueue<T>::Impl {
public:
    /**
     * @brief 构造函数（创建新队列）
     */
    Impl(const std::string& name, const QueueConfig& config)
        : queue_name_(name)
        , config_(config)
        , is_creator_(true)
    {
        // 验证配置
        if (!config_.is_valid()) {
            throw std::invalid_argument("Invalid queue configuration");
        }
        
        // 确保容量是2的幂次
        if (!config_.is_power_of_two()) {
            config_.round_up_capacity_to_power_of_two();
            LOG_WARN_FMT("Queue capacity rounded up to power of 2: " << config_.capacity);
        }
        
        // 计算所需内存大小
        size_t metadata_size = sizeof(QueueMetadata);
        size_t control_size = sizeof(ControlBlock);
        size_t element_size = sizeof(ElementHeader) + sizeof(T);
        size_t total_size = metadata_size + control_size + 
                           (element_size * config_.capacity) + 4096; // 额外空间
        
        try {
            // 创建或打开共享内存
            shm_ = std::make_unique<bip::managed_shared_memory>(
                bip::open_or_create,
                name.c_str(),
                total_size
            );
            
            // 获取或创建元数据
            metadata_ = shm_->find_or_construct<QueueMetadata>("metadata")();
            
            // 初始化元数据（仅创建者）
            if (metadata_->magic_number != QUEUE_MAGIC_NUMBER) {
                metadata_->initialize(config_, sizeof(T));
                LOG_INFO_FMT("Created new queue: " << name << ", capacity: " << config_.capacity);
            } else {
                // 验证现有队列
                if (!metadata_->is_valid()) {
                    throw std::runtime_error("Invalid existing queue metadata");
                }
                if (metadata_->element_size != sizeof(T)) {
                    throw std::runtime_error("Element size mismatch");
                }
                is_creator_ = false;
                LOG_INFO_FMT("Opened existing queue: " << name);
            }
            
            // 获取或创建控制块
            control_ = shm_->find_or_construct<ControlBlock>("control")();
            if (is_creator_) {
                control_->initialize();
            }
            
            // 获取或创建数据缓冲区
            data_buffer_ = static_cast<uint8_t*>(
                shm_->find_or_construct<uint8_t[1]>("data_buffer")[element_size * config_.capacity]
            );
            
            // 增加生产者/消费者计数（根据模式）
            // 这里简化处理，实际使用时根据角色注册
            
        } catch (const bip::interprocess_exception& e) {
            LOG_ERROR_FMT("Failed to create/open shared memory: " << e.what());
            throw;
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~Impl() {
        try {
            // 减少计数
            // 如果是最后一个使用者，可以选择清理共享内存
            // 这里简化处理，不自动删除
        } catch (...) {
            // 析构函数不抛出异常
        }
    }
    
    /**
     * @brief 生产数据
     */
    bool push(const T& data, uint64_t timestamp) {
        MQ_TRACE_FUNC;
        
        if (control_->is_closed()) {
            return false;
        }
        
        // 增加生产者计数
        control_->producer_count.fetch_add(1, std::memory_order_relaxed);
        
        bool result = false;
        
        if (config_.blocking_mode == BlockingMode::BLOCKING) {
            result = push_blocking(data, timestamp);
        } else {
            result = push_non_blocking(data, timestamp);
        }
        
        // 减少生产者计数
        control_->producer_count.fetch_sub(1, std::memory_order_relaxed);
        
        return result;
    }
    
    /**
     * @brief 消费数据
     */
    bool pop(T& data, uint64_t* timestamp) {
        MQ_TRACE_FUNC;
        
        if (control_->is_closed() && is_empty()) {
            return false;
        }
        
        // 增加消费者计数
        control_->consumer_count.fetch_add(1, std::memory_order_relaxed);
        
        bool result = false;
        
        if (config_.blocking_mode == BlockingMode::BLOCKING) {
            result = pop_blocking(data, timestamp);
        } else {
            result = pop_non_blocking(data, timestamp);
        }
        
        // 减少消费者计数
        control_->consumer_count.fetch_sub(1, std::memory_order_relaxed);
        
        return result;
    }
    
    /**
     * @brief 获取队列大小
     */
    size_t size() const {
        uint64_t write_offset = control_->write_offset.load(std::memory_order_acquire);
        uint64_t read_offset = control_->read_offset.load(std::memory_order_acquire);
        return static_cast<size_t>(write_offset - read_offset);
    }
    
    /**
     * @brief 检查队列是否为空
     */
    bool is_empty() const {
        return size() == 0;
    }
    
    /**
     * @brief 检查队列是否已满
     */
    bool is_full() const {
        return size() >= config_.capacity;
    }
    
    /**
     * @brief 获取队列容量
     */
    size_t capacity() const {
        return config_.capacity;
    }
    
    /**
     * @brief 获取统计信息
     */
    QueueStats get_stats() const {
        QueueStats stats;
        stats.total_pushed = control_->total_pushed.load(std::memory_order_relaxed);
        stats.total_popped = control_->total_popped.load(std::memory_order_relaxed);
        stats.overwrite_count = control_->overwrite_count.load(std::memory_order_relaxed);
        stats.producer_count = control_->producer_count.load(std::memory_order_relaxed);
        stats.consumer_count = control_->consumer_count.load(std::memory_order_relaxed);
        stats.current_size = size();
        stats.capacity = config_.capacity;
        stats.is_closed = control_->is_closed();
        return stats;
    }
    
    /**
     * @brief 关闭队列
     */
    void close() {
        control_->close();
        LOG_INFO_FMT("Queue closed: " << queue_name_);
    }
    
    /**
     * @brief 检查队列是否已关闭
     */
    bool is_closed() const {
        return control_->is_closed();
    }
    
private:
    /**
     * @brief 阻塞模式 push
     */
    bool push_blocking(const T& data, uint64_t timestamp) {
        auto timeout = std::chrono::milliseconds(config_.timeout_ms);
        auto start_time = std::chrono::steady_clock::now();
        
        while (true) {
            if (control_->is_closed()) {
                return false;
            }
            
            // 尝试获取写位置
            uint64_t write_pos = control_->write_offset.load(std::memory_order_acquire);
            uint64_t read_pos = control_->read_offset.load(std::memory_order_acquire);
            
            // 检查是否有空间
            if (write_pos - read_pos >= config_.capacity) {
                // 队列已满，检查超时
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= timeout) {
                    return false;  // 超时
                }
                
                // 短暂休眠后重试
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            
            // 尝试 CAS 更新写位置
            if (control_->write_offset.compare_exchange_weak(
                write_pos, write_pos + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
                
                // 成功获取写位置，写入数据
                write_element(write_pos, data, timestamp);
                control_->total_pushed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // CAS 失败，重试
        }
    }
    
    /**
     * @brief 非阻塞模式 push
     */
    bool push_non_blocking(const T& data, uint64_t timestamp) {
        // 获取写位置
        uint64_t write_pos = control_->write_offset.fetch_add(1, std::memory_order_acq_rel);
        uint64_t read_pos = control_->read_offset.load(std::memory_order_acquire);
        
        // 检查是否会覆盖未读数据
        if (write_pos - read_pos >= config_.capacity) {
            // 记录覆盖
            control_->overwrite_count.fetch_add(1, std::memory_order_relaxed);
        }
        
        // 写入数据
        write_element(write_pos, data, timestamp);
        control_->total_pushed.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    /**
     * @brief 阻塞模式 pop
     */
    bool pop_blocking(T& data, uint64_t* timestamp) {
        auto timeout = std::chrono::milliseconds(config_.timeout_ms);
        auto start_time = std::chrono::steady_clock::now();
        
        while (true) {
            if (control_->is_closed() && is_empty()) {
                return false;
            }
            
            // 尝试获取读位置
            uint64_t read_pos = control_->read_offset.load(std::memory_order_acquire);
            uint64_t write_pos = control_->write_offset.load(std::memory_order_acquire);
            
            // 检查是否有数据
            if (read_pos >= write_pos) {
                // 队列为空，检查超时
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= timeout) {
                    return false;  // 超时
                }
                
                // 短暂休眠后重试
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            
            // 尝试 CAS 更新读位置
            if (control_->read_offset.compare_exchange_weak(
                read_pos, read_pos + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
                
                // 成功获取读位置，读取数据
                read_element(read_pos, data, timestamp);
                control_->total_popped.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // CAS 失败，重试
        }
    }
    
    /**
     * @brief 非阻塞模式 pop
     */
    bool pop_non_blocking(T& data, uint64_t* timestamp) {
        // 尝试获取读位置
        uint64_t read_pos = control_->read_offset.load(std::memory_order_acquire);
        uint64_t write_pos = control_->write_offset.load(std::memory_order_acquire);
        
        // 检查是否有数据
        if (read_pos >= write_pos) {
            return false;  // 队列为空
        }
        
        // 尝试 CAS 更新读位置
        if (!control_->read_offset.compare_exchange_strong(
            read_pos, read_pos + 1,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
            return false;  // 竞争失败
        }
        
        // 读取数据
        read_element(read_pos, data, timestamp);
        control_->total_popped.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    /**
     * @brief 写入元素
     */
    void write_element(uint64_t pos, const T& data, uint64_t timestamp) {
        size_t index = pos % config_.capacity;
        size_t element_size = sizeof(ElementHeader) + sizeof(T);
        uint8_t* element_ptr = data_buffer_ + (index * element_size);
        
        // 写入头部
        ElementHeader* header = reinterpret_cast<ElementHeader*>(element_ptr);
        header->initialize(pos, timestamp, sizeof(T));
        header->mark_valid();
        
        // 写入数据
        T* data_ptr = reinterpret_cast<T*>(element_ptr + sizeof(ElementHeader));
        *data_ptr = data;
        
        // 内存屏障确保数据可见
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    /**
     * @brief 读取元素
     */
    void read_element(uint64_t pos, T& data, uint64_t* timestamp) {
        size_t index = pos % config_.capacity;
        size_t element_size = sizeof(ElementHeader) + sizeof(T);
        uint8_t* element_ptr = data_buffer_ + (index * element_size);
        
        // 内存屏障确保读取最新数据
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 读取头部
        ElementHeader* header = reinterpret_cast<ElementHeader*>(element_ptr);
        
        if (timestamp) {
            *timestamp = header->timestamp;
        }
        
        // 读取数据
        T* data_ptr = reinterpret_cast<T*>(element_ptr + sizeof(ElementHeader));
        data = *data_ptr;
        
        // 标记为已读
        header->mark_read();
    }
    
private:
    std::string queue_name_;
    QueueConfig config_;
    bool is_creator_;
    
    std::unique_ptr<bip::managed_shared_memory> shm_;
    QueueMetadata* metadata_;
    ControlBlock* control_;
    uint8_t* data_buffer_;
};

// RingQueue 实现

template<typename T>
RingQueue<T>::RingQueue(const std::string& name, const QueueConfig& config)
    : impl_(std::make_unique<Impl>(name, config))
{
}

template<typename T>
RingQueue<T>::~RingQueue() = default;

template<typename T>
bool RingQueue<T>::push(const T& data, uint64_t timestamp) {
    return impl_->push(data, timestamp);
}

template<typename T>
bool RingQueue<T>::pop(T& data, uint64_t* timestamp) {
    return impl_->pop(data, timestamp);
}

template<typename T>
size_t RingQueue<T>::size() const {
    return impl_->size();
}

template<typename T>
bool RingQueue<T>::is_empty() const {
    return impl_->is_empty();
}

template<typename T>
bool RingQueue<T>::is_full() const {
    return impl_->is_full();
}

template<typename T>
size_t RingQueue<T>::capacity() const {
    return impl_->capacity();
}

template<typename T>
QueueStats RingQueue<T>::get_stats() const {
    return impl_->get_stats();
}

template<typename T>
void RingQueue<T>::close() {
    impl_->close();
}

template<typename T>
bool RingQueue<T>::is_closed() const {
    return impl_->is_closed();
}

} // namespace multiqueue

