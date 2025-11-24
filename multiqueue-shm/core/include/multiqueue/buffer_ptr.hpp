/**
 * @file buffer_ptr.hpp
 * @brief Buffer 智能指针
 * 
 * BufferPtr 是进程本地的轻量级包装器，自动管理 Buffer 引用计数
 */

#pragma once

#include "types.hpp"
#include "timestamp.hpp"
#include "buffer_allocator.hpp"
#include <utility>

namespace multiqueue {

/**
 * @brief Buffer 智能指针（进程本地对象）
 * 
 * 注意：
 * 1. BufferPtr 是进程本地的对象，不存储在共享内存中
 * 2. 它只是共享内存中 BufferMetadata 的包装器
 * 3. 可以在进程内自由拷贝和移动
 * 4. 析构时自动减少引用计数
 * 5. 引用计数归零时自动回收 Buffer
 */
class BufferPtr {
public:
    /**
     * @brief 默认构造函数（空 Buffer）
     */
    BufferPtr() noexcept
        : buffer_id_(INVALID_BUFFER_ID)
        , allocator_(nullptr)
        , data_(nullptr)
    {}
    
    /**
     * @brief 构造函数
     * 
     * @param buffer_id Buffer ID
     * @param allocator Buffer 分配器
     */
    BufferPtr(BufferId buffer_id, SharedBufferAllocator* allocator)
        : buffer_id_(buffer_id)
        , allocator_(allocator)
        , data_(nullptr)
    {
        if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
            // 增加引用计数
            allocator_->add_ref(buffer_id_);
            
            // 获取数据指针（进程本地）
            data_ = allocator_->get_buffer_data(buffer_id_);
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~BufferPtr() {
        release();
    }
    
    /**
     * @brief 拷贝构造函数（增加引用计数）
     */
    BufferPtr(const BufferPtr& other) noexcept
        : buffer_id_(other.buffer_id_)
        , allocator_(other.allocator_)
        , data_(other.data_)
    {
        if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
            allocator_->add_ref(buffer_id_);
        }
    }
    
    /**
     * @brief 拷贝赋值运算符
     */
    BufferPtr& operator=(const BufferPtr& other) {
        if (this != &other) {
            release();
            
            buffer_id_ = other.buffer_id_;
            allocator_ = other.allocator_;
            data_ = other.data_;
            
            if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
                allocator_->add_ref(buffer_id_);
            }
        }
        return *this;
    }
    
    /**
     * @brief 移动构造函数（不改变引用计数）
     */
    BufferPtr(BufferPtr&& other) noexcept
        : buffer_id_(other.buffer_id_)
        , allocator_(other.allocator_)
        , data_(other.data_)
    {
        other.buffer_id_ = INVALID_BUFFER_ID;
        other.allocator_ = nullptr;
        other.data_ = nullptr;
    }
    
    /**
     * @brief 移动赋值运算符
     */
    BufferPtr& operator=(BufferPtr&& other) noexcept {
        if (this != &other) {
            release();
            
            buffer_id_ = other.buffer_id_;
            allocator_ = other.allocator_;
            data_ = other.data_;
            
            other.buffer_id_ = INVALID_BUFFER_ID;
            other.allocator_ = nullptr;
            other.data_ = nullptr;
        }
        return *this;
    }
    
    // ===== 数据访问 =====
    
    /**
     * @brief 获取数据指针
     */
    void* data() noexcept { 
        return data_; 
    }
    
    /**
     * @brief 获取数据指针（const 版本）
     */
    const void* data() const noexcept { 
        return data_; 
    }
    
    /**
     * @brief 类型转换获取数据指针
     */
    template<typename T>
    T* as() noexcept { 
        return static_cast<T*>(data_); 
    }
    
    /**
     * @brief 类型转换获取数据指针（const 版本）
     */
    template<typename T>
    const T* as() const noexcept { 
        return static_cast<const T*>(data_); 
    }
    
    /**
     * @brief 获取 Buffer 大小
     */
    size_t size() const noexcept {
        if (buffer_id_ == INVALID_BUFFER_ID || !allocator_) {
            return 0;
        }
        return allocator_->get_buffer_size(buffer_id_);
    }
    
    /**
     * @brief 获取 Buffer ID
     */
    BufferId id() const noexcept { 
        return buffer_id_; 
    }
    
    /**
     * @brief 是否有效
     */
    bool valid() const noexcept { 
        return buffer_id_ != INVALID_BUFFER_ID && data_ != nullptr; 
    }
    
    /**
     * @brief bool 转换运算符
     */
    explicit operator bool() const noexcept {
        return valid();
    }
    
    /**
     * @brief 获取引用计数
     */
    uint32_t ref_count() const noexcept {
        if (buffer_id_ == INVALID_BUFFER_ID || !allocator_) {
            return 0;
        }
        return allocator_->get_ref_count(buffer_id_);
    }
    
    // ===== 时间戳访问 =====
    
    /**
     * @brief 获取时间戳
     */
    Timestamp timestamp() const noexcept {
        if (buffer_id_ == INVALID_BUFFER_ID || !allocator_) {
            return Timestamp();
        }
        return allocator_->get_timestamp(buffer_id_);
    }
    
    /**
     * @brief 设置时间戳
     */
    void set_timestamp(const Timestamp& ts) {
        if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
            allocator_->set_timestamp(buffer_id_, ts);
        }
    }
    
    /**
     * @brief 设置时间范围
     */
    void set_time_range(const TimeRange& range) {
        if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
            allocator_->set_time_range(buffer_id_, range);
        }
    }
    
    /**
     * @brief 重置（释放当前 Buffer）
     */
    void reset() noexcept {
        release();
    }
    
    /**
     * @brief 交换两个 BufferPtr
     */
    void swap(BufferPtr& other) noexcept {
        std::swap(buffer_id_, other.buffer_id_);
        std::swap(allocator_, other.allocator_);
        std::swap(data_, other.data_);
    }
    
    // ===== 比较运算符 =====
    
    bool operator==(const BufferPtr& other) const noexcept {
        return buffer_id_ == other.buffer_id_;
    }
    
    bool operator!=(const BufferPtr& other) const noexcept {
        return buffer_id_ != other.buffer_id_;
    }
    
    bool operator<(const BufferPtr& other) const noexcept {
        return buffer_id_ < other.buffer_id_;
    }
    
private:
    /**
     * @brief 释放 Buffer（减少引用计数，可能触发回收）
     */
    void release() noexcept {
        if (buffer_id_ != INVALID_BUFFER_ID && allocator_) {
            bool should_free = allocator_->remove_ref(buffer_id_);
            
            if (should_free) {
                // 引用计数归零，回收 Buffer
                allocator_->deallocate(buffer_id_);
            }
        }
        
        buffer_id_ = INVALID_BUFFER_ID;
        allocator_ = nullptr;
        data_ = nullptr;
    }
    
    BufferId buffer_id_;                   ///< Buffer ID（共享的）
    SharedBufferAllocator* allocator_;     ///< 分配器（进程本地）
    void* data_;                           ///< 数据指针（进程本地）
};

/**
 * @brief 交换两个 BufferPtr（非成员函数）
 */
inline void swap(BufferPtr& a, BufferPtr& b) noexcept {
    a.swap(b);
}

}  // namespace multiqueue

