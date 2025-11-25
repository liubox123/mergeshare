/**
 * @file port_queue.hpp
 * @brief 端口队列实现（广播模式）
 * 
 * PortQueue 用于 Block 之间传递 Buffer ID，存储在共享内存中
 * 支持广播模式：多个消费者可以同时读取相同的数据
 */

#pragma once

#include "types.hpp"
#include "buffer_allocator.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace multiqueue {

using namespace boost::interprocess;

/**
 * @brief 消费者 ID 类型
 */
using ConsumerId = uint32_t;
constexpr ConsumerId INVALID_CONSUMER_ID = static_cast<ConsumerId>(-1);

/**
 * @brief 端口队列头部（存储在共享内存）
 * 
 * 支持广播模式：每个消费者有独立的读指针，可以同时读取相同数据
 */
struct PortQueueHeader {
    static constexpr uint32_t MAX_CONSUMERS = 16;  ///< 最大消费者数量
    
    uint32_t magic_number;                         ///< 魔数
    PortId port_id;                                ///< 端口 ID
    size_t capacity;                               ///< 队列容量
    std::atomic<size_t> tail;                      ///< 队列尾（写位置）
    
    // 消费者管理（广播模式）
    std::atomic<uint32_t> consumer_count;          ///< 已注册的消费者数量
    std::atomic<size_t> consumer_heads[MAX_CONSUMERS];  ///< 每个消费者的读指针
    std::atomic<bool> consumer_active[MAX_CONSUMERS];   ///< 消费者是否活跃
    
    interprocess_mutex mutex;                      ///< 互斥锁
    interprocess_condition not_full;               ///< 非满条件变量
    
    std::atomic<bool> initialized;                 ///< 是否已初始化
    std::atomic<bool> closed;                      ///< 是否已关闭
    
    PortQueueHeader() noexcept
        : magic_number(SHM_MAGIC_NUMBER)
        , port_id(INVALID_PORT_ID)
        , capacity(0)
        , tail(0)
        , consumer_count(0)
        , mutex()
        , not_full()
        , initialized(false)
        , closed(false)
    {
        // 初始化消费者数组
        for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
            consumer_heads[i].store(0, std::memory_order_relaxed);
            consumer_active[i].store(false, std::memory_order_relaxed);
        }
    }
};

/**
 * @brief 端口队列（存储在共享内存）
 * 
 * 内存布局：
 * [PortQueueHeader][BufferId array: capacity]
 */
class PortQueue {
public:
    /**
     * @brief 构造函数（进程本地对象）
     */
    PortQueue()
        : header_(nullptr)
        , data_(nullptr)
        , allocator_(nullptr)
        , shm_()
        , region_()
    {}
    
    /**
     * @brief 析构函数
     */
    ~PortQueue() {
        // 不删除共享内存，只解除映射
    }
    
    /**
     * @brief 设置 Buffer Allocator（用于管理引用计数）
     */
    void set_allocator(SharedBufferAllocator* allocator) {
        allocator_ = allocator;
    }
    
    /**
     * @brief 创建端口队列（第一个进程调用）
     * 
     * @param name 共享内存名称
     * @param port_id 端口 ID
     * @param capacity 队列容量
     * @return true 成功，false 失败
     */
    bool create(const char* name, PortId port_id, size_t capacity) {
        try {
            // 计算总大小
            size_t header_size = sizeof(PortQueueHeader);
            size_t data_size = sizeof(BufferId) * capacity;
            size_t total_size = header_size + data_size;
            
            // 创建共享内存
            shared_memory_object::remove(name);  // 删除旧的
            shm_ = shared_memory_object(
                create_only,
                name,
                read_write
            );
            shm_.truncate(total_size);
            
            // 映射到进程地址空间
            region_ = mapped_region(shm_, read_write);
            
            // 获取指针
            char* base = static_cast<char*>(region_.get_address());
            header_ = reinterpret_cast<PortQueueHeader*>(base);
            data_ = reinterpret_cast<BufferId*>(base + header_size);
            
            // 初始化头部（不使用 placement new，直接设置字段）
            header_->magic_number = SHM_MAGIC_NUMBER;
            header_->port_id = port_id;
            header_->capacity = capacity;
            header_->tail.store(0, std::memory_order_relaxed);
            header_->consumer_count.store(0, std::memory_order_relaxed);
            header_->initialized.store(false, std::memory_order_relaxed);
            header_->closed.store(false, std::memory_order_relaxed);
            
            // 初始化消费者数组
            for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
                header_->consumer_heads[i].store(0, std::memory_order_relaxed);
                header_->consumer_active[i].store(false, std::memory_order_relaxed);
            }
            
            // 初始化锁和条件变量（使用 placement new）
            new (&header_->mutex) interprocess_mutex();
            new (&header_->not_full) interprocess_condition();
            
            // 初始化数据数组
            for (size_t i = 0; i < capacity; ++i) {
                data_[i] = INVALID_BUFFER_ID;
            }
            
            // 标记为已初始化
            header_->initialized.store(true, std::memory_order_release);
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    /**
     * @brief 打开已存在的端口队列（后续进程调用）
     * 
     * @param name 共享内存名称
     * @return true 成功，false 失败
     */
    bool open(const char* name) {
        try {
            // 打开共享内存
            shm_ = shared_memory_object(
                open_only,
                name,
                read_write
            );
            
            // 映射到进程地址空间
            region_ = mapped_region(shm_, read_write);
            
            // 获取指针
            char* base = static_cast<char*>(region_.get_address());
            header_ = reinterpret_cast<PortQueueHeader*>(base);
            
            // 验证魔数
            if (header_->magic_number != SHM_MAGIC_NUMBER) {
                return false;
            }
            
            // 等待初始化完成
            while (!header_->initialized.load(std::memory_order_acquire)) {
                // 自旋等待
            }
            
            // 计算数据指针
            size_t header_size = sizeof(PortQueueHeader);
            data_ = reinterpret_cast<BufferId*>(base + header_size);
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    /**
     * @brief 注册消费者（广播模式）
     * 
     * @return 消费者 ID，INVALID_CONSUMER_ID 表示失败
     */
    ConsumerId register_consumer() {
        if (!header_) {
            return INVALID_CONSUMER_ID;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        // 查找空闲槽位
        for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
            if (!header_->consumer_active[i].load(std::memory_order_acquire)) {
                // 初始化消费者读指针为当前 tail（从当前位置开始读取）
                size_t current_tail = header_->tail.load(std::memory_order_acquire);
                header_->consumer_heads[i].store(current_tail, std::memory_order_release);
                header_->consumer_active[i].store(true, std::memory_order_release);
                header_->consumer_count.fetch_add(1, std::memory_order_release);
                return i;
            }
        }
        
        return INVALID_CONSUMER_ID;  // 消费者数量已满
    }
    
    /**
     * @brief 注销消费者
     * 
     * @param consumer_id 消费者 ID
     */
    void unregister_consumer(ConsumerId consumer_id) {
        if (!header_ || consumer_id >= PortQueueHeader::MAX_CONSUMERS) {
            return;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        if (!header_->consumer_active[consumer_id].load(std::memory_order_acquire)) {
            return;  // 消费者已经不活跃
        }
        
        // 释放该消费者尚未读取的所有 buffer 的引用
        if (allocator_) {
            size_t head = header_->consumer_heads[consumer_id].load(std::memory_order_acquire);
            size_t tail = header_->tail.load(std::memory_order_acquire);
            for (size_t i = head; i < tail; ++i) {
                BufferId buffer_id = data_[i % header_->capacity];
                if (buffer_id != INVALID_BUFFER_ID) {
                    // 减少引用计数
                    allocator_->remove_ref(buffer_id);
                }
            }
        }
        
        // 标记为非活跃
        header_->consumer_active[consumer_id].store(false, std::memory_order_release);
        header_->consumer_count.fetch_sub(1, std::memory_order_release);
        
        // 通知生产者（可能现在有空间了）
        header_->not_full.notify_all();
    }
    
    /**
     * @brief 推送 Buffer ID（阻塞，广播模式）
     * 
     * @param buffer_id Buffer ID
     * @return true 成功，false 失败（队列已关闭）
     */
    bool push(BufferId buffer_id) {
        if (!header_ || header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        size_t capacity = header_->capacity;
        
        // 等待队列非满：找到最慢的消费者
        while (true) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            
            size_t tail = header_->tail.load(std::memory_order_acquire);
            size_t min_head = tail;  // 初始值为 tail（如果没有消费者）
            
            // 找到所有活跃消费者中最慢的（head 最小的）
            uint32_t active_consumer_count = 0;
            for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
                if (header_->consumer_active[i].load(std::memory_order_acquire)) {
                    size_t head = header_->consumer_heads[i].load(std::memory_order_acquire);
                    if (active_consumer_count == 0 || head < min_head) {
                        min_head = head;
                    }
                    active_consumer_count++;
                }
            }
            
            // 如果没有消费者，直接返回失败（或者可以允许push但不广播）
            if (active_consumer_count == 0) {
                // 可选策略：允许生产但数据会丢失
                // 或者返回 false 强制要求至少有一个消费者
                // 这里我们选择：允许生产，但数据不会被任何人读取
                // 为了简化，我们直接 push
            }
            
            // 检查队列是否满：如果最慢的消费者落后 capacity，则队列满
            if (tail >= min_head + capacity) {
                // 队列满，等待
                header_->not_full.wait(lock);
                continue;
            }
            
            break;  // 有空间，跳出循环
        }
        
        // 写入数据
        size_t tail = header_->tail.load(std::memory_order_relaxed);
        data_[tail % capacity] = buffer_id;
        
        // 增加 buffer 引用计数（关键！每个活跃消费者都需要一份引用）
        if (allocator_) {
            uint32_t active_count = header_->consumer_count.load(std::memory_order_acquire);
            // 为每个消费者增加一次引用（除了初始的 1 次）
            for (uint32_t i = 1; i < active_count; ++i) {
                allocator_->add_ref(buffer_id);
            }
        }
        
        // 更新 tail
        header_->tail.fetch_add(1, std::memory_order_release);
        
        // 注意：不需要 notify（消费者主动轮询）
        // 如果需要阻塞式读取，可以为每个消费者添加独立的条件变量
        
        return true;
    }
    
    /**
     * @brief 推送 Buffer ID（超时，广播模式）
     * 
     * @param buffer_id Buffer ID
     * @param timeout_ms 超时时间（毫秒）
     * @return true 成功，false 失败（超时或队列已关闭）
     */
    bool push_with_timeout(BufferId buffer_id, uint32_t timeout_ms) {
        if (!header_ || header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        auto deadline = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(timeout_ms);
        
        size_t capacity = header_->capacity;
        
        // 等待队列非满
        while (true) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            
            size_t tail = header_->tail.load(std::memory_order_acquire);
            size_t min_head = tail;
            
            // 找到最慢的消费者
            uint32_t active_consumer_count = 0;
            for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
                if (header_->consumer_active[i].load(std::memory_order_acquire)) {
                    size_t head = header_->consumer_heads[i].load(std::memory_order_acquire);
                    if (active_consumer_count == 0 || head < min_head) {
                        min_head = head;
                    }
                    active_consumer_count++;
                }
            }
            
            // 检查是否满
            if (tail >= min_head + capacity) {
                // 队列满，等待超时
                if (!header_->not_full.timed_wait(lock, 
                    boost::posix_time::microsec_clock::universal_time() + 
                    boost::posix_time::milliseconds(timeout_ms))) {
                    return false;  // 超时
                }
                
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;  // 超时
                }
                continue;
            }
            
            break;  // 有空间
        }
        
        // 写入数据
        size_t tail = header_->tail.load(std::memory_order_relaxed);
        data_[tail % capacity] = buffer_id;
        
        // 增加引用计数
        // 在广播模式下，Buffer 在队列中时需要有一个引用计数，防止被释放
        // Buffer 在 push 之前已经有一个引用（从分配时，由 BufferPtr 持有）
        // 当 BufferPtr 析构时，这个引用会被释放
        // 所以需要增加 1 次引用，确保 Buffer 在队列中时不会被释放
        // 当消费者 pop 时，BufferPtr 会增加引用计数，所以 Buffer 不会被释放
        if (allocator_) {
            allocator_->add_ref(buffer_id);
        }
        
        // 更新 tail
        header_->tail.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 弹出 Buffer ID（非阻塞，广播模式）
     * 
     * @param consumer_id 消费者 ID
     * @param buffer_id 输出参数，Buffer ID
     * @return true 成功，false 失败（队列为空或已关闭）
     */
    bool pop(ConsumerId consumer_id, BufferId& buffer_id) {
        if (!header_ || consumer_id >= PortQueueHeader::MAX_CONSUMERS) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        // 检查消费者是否活跃
        if (!header_->consumer_active[consumer_id].load(std::memory_order_acquire)) {
            return false;
        }
        
        // 获取该消费者的读指针
        size_t head = header_->consumer_heads[consumer_id].load(std::memory_order_acquire);
        size_t tail = header_->tail.load(std::memory_order_acquire);
        
        // 检查是否有数据
        if (head >= tail) {
            return false;  // 没有数据
        }
        
        // 读取数据
        buffer_id = data_[head % header_->capacity];
        
        // 更新该消费者的读指针
        header_->consumer_heads[consumer_id].fetch_add(1, std::memory_order_release);
        
        // 注意：不在这里减少引用计数
        // push 时增加的引用计数（1 次）表示队列持有 Buffer
        // 当 BufferPtr 构造函数时，会增加引用计数
        // 当 BufferPtr 析构时，会减少引用计数
        // 但是，push 时增加的引用计数需要在所有消费者都读取后减少
        // 这应该在 BufferPtr 析构时处理，或者由队列管理
        // 目前，我们假设 BufferPtr 会管理引用计数，所以这里不需要操作
        
        // 检查是否是最后一个消费者读取该位置
        // 如果是，通知生产者（可能有空间了）
        bool all_passed = true;
        for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
            if (header_->consumer_active[i].load(std::memory_order_acquire)) {
                if (header_->consumer_heads[i].load(std::memory_order_acquire) <= head) {
                    all_passed = false;
                    break;
                }
            }
        }
        
        if (all_passed) {
            header_->not_full.notify_all();
        }
        
        return true;
    }
    
    /**
     * @brief 弹出 Buffer ID（超时，广播模式）
     * 
     * @param consumer_id 消费者 ID
     * @param buffer_id 输出参数，Buffer ID
     * @param timeout_ms 超时时间（毫秒）
     * @return true 成功，false 失败（超时或队列已关闭）
     */
    bool pop_with_timeout(ConsumerId consumer_id, BufferId& buffer_id, uint32_t timeout_ms) {
        if (!header_ || consumer_id >= PortQueueHeader::MAX_CONSUMERS) {
            return false;
        }
        
        auto deadline = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(timeout_ms);
        
        // 简单轮询实现（可以优化为条件变量）
        while (true) {
            if (pop(consumer_id, buffer_id)) {
                return true;
            }
            
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;  // 超时
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    /**
     * @brief 获取队列中待读取的数据数量（对于指定消费者）
     */
    size_t size(ConsumerId consumer_id) const {
        if (!header_ || consumer_id >= PortQueueHeader::MAX_CONSUMERS) {
            return 0;
        }
        
        if (!header_->consumer_active[consumer_id].load(std::memory_order_acquire)) {
            return 0;
        }
        
        size_t head = header_->consumer_heads[consumer_id].load(std::memory_order_acquire);
        size_t tail = header_->tail.load(std::memory_order_acquire);
        
        if (tail > head) {
            return tail - head;
        }
        return 0;
    }
    
    /**
     * @brief 获取队列容量
     */
    size_t capacity() const {
        if (!header_) {
            return 0;
        }
        return header_->capacity;
    }
    
    /**
     * @brief 队列是否为空（对于指定消费者）
     */
    bool empty(ConsumerId consumer_id) const {
        return size(consumer_id) == 0;
    }
    
    /**
     * @brief 是否已关闭
     */
    bool is_closed() const {
        if (!header_) {
            return true;
        }
        return header_->closed.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 关闭队列
     */
    void close() {
        if (!header_) {
            return;
        }
        
        header_->closed.store(true, std::memory_order_release);
        
        // 唤醒所有等待的线程
        header_->not_full.notify_all();
    }
    
    /**
     * @brief 是否有效
     */
    bool is_valid() const {
        return header_ != nullptr &&
               header_->magic_number == SHM_MAGIC_NUMBER &&
               header_->initialized.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取活跃消费者数量
     */
    uint32_t get_consumer_count() const {
        if (!header_) {
            return 0;
        }
        return header_->consumer_count.load(std::memory_order_acquire);
    }
    
private:
    PortQueueHeader* header_;                  ///< 头部指针（进程本地）
    BufferId* data_;                           ///< 数据数组（进程本地）
    SharedBufferAllocator* allocator_;         ///< Buffer 分配器（用于引用计数）
    
    shared_memory_object shm_;                 ///< 共享内存对象（进程本地）
    mapped_region region_;                     ///< 映射区域（进程本地）
};

}  // namespace multiqueue

