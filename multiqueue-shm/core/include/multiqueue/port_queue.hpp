/**
 * @file port_queue.hpp
 * @brief 端口队列实现
 * 
 * PortQueue 用于 Block 之间传递 Buffer ID，存储在共享内存中
 */

#pragma once

#include "types.hpp"
#include <atomic>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <chrono>

namespace multiqueue {

using namespace boost::interprocess;

/**
 * @brief 端口队列头部（存储在共享内存）
 */
struct PortQueueHeader {
    uint32_t magic_number;               ///< 魔数
    PortId port_id;                      ///< 端口 ID
    size_t capacity;                     ///< 队列容量
    std::atomic<size_t> size;            ///< 当前元素数量
    std::atomic<size_t> head;            ///< 队列头（读位置）
    std::atomic<size_t> tail;            ///< 队列尾（写位置）
    
    interprocess_mutex mutex;            ///< 互斥锁
    interprocess_condition not_empty;    ///< 非空条件变量
    interprocess_condition not_full;     ///< 非满条件变量
    
    std::atomic<bool> initialized;       ///< 是否已初始化
    std::atomic<bool> closed;            ///< 是否已关闭
    
    PortQueueHeader() noexcept
        : magic_number(SHM_MAGIC_NUMBER)
        , port_id(INVALID_PORT_ID)
        , capacity(0)
        , size(0)
        , head(0)
        , tail(0)
        , mutex()
        , not_empty()
        , not_full()
        , initialized(false)
        , closed(false)
    {}
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
            header_->size.store(0, std::memory_order_relaxed);
            header_->head.store(0, std::memory_order_relaxed);
            header_->tail.store(0, std::memory_order_relaxed);
            header_->initialized.store(false, std::memory_order_relaxed);
            header_->closed.store(false, std::memory_order_relaxed);
            
            // 初始化锁和条件变量（使用 placement new）
            new (&header_->mutex) interprocess_mutex();
            new (&header_->not_empty) interprocess_condition();
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
     * @brief 推送 Buffer ID（阻塞）
     * 
     * @param buffer_id Buffer ID
     * @return true 成功，false 失败（队列已关闭）
     */
    bool push(BufferId buffer_id) {
        if (!header_ || header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        // 等待队列非满
        while (header_->size.load(std::memory_order_acquire) >= header_->capacity) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            header_->not_full.wait(lock);
        }
        
        // 写入数据
        size_t tail = header_->tail.load(std::memory_order_relaxed);
        data_[tail] = buffer_id;
        
        // 更新尾指针
        header_->tail.store((tail + 1) % header_->capacity, std::memory_order_release);
        header_->size.fetch_add(1, std::memory_order_release);
        
        // 通知等待的读者
        header_->not_empty.notify_one();
        
        return true;
    }
    
    /**
     * @brief 推送 Buffer ID（超时）
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
        
        // 等待队列非满
        while (header_->size.load(std::memory_order_acquire) >= header_->capacity) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            
            if (!header_->not_full.timed_wait(lock, 
                boost::posix_time::microsec_clock::universal_time() + 
                boost::posix_time::milliseconds(timeout_ms))) {
                return false;  // 超时
            }
            
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;  // 超时
            }
        }
        
        // 写入数据
        size_t tail = header_->tail.load(std::memory_order_relaxed);
        data_[tail] = buffer_id;
        
        // 更新尾指针
        header_->tail.store((tail + 1) % header_->capacity, std::memory_order_release);
        header_->size.fetch_add(1, std::memory_order_release);
        
        // 通知等待的读者
        header_->not_empty.notify_one();
        
        return true;
    }
    
    /**
     * @brief 弹出 Buffer ID（阻塞）
     * 
     * @param buffer_id 输出参数，Buffer ID
     * @return true 成功，false 失败（队列已关闭且为空）
     */
    bool pop(BufferId& buffer_id) {
        if (!header_) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        // 等待队列非空
        while (header_->size.load(std::memory_order_acquire) == 0) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            header_->not_empty.wait(lock);
        }
        
        // 读取数据
        size_t head = header_->head.load(std::memory_order_relaxed);
        buffer_id = data_[head];
        
        // 更新头指针
        header_->head.store((head + 1) % header_->capacity, std::memory_order_release);
        header_->size.fetch_sub(1, std::memory_order_release);
        
        // 通知等待的写者
        header_->not_full.notify_one();
        
        return true;
    }
    
    /**
     * @brief 弹出 Buffer ID（超时）
     * 
     * @param buffer_id 输出参数，Buffer ID
     * @param timeout_ms 超时时间（毫秒）
     * @return true 成功，false 失败（超时或队列已关闭且为空）
     */
    bool pop_with_timeout(BufferId& buffer_id, uint32_t timeout_ms) {
        if (!header_) {
            return false;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->mutex);
        
        auto deadline = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(timeout_ms);
        
        // 等待队列非空
        while (header_->size.load(std::memory_order_acquire) == 0) {
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            
            if (!header_->not_empty.timed_wait(lock,
                boost::posix_time::microsec_clock::universal_time() + 
                boost::posix_time::milliseconds(timeout_ms))) {
                return false;  // 超时
            }
            
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;  // 超时
            }
        }
        
        // 读取数据
        size_t head = header_->head.load(std::memory_order_relaxed);
        buffer_id = data_[head];
        
        // 更新头指针
        header_->head.store((head + 1) % header_->capacity, std::memory_order_release);
        header_->size.fetch_sub(1, std::memory_order_release);
        
        // 通知等待的写者
        header_->not_full.notify_one();
        
        return true;
    }
    
    /**
     * @brief 获取队列大小
     */
    size_t size() const {
        if (!header_) {
            return 0;
        }
        return header_->size.load(std::memory_order_acquire);
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
     * @brief 是否为空
     */
    bool empty() const {
        return size() == 0;
    }
    
    /**
     * @brief 是否已满
     */
    bool full() const {
        if (!header_) {
            return false;
        }
        return header_->size.load(std::memory_order_acquire) >= header_->capacity;
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
        header_->not_empty.notify_all();
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
    
private:
    PortQueueHeader* header_;          ///< 头部指针（进程本地）
    BufferId* data_;                   ///< 数据数组（进程本地）
    
    shared_memory_object shm_;         ///< 共享内存对象（进程本地）
    mapped_region region_;             ///< 映射区域（进程本地）
};

}  // namespace multiqueue

