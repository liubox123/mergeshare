#pragma once

#include "config.hpp"
#include "metadata.hpp"
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <sstream>
#include <cstring>
#include <iostream>

// 平台特定头文件
#ifdef _WIN32
    #include <process.h>
    #define getpid _getpid
#else
    #include <unistd.h>
#endif

namespace multiqueue {

namespace bip = boost::interprocess;

/**
 * @brief 环形队列模板类
 * 
 * 基于共享内存的多生产者-多消费者（MPMC）无锁环形队列
 * 
 * @tparam T 元素类型（必须是 trivially copyable）
 * 
 * 特性：
 * - 多生产者多消费者
 * - 无锁算法（基于 CAS 原子操作）
 * - 支持阻塞/非阻塞模式
 * - 支持时间戳
 * - 跨进程共享
 * 
 * 内存布局：
 * [QueueMetadata][ControlBlock][Element0][Element1]...[ElementN-1]
 */
template<typename T>
class RingQueue {
    // 确保 T 是 trivially copyable 类型
    static_assert(std::is_trivially_copyable<T>::value,
                  "T must be trivially copyable for shared memory usage");
    
public:
    /**
     * @brief 构造函数：创建或打开队列
     * 
     * @param name 队列名称（共享内存段名称）
     * @param config 队列配置
     * @throws std::runtime_error 如果创建/打开失败
     */
    explicit RingQueue(const std::string& name, const QueueConfig& config)
        : queue_name_(name)
        , config_(config)
        , shm_(nullptr)
        , metadata_(nullptr)
        , control_(nullptr)
        , data_(nullptr)
        , is_creator_(false)
        , consumer_slot_id_(-1)
        , is_consumer_(false)
    {
        std::cout<<" RingQueue() "<<std::endl;
        std::cout.flush();
        // 验证配置
        if (!config.is_valid()) {
            throw std::invalid_argument("Invalid queue configuration");
        }
        
        // 计算共享内存大小
        size_t element_full_size = sizeof(ElementHeader) + sizeof(T);
        size_t total_size = sizeof(QueueMetadata) + sizeof(ControlBlock) + 
                           element_full_size * config.capacity;
        
        // 根据角色选择不同的打开/创建策略
        if (config.queue_role == QueueRole::CONSUMER) {
            // ========== 消费者模式：只打开，不创建 ==========
            bool opened = false;
            uint32_t retry_count = 0;
            
            while (!opened && retry_count <= config.open_retry_count) {
                try {
                    std::cout<<"consumer make_unique"<<std::endl;
                    std::cout.flush();
                    shm_ = std::make_unique<bip::managed_shared_memory>(
                        bip::open_only, name.c_str()
                    );
                    
                    // 验证魔数
                    void* base_addr = shm_->get_address();
                    QueueMetadata* temp_meta = static_cast<QueueMetadata*>(base_addr);
                    
                    if (temp_meta->magic_number != QUEUE_MAGIC_NUMBER) {
                        throw std::runtime_error("Invalid shared memory: bad magic number");
                    }
                    
                    opened = true;
                    is_creator_ = false;
                    
                } catch (const bip::interprocess_exception& e) {
                    // 共享内存不存在或无法打开
                    if (retry_count < config.open_retry_count) {
                        retry_count++;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(config.open_retry_interval_ms)
                        );
                    } else {
                        throw std::runtime_error(
                            std::string("Consumer failed to open shared memory after ") +
                            std::to_string(config.open_retry_count) + " retries: " + e.what()
                        );
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(
                        std::string("Consumer failed to open shared memory: ") + e.what()
                    );
                }
            }
            
        } else {
            // ========== 生产者模式：可以创建或打开 ==========
            bool need_recreate = false;
            
            try {
                // 先尝试打开现有的共享内存
                std::cout<<"producer make_unique"<<std::endl;
                std::cout.flush();
                shm_ = std::make_unique<bip::managed_shared_memory>(
                    bip::open_only, name.c_str()
                );
                is_creator_ = false;
                
                // 映射内存进行初步验证
                void* base_addr = shm_->get_address();
                QueueMetadata* temp_meta = static_cast<QueueMetadata*>(base_addr);
                
                // 检查魔数，如果无效说明共享内存已损坏
                if (temp_meta->magic_number != QUEUE_MAGIC_NUMBER) {
                    need_recreate = true;
                    shm_.reset();  // 释放
                }
                
            } catch (const bip::interprocess_exception&) {
                need_recreate = true;
            }
            
            // 如果需要创建或重建
            if (need_recreate) {
                try {
                    // 删除可能存在的损坏的共享内存
                    bip::shared_memory_object::remove(name.c_str());
                    
                    // 创建新的共享内存
                    std::cout<<"producer create"<<std::endl;
                    std::cout.flush();
                    shm_ = std::make_unique<bip::managed_shared_memory>(
                        bip::create_only, name.c_str(), total_size
                    );
                    is_creator_ = true;
                    
                } catch (const std::exception& e) {
                    throw std::runtime_error(
                        std::string("Producer failed to create shared memory: ") + e.what()
                    );
                }
            }
        }
        
        // 映射元数据、控制块和数据区
        void* base_addr = shm_->get_address();
        metadata_ = static_cast<QueueMetadata*>(base_addr);
        control_ = reinterpret_cast<ControlBlock*>(
            static_cast<char*>(base_addr) + sizeof(QueueMetadata)
        );
        data_ = static_cast<char*>(base_addr) + sizeof(QueueMetadata) + sizeof(ControlBlock);
        
        // 如果是创建者，初始化元数据和控制块
        if (is_creator_) {
            metadata_->initialize(config, sizeof(T));
            control_->initialize();
        } else {
            // 如果是打开者，验证元数据
            if (!metadata_->is_valid()) {
                throw std::runtime_error("Invalid metadata in shared memory");
            }
            
            // 检查类型兼容性
            if (metadata_->element_size != sizeof(T)) {
                throw std::runtime_error("Element size mismatch");
            }
            
            if (metadata_->capacity != config.capacity) {
                throw std::runtime_error("Capacity mismatch");
            }
        }
        
        // 注册生产者
        control_->producer_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 析构函数
     */
    ~RingQueue() {
        try {
            // 注销消费者
            if (is_consumer_ && consumer_slot_id_ >= 0 && control_) {
                control_->consumers.unregister_consumer(consumer_slot_id_);
            }
            
            // 注销生产者
            if (control_) {
                control_->producer_count.fetch_sub(1, std::memory_order_relaxed);
            }
            
            // 如果是创建者且是最后一个使用者，可以选择删除共享内存
            // （但通常我们保留共享内存以便其他进程使用）
            
        } catch (...) {
            // 析构函数不应抛出异常
        }
    }
    
    // 禁止拷贝和移动
    RingQueue(const RingQueue&) = delete;
    RingQueue& operator=(const RingQueue&) = delete;
    RingQueue(RingQueue&&) = delete;
    RingQueue& operator=(RingQueue&&) = delete;
    
    /**
     * @brief 写入数据（阻塞模式）
     * 
     * @param data 要写入的数据
     * @param timestamp 时间戳（如果队列启用时间戳）
     * @return true 写入成功，false 超时或失败
     */
    bool push(const T& data, uint64_t timestamp = 0) {
        if (config_.blocking_mode == BlockingMode::BLOCKING) {
            return push_blocking(data, timestamp);
        } else {
            return push_non_blocking(data, timestamp);
        }
    }
    
    /**
     * @brief 尝试写入数据（不阻塞）
     * 
     * @param data 要写入的数据
     * @param timestamp 时间戳
     * @return true 写入成功，false 队列满
     */
    bool try_push(const T& data, uint64_t timestamp = 0) {
        uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
        uint64_t slowest_read = control_->consumers.get_slowest_offset();
        
        // 检查队列是否满（相对于最慢的消费者）
        if (write_idx - slowest_read >= config_.capacity) {
            return false;
        }
        
        // 尝试 CAS 获取写入位置
        uint64_t next_write = write_idx + 1;
        if (!control_->write_offset.compare_exchange_strong(
                write_idx, next_write,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return false;  // 被其他生产者抢先
        }
        
        // 写入数据
        write_element(write_idx, data, timestamp);
        
        return true;
    }
    
    /**
     * @brief 带超时的写入
     * 
     * @param data 要写入的数据
     * @param timeout_ms 超时时间（毫秒）
     * @param timestamp 时间戳
     * @return true 写入成功，false 超时
     */
    bool push_with_timeout(const T& data, uint32_t timeout_ms, uint64_t timestamp = 0) {
        auto start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeout_ms);
        
        while (true) {
            if (try_push(data, timestamp)) {
                return true;
            }
            
            // 检查超时
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                return false;
            }
            
            // 短暂等待（自旋或让出 CPU）
            if (config_.enable_spin_wait) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    /**
     * @brief 读取数据（阻塞模式）
     * 
     * @param data 输出参数，存储读取的数据
     * @param timestamp 输出参数，存储时间戳（可选）
     * @return true 读取成功，false 超时或队列已关闭
     */
    bool pop(T& data, uint64_t* timestamp = nullptr) {
        if (config_.blocking_mode == BlockingMode::BLOCKING) {
            return pop_blocking(data, timestamp);
        } else {
            return try_pop(data, timestamp);
        }
    }
    
    /**
     * @brief 尝试读取数据（不阻塞）
     * 
     * 广播模式：每个消费者独立读取数据
     * 
     * @param data 输出参数
     * @param timestamp 输出参数（可选）
     * @return true 读取成功，false 队列空或未注册消费者
     */
    bool try_pop(T& data, uint64_t* timestamp = nullptr) {
        // 检查是否已注册为消费者
        if (!is_consumer_ || consumer_slot_id_ < 0) {
            return false;  // 未注册消费者
        }
        
        // 获取当前消费者的槽位
        ConsumerSlot& my_slot = control_->consumers.slots[consumer_slot_id_];
        
        // 读取我的读取位置
        uint64_t my_read = my_slot.read_offset.load(std::memory_order_acquire);
        uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
        
        // 检查是否有新数据
        if (my_read >= write_idx) {
            return false;  // 队列空（对于当前消费者）
        }
        
        // 读取数据
        read_element(my_read, data, timestamp);
        
        // 更新我的读取位置
        my_slot.read_offset.store(my_read + 1, std::memory_order_release);
        
        // 更新最后访问时间
        my_slot.update_access_time();
        
        return true;
    }
    
    /**
     * @brief 带超时的读取
     * 
     * @param data 输出参数
     * @param timeout_ms 超时时间（毫秒）
     * @param timestamp 输出参数（可选）
     * @return true 读取成功，false 超时
     */
    bool pop_with_timeout(T& data, uint32_t timeout_ms, uint64_t* timestamp = nullptr) {
        auto start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeout_ms);
        
        while (true) {
            if (try_pop(data, timestamp)) {
                return true;
            }
            
            // 检查超时
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                return false;
            }
            
            // 短暂等待
            if (config_.enable_spin_wait) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    /**
     * @brief 查看队首元素（不移除）
     * 
     * 广播模式：查看当前消费者的下一个待读元素
     * 
     * @param data 输出参数
     * @param timestamp 输出参数（可选）
     * @return true 成功，false 队列空或未注册
     */
    bool peek(T& data, uint64_t* timestamp = nullptr) const {
        // 检查是否已注册为消费者
        if (!is_consumer_ || consumer_slot_id_ < 0) {
            return false;
        }
        
        const ConsumerSlot& my_slot = control_->consumers.slots[consumer_slot_id_];
        uint64_t my_read = my_slot.read_offset.load(std::memory_order_acquire);
        uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
        
        if (my_read >= write_idx) {
            return false;
        }
        
        // 读取但不更新读取位置
        read_element(my_read, data, timestamp);
        
        return true;
    }
    
    /**
     * @brief 获取当前队列大小（对于当前消费者）
     * 
     * 广播模式：返回当前消费者的未读元素数量
     * 
     * @return 队列中的元素数量
     */
    size_t size() const {
        if (!is_consumer_ || consumer_slot_id_ < 0) {
            // 未注册消费者，返回队列总大小
            uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
            uint64_t slowest = control_->consumers.get_slowest_offset();
            if (write_idx < slowest) return 0;
            size_t sz = write_idx - slowest;
            return (sz > config_.capacity) ? config_.capacity : sz;
        }
        
        // 已注册消费者，返回当前消费者的未读数量
        const ConsumerSlot& my_slot = control_->consumers.slots[consumer_slot_id_];
        uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
        uint64_t my_read = my_slot.read_offset.load(std::memory_order_acquire);
        
        if (write_idx < my_read) {
            return 0;
        }
        
        size_t sz = write_idx - my_read;
        return (sz > config_.capacity) ? config_.capacity : sz;
    }
    
    /**
     * @brief 检查队列是否为空
     * @return true 如果队列为空
     */
    bool empty() const {
        return size() == 0;
    }
    
    /**
     * @brief 检查队列是否已满
     * @return true 如果队列已满（相对于最慢消费者）
     */
    bool full() const {
        uint64_t write_idx = control_->write_offset.load(std::memory_order_acquire);
        uint64_t slowest = control_->consumers.get_slowest_offset();
        return (write_idx - slowest) >= config_.capacity;
    }
    
    /**
     * @brief 获取队列容量
     * @return 队列容量
     */
    size_t capacity() const {
        return config_.capacity;
    }
    
    /**
     * @brief 获取元数据
     * @return 元数据引用
     */
    const QueueMetadata& metadata() const {
        return *metadata_;
    }
    
    /**
     * @brief 获取统计信息
     * @return 统计信息结构
     */
    QueueStats get_stats() const {
        QueueStats stats;
        
        stats.total_pushed = control_->total_pushed.load(std::memory_order_relaxed);
        stats.total_popped = control_->total_popped.load(std::memory_order_relaxed);
        stats.overwrite_count = control_->overwrite_count.load(std::memory_order_relaxed);
        stats.producer_count = control_->producer_count.load(std::memory_order_relaxed);
        stats.consumer_count = control_->consumers.active_count.load(std::memory_order_relaxed);
        stats.current_size = size();
        stats.capacity = config_.capacity;
        stats.created_at = metadata_->created_at;
        stats.last_write_time = control_->last_write_time.load(std::memory_order_relaxed);
        stats.last_read_time = control_->last_read_time.load(std::memory_order_relaxed);
        stats.is_closed = control_->is_closed();
        
        return stats;
    }
    
    /**
     * @brief 关闭队列
     * 
     * 关闭后，不能再写入数据，但可以继续读取已有数据
     */
    void close() {
        control_->close();
    }
    
    /**
     * @brief 检查队列是否已关闭
     * @return true 如果队列已关闭
     */
    bool is_closed() const {
        return control_->is_closed();
    }
    
    /**
     * @brief 获取队列名称
     * @return 队列名称
     */
    const std::string& name() const {
        return queue_name_;
    }
    
    /**
     * @brief 注册消费者（广播模式）
     * 
     * 每个消费者需要先注册才能独立读取数据。
     * 所有消费者都能读到全部数据（广播模式）。
     * 
     * @param consumer_id 消费者标识（可选，默认使用进程ID+线程ID）
     * @param from_beginning 是否从头开始读取（true=从0开始，false=从当前位置）
     * @return true 注册成功，false 失败（槽位已满）
     */
    bool register_consumer(const std::string& consumer_id = "", bool from_beginning = true) {
        std::cerr << "[register_consumer] START, id=" << consumer_id << ", from_beginning=" << from_beginning << std::endl;
        
        // 如果已经注册，先注销
        if (is_consumer_ && consumer_slot_id_ >= 0) {
            std::cerr << "[register_consumer] Already registered, unregistering..." << std::endl;
            unregister_consumer();
        }
        
        // 生成消费者ID
        std::string final_id = consumer_id;
        if (final_id.empty()) {
            // 使用进程ID + 线程ID作为默认ID
            std::ostringstream oss;
            oss << "p" << getpid() << "_t" << std::this_thread::get_id();
            final_id = oss.str();
        }
        std::cerr << "[register_consumer] Final ID: " << final_id << std::endl;
        
        // 确定起始读取位置
        std::cerr << "[register_consumer] Getting start offset..." << std::endl;
        uint64_t start_offset = 0;
        if (!from_beginning) {
            // 从当前写入位置开始（新消息模式）
            start_offset = control_->write_offset.load(std::memory_order_acquire);
        }
        std::cerr << "[register_consumer] Start offset: " << start_offset << std::endl;
        
        // 注册到空闲槽位
        std::cerr << "[register_consumer] Calling ConsumerRegistry::register_consumer..." << std::endl;
        int slot_id = control_->consumers.register_consumer(final_id.c_str(), start_offset);
        std::cerr << "[register_consumer] Got slot_id: " << slot_id << std::endl;
        
        if (slot_id >= 0) {
            consumer_slot_id_ = slot_id;
            is_consumer_ = true;
            std::cerr << "[register_consumer] SUCCESS, slot=" << slot_id << std::endl;
            return true;
        }
        
        std::cerr << "[register_consumer] FAILED, no available slots" << std::endl;
        return false;  // 槽位已满
    }
    
    /**
     * @brief 注销消费者
     */
    void unregister_consumer() {
        if (is_consumer_ && consumer_slot_id_ >= 0) {
            control_->consumers.unregister_consumer(consumer_slot_id_);
            consumer_slot_id_ = -1;
            is_consumer_ = false;
        }
    }
    
    /**
     * @brief 获取消费者槽位ID
     * @return 槽位ID，-1表示未注册
     */
    int get_consumer_slot_id() const {
        return consumer_slot_id_;
    }
    
    /**
     * @brief 获取活跃消费者数量
     * @return 活跃消费者数量
     */
    uint32_t get_active_consumer_count() const {
        return control_->consumers.active_count.load(std::memory_order_relaxed);
    }
    
private:
    /**
     * @brief 阻塞模式写入
     */
    bool push_blocking(const T& data, uint64_t timestamp) {
        return push_with_timeout(data, config_.timeout_ms, timestamp);
    }
    
    /**
     * @brief 非阻塞模式写入（可覆盖旧数据）
     */
    bool push_non_blocking(const T& data, uint64_t timestamp) {
        uint64_t write_idx = control_->write_offset.fetch_add(1, std::memory_order_acq_rel);
        
        // 检查是否覆盖了最慢消费者的未读数据
        uint64_t slowest_read = control_->consumers.get_slowest_offset();
        if (write_idx - slowest_read >= config_.capacity) {
            // 覆盖旧数据（多消费者模式下，只记录覆盖次数，不推进读取位置）
            control_->overwrite_count.fetch_add(1, std::memory_order_relaxed);
        }
        
        // 写入数据
        write_element(write_idx, data, timestamp);
        
        return true;
    }
    
    /**
     * @brief 阻塞模式读取
     */
    bool pop_blocking(T& data, uint64_t* timestamp) {
        return pop_with_timeout(data, config_.timeout_ms, timestamp);
    }
    
    /**
     * @brief 写入元素到指定位置
     */
    void write_element(uint64_t idx, const T& data, uint64_t timestamp) {
        size_t slot = idx % config_.capacity;
        size_t element_size = sizeof(ElementHeader) + sizeof(T);
        char* element_ptr = data_ + slot * element_size;
        
        ElementHeader* header = reinterpret_cast<ElementHeader*>(element_ptr);
        T* data_ptr = reinterpret_cast<T*>(element_ptr + sizeof(ElementHeader));
        
        // 初始化头部
        header->initialize(idx, timestamp, sizeof(T));
        
        // 拷贝数据
        std::memcpy(data_ptr, &data, sizeof(T));
        
        // 标记数据有效
        header->mark_valid();
        
        // 更新统计信息
        control_->total_pushed.fetch_add(1, std::memory_order_relaxed);
        control_->last_write_time.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_relaxed
        );
    }
    
    /**
     * @brief 从指定位置读取元素
     */
    void read_element(uint64_t idx, T& data, uint64_t* timestamp) const {
        size_t slot = idx % config_.capacity;
        size_t element_size = sizeof(ElementHeader) + sizeof(T);
        const char* element_ptr = data_ + slot * element_size;
        
        const ElementHeader* header = reinterpret_cast<const ElementHeader*>(element_ptr);
        const T* data_ptr = reinterpret_cast<const T*>(element_ptr + sizeof(ElementHeader));
        
        // 等待数据有效
        while (!header->is_valid()) {
            std::this_thread::yield();
        }
        
        // 读取数据
        std::memcpy(&data, data_ptr, sizeof(T));
        
        // 读取时间戳
        if (timestamp && config_.has_timestamp) {
            *timestamp = header->timestamp;
        }
        
        // 更新统计信息
        control_->total_popped.fetch_add(1, std::memory_order_relaxed);
        control_->last_read_time.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_relaxed
        );
    }
    
private:
    std::string queue_name_;                            ///< 队列名称
    QueueConfig config_;                                ///< 队列配置
    std::unique_ptr<bip::managed_shared_memory> shm_;   ///< 共享内存对象
    QueueMetadata* metadata_;                           ///< 元数据指针
    ControlBlock* control_;                             ///< 控制块指针
    char* data_;                                        ///< 数据区指针
    bool is_creator_;                                   ///< 是否是创建者
    int consumer_slot_id_;                              ///< 当前消费者的槽位ID（-1表示未注册）
    bool is_consumer_;                                  ///< 是否作为消费者（需要注销）
};

} // namespace multiqueue


