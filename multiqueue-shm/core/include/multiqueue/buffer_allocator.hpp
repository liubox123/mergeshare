/**
 * @file buffer_allocator.hpp
 * @brief 共享 Buffer 分配器
 * 
 * SharedBufferAllocator 负责从 Buffer Pool 分配和释放 Buffer，
 * 并维护 BufferMetadata
 */

#pragma once

#include "types.hpp"
#include "timestamp.hpp"
#include "buffer_metadata.hpp"
#include "buffer_pool.hpp"
#include "global_registry.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace multiqueue {

/**
 * @brief 共享 Buffer 分配器（进程本地对象）
 * 
 * 负责：
 * 1. 从 Buffer Pool 分配/释放内存块
 * 2. 在 BufferMetadataTable 中创建/删除 Buffer 元数据
 * 3. 维护进程本地的 Pool 映射
 */
class SharedBufferAllocator {
public:
    /**
     * @brief 构造函数
     * 
     * @param registry 全局注册表指针（共享内存中）
     * @param process_id 当前进程 ID
     */
    SharedBufferAllocator(GlobalRegistry* registry, ProcessId process_id)
        : registry_(registry)
        , process_id_(process_id)
        , pools_()
    {
        if (!registry_) {
            throw std::invalid_argument("GlobalRegistry pointer cannot be null");
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~SharedBufferAllocator() {
        // 不删除共享内存，只清理进程本地对象
        pools_.clear();
    }
    
    /**
     * @brief 注册一个 Buffer Pool
     * 
     * @param pool_id Pool ID
     * @param shm_name 共享内存名称
     * @return true 成功，false 失败
     */
    bool register_pool(PoolId pool_id, const char* shm_name) {
        if (pools_.find(pool_id) != pools_.end()) {
            // 已存在
            return true;
        }
        
        auto pool = std::make_unique<BufferPool>();
        if (!pool->open(shm_name)) {
            return false;
        }
        
        // 保存映射信息
        PoolMapping mapping;
        mapping.base_addr = pool->get_base_address();
        mapping.size = pool->get_block_count() * pool->get_block_size();
        mapping.pool = std::move(pool);
        
        pools_[pool_id] = std::move(mapping);
        
        return true;
    }
    
    /**
     * @brief 分配 Buffer
     * 
     * @param size 所需大小（字节）
     * @return Buffer ID，0 表示失败
     */
    BufferId allocate(size_t size) {
        // 1. 选择合适的池
        PoolId pool_id = select_pool(size);
        if (pool_id == INVALID_POOL_ID) {
            return INVALID_BUFFER_ID;
        }
        
        // 2. 确保池已注册
        if (pools_.find(pool_id) == pools_.end()) {
            // 动态注册池
            if (!auto_register_pool(pool_id)) {
                return INVALID_BUFFER_ID;
            }
        }
        
        BufferPool* pool = pools_[pool_id].pool.get();
        
        // 3. 从池中分配一个块
        int32_t block_index = pool->allocate_block();
        if (block_index < 0) {
            return INVALID_BUFFER_ID;
        }
        
        // 4. 在 BufferMetadata 表中分配槽位
        int32_t meta_slot = registry_->buffer_metadata_table.allocate_slot();
        if (meta_slot < 0) {
            // 回收池中的块
            pool->free_block(block_index);
            return INVALID_BUFFER_ID;
        }
        
        // 5. 初始化 BufferMetadata
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        meta.pool_id = pool_id;
        meta.block_index = static_cast<uint32_t>(block_index);
        meta.size = size;
        meta.ref_count.store(1, std::memory_order_release);  // 初始引用计数为 1
        meta.data_shm_offset = pool->get_block_offset(block_index);
        meta.creator_process = process_id_;
        meta.alloc_time_ns = Timestamp::now().to_nanoseconds();
        meta.has_time_range = false;
        meta.set_valid(true);
        
        return meta.buffer_id;
    }
    
    /**
     * @brief 释放 Buffer（引用计数归零时调用）
     * 
     * @param buffer_id Buffer ID
     */
    void deallocate(BufferId buffer_id) {
        // 1. 查找 BufferMetadata
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return;
        }
        
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        
        // 2. 检查引用计数（双重检查）
        uint32_t ref = meta.ref_count.load(std::memory_order_acquire);
        if (ref > 0) {
            // 引用计数不为 0，不应释放
            return;
        }
        
        // 3. 标记为无效
        meta.set_valid(false);
        
        // 4. 获取池信息
        PoolId pool_id = meta.pool_id;
        int32_t block_index = static_cast<int32_t>(meta.block_index);
        
        // 5. 回收池中的块
        if (pools_.find(pool_id) != pools_.end()) {
            pools_[pool_id].pool->free_block(block_index);
        }
        
        // 6. 释放 BufferMetadata 槽位
        registry_->buffer_metadata_table.free_slot(meta_slot);
    }
    
    /**
     * @brief 获取 Buffer 数据指针（进程本地）
     * 
     * @param buffer_id Buffer ID
     * @return 数据指针，nullptr 表示失败
     */
    void* get_buffer_data(BufferId buffer_id) {
        // 1. 查找 BufferMetadata
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return nullptr;
        }
        
        const BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        
        if (!meta.is_valid()) {
            return nullptr;
        }
        
        // 2. 获取池
        PoolId pool_id = meta.pool_id;
        if (pools_.find(pool_id) == pools_.end()) {
            // 动态注册池
            if (!auto_register_pool(pool_id)) {
                return nullptr;
            }
        }
        
        BufferPool* pool = pools_[pool_id].pool.get();
        
        // 3. 获取数据指针
        return pool->get_block_data(static_cast<int32_t>(meta.block_index));
    }
    
    /**
     * @brief 增加 Buffer 引用计数
     * 
     * @param buffer_id Buffer ID
     * @return true 成功，false 失败
     */
    bool add_ref(BufferId buffer_id) {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return false;
        }
        
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        meta.add_ref();
        
        return true;
    }
    
    /**
     * @brief 减少 Buffer 引用计数
     * 
     * @param buffer_id Buffer ID
     * @return 如果引用计数归零，返回 true
     */
    bool remove_ref(BufferId buffer_id) {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return false;
        }
        
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        uint32_t new_ref = meta.remove_ref();
        
        return (new_ref == 0);
    }
    
    /**
     * @brief 获取 Buffer 大小
     */
    size_t get_buffer_size(BufferId buffer_id) const {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return 0;
        }
        
        return registry_->buffer_metadata_table.entries[meta_slot].size;
    }
    
    /**
     * @brief 获取 Buffer 引用计数
     */
    uint32_t get_ref_count(BufferId buffer_id) const {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return 0;
        }
        
        return registry_->buffer_metadata_table.entries[meta_slot].get_ref_count();
    }
    
    /**
     * @brief 设置 Buffer 时间戳
     */
    void set_timestamp(BufferId buffer_id, const Timestamp& ts) {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return;
        }
        
        registry_->buffer_metadata_table.entries[meta_slot].timestamp = ts;
    }
    
    /**
     * @brief 获取 Buffer 时间戳
     */
    Timestamp get_timestamp(BufferId buffer_id) const {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return Timestamp();
        }
        
        return registry_->buffer_metadata_table.entries[meta_slot].timestamp;
    }
    
    /**
     * @brief 设置 Buffer 时间范围
     */
    void set_time_range(BufferId buffer_id, const TimeRange& range) {
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return;
        }
        
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        meta.time_range = range;
        meta.has_time_range = true;
    }
    
private:
    /**
     * @brief 进程本地的池映射信息
     */
    struct PoolMapping {
        void* base_addr;                      ///< 进程本地基地址
        size_t size;                          ///< 大小
        std::unique_ptr<BufferPool> pool;     ///< Buffer Pool 对象
    };
    
    /**
     * @brief 选择合适的池
     * 
     * 根据请求的大小选择最合适的池
     */
    PoolId select_pool(size_t size) const {
        // 遍历所有池，找到第一个 block_size >= size 的池
        for (uint32_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
            const auto& pool_info = registry_->buffer_pool_registry.pools[i];
            if (pool_info.active && pool_info.block_size >= size) {
                return pool_info.pool_id;
            }
        }
        
        return INVALID_POOL_ID;
    }
    
    /**
     * @brief 自动注册池（延迟注册）
     */
    bool auto_register_pool(PoolId pool_id) {
        if (pool_id >= MAX_BUFFER_POOLS) {
            return false;
        }
        
        const auto& pool_info = registry_->buffer_pool_registry.pools[pool_id];
        if (!pool_info.active) {
            return false;
        }
        
        return register_pool(pool_id, pool_info.shm_name);
    }
    
    GlobalRegistry* registry_;                          ///< 全局注册表（共享内存）
    ProcessId process_id_;                              ///< 当前进程 ID
    std::unordered_map<PoolId, PoolMapping> pools_;    ///< 池映射（进程本地）
};

}  // namespace multiqueue

