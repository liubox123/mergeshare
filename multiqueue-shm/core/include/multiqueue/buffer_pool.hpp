/**
 * @file buffer_pool.hpp
 * @brief Buffer Pool 实现
 * 
 * Buffer Pool 管理固定大小的内存块，存储在共享内存中
 */

#pragma once

#include "types.hpp"
#include <atomic>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cstring>

namespace multiqueue {

using namespace boost::interprocess;

/**
 * @brief Buffer Pool 头部（存储在共享内存）
 */
struct BufferPoolHeader {
    uint32_t magic_number;               ///< 魔数
    PoolId pool_id;                      ///< Pool ID
    size_t block_size;                   ///< 每个块的大小
    size_t block_count;                  ///< 块数量
    size_t header_size;                  ///< 头部大小
    size_t data_offset;                  ///< 数据区偏移
    std::atomic<bool> initialized;       ///< 是否已初始化
    
    // 空闲链表
    interprocess_mutex pool_mutex;       ///< 池锁
    std::atomic<uint32_t> free_count;    ///< 空闲块数量
    std::atomic<int32_t> free_head;      ///< 空闲链表头（-1 表示空）
    
    BufferPoolHeader() noexcept
        : magic_number(SHM_MAGIC_NUMBER)
        , pool_id(INVALID_POOL_ID)
        , block_size(0)
        , block_count(0)
        , header_size(0)
        , data_offset(0)
        , initialized(false)
        , pool_mutex()
        , free_count(0)
        , free_head(-1)
    {}
};

/**
 * @brief Buffer Pool（共享内存中的内存池）
 * 
 * 内存布局：
 * [BufferPoolHeader][FreeList: int32_t * block_count][Data: block_size * block_count]
 */
class BufferPool {
public:
    /**
     * @brief 构造函数（进程本地对象）
     */
    BufferPool()
        : header_(nullptr)
        , free_list_(nullptr)
        , data_base_(nullptr)
        , shm_()
        , region_()
    {}
    
    /**
     * @brief 析构函数
     */
    ~BufferPool() {
        // 不删除共享内存，只解除映射
    }
    
    /**
     * @brief 创建 Buffer Pool（第一个进程调用）
     * 
     * @param name 共享内存名称
     * @param pool_id Pool ID
     * @param block_size 每个块的大小
     * @param block_count 块数量
     * @return true 成功，false 失败
     */
    bool create(const char* name, PoolId pool_id, size_t block_size, size_t block_count) {
        try {
            // 计算总大小
            size_t header_size = sizeof(BufferPoolHeader);
            size_t free_list_size = sizeof(int32_t) * block_count;
            size_t data_size = block_size * block_count;
            size_t total_size = header_size + free_list_size + data_size;
            
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
            header_ = reinterpret_cast<BufferPoolHeader*>(base);
            free_list_ = reinterpret_cast<int32_t*>(base + header_size);
            data_base_ = base + header_size + free_list_size;
            
            // 初始化头部（不使用 placement new，直接设置字段）
            header_->magic_number = SHM_MAGIC_NUMBER;
            header_->pool_id = pool_id;
            header_->block_size = block_size;
            header_->block_count = block_count;
            header_->header_size = header_size;
            header_->data_offset = header_size + free_list_size;
            header_->initialized.store(false, std::memory_order_relaxed);
            
            // 初始化锁（使用 placement new）
            new (&header_->pool_mutex) interprocess_mutex();
            
            // 初始化空闲链表
            for (size_t i = 0; i < block_count; ++i) {
                free_list_[i] = (i + 1 < block_count) ? static_cast<int32_t>(i + 1) : -1;
            }
            header_->free_head.store(0, std::memory_order_relaxed);
            header_->free_count.store(static_cast<uint32_t>(block_count), std::memory_order_relaxed);
            
            // 标记为已初始化
            header_->initialized.store(true, std::memory_order_release);
            
            return true;
            
        } catch (const std::exception& e) {
            // 创建失败
            return false;
        }
    }
    
    /**
     * @brief 打开已存在的 Buffer Pool（后续进程调用）
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
            header_ = reinterpret_cast<BufferPoolHeader*>(base);
            
            // 验证魔数
            if (header_->magic_number != SHM_MAGIC_NUMBER) {
                return false;
            }
            
            // 等待初始化完成
            while (!header_->initialized.load(std::memory_order_acquire)) {
                // 自旋等待
            }
            
            // 计算指针
            free_list_ = reinterpret_cast<int32_t*>(base + header_->header_size);
            data_base_ = base + header_->data_offset;
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    /**
     * @brief 分配一个块
     * 
     * @return 块索引，-1 表示失败
     */
    int32_t allocate_block() {
        if (!header_) {
            return -1;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->pool_mutex);
        
        // 从空闲链表获取
        int32_t block_index = header_->free_head.load(std::memory_order_acquire);
        if (block_index < 0) {
            return -1;  // 无可用块
        }
        
        // 更新链表头
        header_->free_head.store(free_list_[block_index], std::memory_order_release);
        header_->free_count.fetch_sub(1, std::memory_order_relaxed);
        
        return block_index;
    }
    
    /**
     * @brief 释放一个块
     * 
     * @param block_index 块索引
     */
    void free_block(int32_t block_index) {
        if (!header_ || block_index < 0 || 
            block_index >= static_cast<int32_t>(header_->block_count)) {
            return;
        }
        
        scoped_lock<interprocess_mutex> lock(header_->pool_mutex);
        
        // 加入空闲链表
        int32_t old_head = header_->free_head.load(std::memory_order_acquire);
        free_list_[block_index] = old_head;
        header_->free_head.store(block_index, std::memory_order_release);
        header_->free_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 获取块的数据指针（进程本地）
     * 
     * @param block_index 块索引
     * @return 数据指针，nullptr 表示失败
     */
    void* get_block_data(int32_t block_index) {
        if (!header_ || !data_base_ || block_index < 0 ||
            block_index >= static_cast<int32_t>(header_->block_count)) {
            return nullptr;
        }
        
        return data_base_ + (block_index * header_->block_size);
    }
    
    /**
     * @brief 获取块的共享内存偏移量
     * 
     * @param block_index 块索引
     * @return 相对于 Pool 共享内存基地址的偏移
     */
    uint64_t get_block_offset(int32_t block_index) const {
        if (!header_ || block_index < 0 ||
            block_index >= static_cast<int32_t>(header_->block_count)) {
            return 0;
        }
        
        return header_->data_offset + (block_index * header_->block_size);
    }
    
    /**
     * @brief 获取空闲块数量
     */
    uint32_t get_free_count() const {
        if (!header_) {
            return 0;
        }
        return header_->free_count.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取总块数量
     */
    size_t get_block_count() const {
        if (!header_) {
            return 0;
        }
        return header_->block_count;
    }
    
    /**
     * @brief 获取块大小
     */
    size_t get_block_size() const {
        if (!header_) {
            return 0;
        }
        return header_->block_size;
    }
    
    /**
     * @brief 获取 Pool ID
     */
    PoolId get_pool_id() const {
        if (!header_) {
            return INVALID_POOL_ID;
        }
        return header_->pool_id;
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
     * @brief 获取共享内存基地址（进程本地）
     */
    void* get_base_address() const {
        if (!region_.get_address()) {
            return nullptr;
        }
        return region_.get_address();
    }
    
    /**
     * @brief 获取头部指针（用于统计和调试）
     */
    const BufferPoolHeader* header() const {
        return header_;
    }
    
private:
    BufferPoolHeader* header_;     ///< 头部指针（进程本地）
    int32_t* free_list_;           ///< 空闲链表（进程本地）
    char* data_base_;              ///< 数据区基地址（进程本地）
    
    shared_memory_object shm_;     ///< 共享内存对象（进程本地）
    mapped_region region_;         ///< 映射区域（进程本地）
};

}  // namespace multiqueue

