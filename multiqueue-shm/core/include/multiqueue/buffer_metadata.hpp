/**
 * @file buffer_metadata.hpp
 * @brief Buffer 元数据定义
 * 
 * Buffer 元数据存储在共享内存中，支持跨进程引用计数
 */

#pragma once

#include "types.hpp"
#include "timestamp.hpp"
#include <atomic>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace multiqueue {

using namespace boost::interprocess;

/**
 * @brief Buffer 元数据（存储在共享内存）
 * 
 * 注意：
 * 1. 此结构体的所有成员都必须是 POD 类型或支持跨进程的类型
 * 2. 使用 alignas(CACHE_LINE_SIZE) 避免 false sharing
 * 3. 引用计数使用 std::atomic，必须是 lock-free 的
 */
struct alignas(CACHE_LINE_SIZE) BufferMetadata {
    // ===== 标识信息 =====
    BufferId buffer_id;          ///< 全局唯一 ID
    
    // ===== 内存位置信息 =====
    PoolId pool_id;              ///< 所属池 ID
    uint32_t block_index;        ///< 在池中的块索引
    size_t size;                 ///< 实际数据大小（字节）
    
    // ===== 引用计数（跨进程原子操作）=====
    std::atomic<uint32_t> ref_count;
    
    // ===== 数据位置 =====
    // 不存储指针！只存储相对偏移量
    uint64_t data_shm_offset;    ///< 相对于池共享内存基地址的偏移
    
    // ===== 时间戳信息 =====
    Timestamp timestamp;         ///< 单点时间戳
    TimeRange time_range;        ///< 时间范围（可选）
    bool has_time_range;         ///< 是否有时间范围
    
    // ===== 状态 =====
    std::atomic<bool> valid;     ///< 是否有效
    
    // ===== 所有者信息（用于清理）=====
    ProcessId creator_process;   ///< 创建此 Buffer 的进程 ID
    TimestampNs alloc_time_ns;   ///< 分配时间（纳秒）
    
    /**
     * @brief 默认构造函数
     */
    BufferMetadata() noexcept
        : buffer_id(INVALID_BUFFER_ID)
        , pool_id(INVALID_POOL_ID)
        , block_index(0)
        , size(0)
        , ref_count(0)
        , data_shm_offset(0)
        , timestamp()
        , time_range()
        , has_time_range(false)
        , valid(false)
        , creator_process(INVALID_PROCESS_ID)
        , alloc_time_ns(0)
    {
    }
    
    /**
     * @brief 原子地增加引用计数
     * @return 增加后的引用计数
     */
    uint32_t add_ref() noexcept {
        return ref_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    
    /**
     * @brief 原子地减少引用计数
     * @return 减少后的引用计数
     */
    uint32_t remove_ref() noexcept {
        return ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    }
    
    /**
     * @brief 获取当前引用计数
     */
    uint32_t get_ref_count() const noexcept {
        return ref_count.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 检查是否有效
     */
    bool is_valid() const noexcept {
        return valid.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 设置为有效
     */
    void set_valid(bool v) noexcept {
        valid.store(v, std::memory_order_release);
    }
} MQSHM_PACKED;

// 静态断言：确保 std::atomic 是 lock-free 的
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "std::atomic<uint32_t> must be lock-free for cross-process usage");
static_assert(std::atomic<bool>::is_always_lock_free,
              "std::atomic<bool> must be lock-free for cross-process usage");

/**
 * @brief Buffer 元数据表（存储在共享内存）
 * 
 * 管理所有 Buffer 的元数据，支持多进程并发访问
 */
struct BufferMetadataTable {
    // ===== 同步原语（跨进程）=====
    interprocess_mutex table_mutex;  ///< 表级锁（用于分配/释放槽位）
    
    // ===== 计数器 =====
    std::atomic<uint32_t> allocated_count;  ///< 已分配数量
    std::atomic<uint64_t> next_buffer_id;   ///< 下一个 Buffer ID
    
    // ===== 元数据数组 =====
    BufferMetadata entries[MAX_BUFFERS];
    
    // ===== 空闲链表 =====
    std::atomic<int32_t> free_head;  ///< 空闲链表头（-1 表示空）
    int32_t next_free[MAX_BUFFERS];  ///< 下一个空闲槽位索引
    
    /**
     * @brief 初始化表（只由第一个进程调用）
     */
    void initialize() noexcept {
        // 初始化锁
        new (&table_mutex) interprocess_mutex();
        
        // 初始化计数器
        allocated_count.store(0, std::memory_order_relaxed);
        next_buffer_id.store(1, std::memory_order_relaxed);
        
        // 初始化所有条目
        for (size_t i = 0; i < MAX_BUFFERS; ++i) {
            new (&entries[i]) BufferMetadata();
            entries[i].set_valid(false);
            entries[i].ref_count.store(0, std::memory_order_relaxed);
            next_free[i] = (i + 1 < MAX_BUFFERS) ? static_cast<int32_t>(i + 1) : -1;
        }
        
        // 初始化空闲链表
        free_head.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief 分配一个 BufferMetadata 槽位
     * @return 槽位索引，-1 表示失败
     */
    int32_t allocate_slot() noexcept {
        scoped_lock<interprocess_mutex> lock(table_mutex);
        
        // 从空闲链表获取
        int32_t slot = free_head.load(std::memory_order_acquire);
        if (slot < 0) {
            return -1;  // 无可用槽位
        }
        
        // 更新链表头
        free_head.store(next_free[slot], std::memory_order_release);
        
        // 分配 Buffer ID
        uint64_t buffer_id = next_buffer_id.fetch_add(1, std::memory_order_acq_rel);
        
        // 初始化槽位
        BufferMetadata& meta = entries[slot];
        meta.buffer_id = buffer_id;
        meta.ref_count.store(0, std::memory_order_relaxed);
        meta.set_valid(false);  // 稍后设为 true
        
        allocated_count.fetch_add(1, std::memory_order_relaxed);
        
        return slot;
    }
    
    /**
     * @brief 释放一个 BufferMetadata 槽位
     * @param slot 槽位索引
     */
    void free_slot(int32_t slot) noexcept {
        if (slot < 0 || slot >= static_cast<int32_t>(MAX_BUFFERS)) {
            return;
        }
        
        scoped_lock<interprocess_mutex> lock(table_mutex);
        
        // 标记为无效
        entries[slot].set_valid(false);
        entries[slot].buffer_id = INVALID_BUFFER_ID;
        
        // 加入空闲链表
        int32_t old_head = free_head.load(std::memory_order_acquire);
        next_free[slot] = old_head;
        free_head.store(slot, std::memory_order_release);
        
        allocated_count.fetch_sub(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 根据 Buffer ID 查找槽位
     * @return 槽位索引，-1 表示未找到
     */
    int32_t find_slot_by_id(BufferId buffer_id) const noexcept {
        // 线性搜索（对于 4096 个条目，性能可接受）
        // 未来可以优化为哈希表
        for (size_t i = 0; i < MAX_BUFFERS; ++i) {
            if (entries[i].is_valid() && entries[i].buffer_id == buffer_id) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }
    
    /**
     * @brief 获取统计信息
     */
    uint32_t get_allocated_count() const noexcept {
        return allocated_count.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取下一个 Buffer ID（不分配）
     */
    uint64_t peek_next_buffer_id() const noexcept {
        return next_buffer_id.load(std::memory_order_acquire);
    }
};

}  // namespace multiqueue
