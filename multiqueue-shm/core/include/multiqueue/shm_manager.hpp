/**
 * @file shm_manager.hpp
 * @brief 共享内存管理器
 * 
 * 统一管理 BufferPool、Buffer 分配、引用计数和统计信息
 */

#pragma once

#include "types.hpp"
#include "buffer_pool.hpp"
#include "buffer_allocator.hpp"
#include "buffer_ptr.hpp"
#include "global_registry.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <iostream>

namespace multiqueue {

/**
 * @brief BufferPool 配置
 */
struct PoolConfig {
    std::string name;           ///< 池名称
    size_t block_size;          ///< 单个 Buffer 大小（字节）
    size_t block_count;         ///< Buffer 数量
    bool expandable;            ///< 是否可扩展（暂未实现）
    size_t max_blocks;          ///< 最大 Buffer 数量（可扩展时，暂未实现）
    
    PoolConfig()
        : name("default")
        , block_size(4096)
        , block_count(256)
        , expandable(false)
        , max_blocks(0)
    {}
    
    PoolConfig(const std::string& name_, size_t block_size_, size_t block_count_)
        : name(name_)
        , block_size(block_size_)
        , block_count(block_count_)
        , expandable(false)
        , max_blocks(0)
    {}
};

/**
 * @brief 共享内存管理器配置
 */
struct ShmConfig {
    std::string name_prefix;    ///< 共享内存名称前缀
    std::vector<PoolConfig> pools;  ///< 池配置列表
    
    ShmConfig()
        : name_prefix("mqshm_")
    {}
    
    /**
     * @brief 创建默认配置
     * 
     * 预定义三个池：
     * - small: 4KB × 1024
     * - medium: 64KB × 512
     * - large: 1MB × 128
     */
    static ShmConfig default_config() {
        ShmConfig config;
        config.pools = {
            PoolConfig("small",  4096,         1024),  // 4KB
            PoolConfig("medium", 65536,        512),   // 64KB
            PoolConfig("large",  1048576,      128)    // 1MB
        };
        return config;
    }
};

/**
 * @brief 池统计信息
 */
struct PoolStats {
    std::string name;           ///< 池名称
    PoolId pool_id;             ///< 池 ID
    size_t block_size;          ///< Block 大小
    size_t block_count;         ///< Block 总数
    size_t blocks_used;         ///< 已使用 Block 数
    size_t blocks_free;         ///< 空闲 Block 数
    double utilization;         ///< 利用率（0.0 ~ 1.0）
    
    PoolStats()
        : name("unknown")
        , pool_id(INVALID_POOL_ID)
        , block_size(0)
        , block_count(0)
        , blocks_used(0)
        , blocks_free(0)
        , utilization(0.0)
    {}
};

/**
 * @brief ShmManager 统计信息
 */
struct ShmStats {
    size_t total_pools;         ///< 总池数
    size_t total_capacity;      ///< 总容量（字节）
    size_t total_allocated;     ///< 已分配容量（字节）
    size_t total_free;          ///< 空闲容量（字节）
    uint64_t allocation_count;  ///< 分配次数
    uint64_t deallocation_count;///< 释放次数
    
    std::vector<PoolStats> pool_stats;  ///< 每个池的统计信息
    
    ShmStats()
        : total_pools(0)
        , total_capacity(0)
        , total_allocated(0)
        , total_free(0)
        , allocation_count(0)
        , deallocation_count(0)
    {}
};

/**
 * @brief 共享内存管理器
 * 
 * 负责：
 * 1. 管理多个 BufferPool
 * 2. 统一的 Buffer 分配接口（自动选择合适的池）
 * 3. 引用计数管理
 * 4. 统计信息收集
 */
class ShmManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param registry GlobalRegistry 指针
     * @param process_id 当前进程 ID
     * @param config 配置
     */
    explicit ShmManager(
        GlobalRegistry* registry,
        ProcessId process_id,
        const ShmConfig& config = ShmConfig::default_config()
    )
        : config_(config)
        , registry_(registry)
        , process_id_(process_id)
        , allocator_(nullptr)
        , pools_()
        , pool_name_to_id_()
        , pool_id_to_name_()
        , next_pool_id_(0)
        , allocation_count_(0)
        , deallocation_count_(0)
        , initialized_(false)
    {
    }
    
    /**
     * @brief 析构函数
     */
    ~ShmManager() {
        shutdown();
    }
    
    // 禁用拷贝和移动
    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;
    ShmManager(ShmManager&&) = delete;
    ShmManager& operator=(ShmManager&&) = delete;
    
    /**
     * @brief 初始化
     * 
     * 创建所有配置的 BufferPool 并注册
     * 
     * @return true 成功，false 失败
     */
    bool initialize() {
        if (initialized_) {
            return false;  // 已初始化
        }
        
        // 创建 SharedBufferAllocator
        allocator_ = std::make_unique<SharedBufferAllocator>(registry_, process_id_);
        
        // 创建所有配置的 Pool
        for (const auto& pool_config : config_.pools) {
            if (!add_pool(pool_config)) {
                // 清理已创建的池
                shutdown();
                return false;
            }
        }
        
        initialized_ = true;
        return true;
    }
    
    /**
     * @brief 关闭
     * 
     * 清理所有资源
     */
    void shutdown() {
        if (!initialized_) {
            return;
        }
        
        // 清理映射
        pool_name_to_id_.clear();
        pool_id_to_name_.clear();
        pools_.clear();
        
        // 清理 allocator
        allocator_.reset();
        
        initialized_ = false;
    }
    
    /**
     * @brief 是否已初始化
     */
    bool is_initialized() const {
        return initialized_;
    }
    
    // ===== Buffer 分配 =====
    
    /**
     * @brief 分配指定大小的 Buffer
     * 
     * 自动选择合适的 Pool 进行分配
     * 
     * @param size 所需大小（字节）
     * @return BufferPtr 智能指针，失败时返回无效指针
     */
    BufferPtr allocate(size_t size) {
        if (!initialized_ || !allocator_) {
            return BufferPtr();  // 无效指针
        }
        
        // 选择合适的池
        PoolId pool_id = select_pool_for_size(size);
        if (pool_id == INVALID_POOL_ID) {
            return BufferPtr();  // 没有合适的池
        }
        
        // 分配 Buffer（allocate 返回 BufferId）
        BufferId buffer_id = allocator_->allocate(size);
        
        if (buffer_id != INVALID_BUFFER_ID) {
            allocation_count_.fetch_add(1, std::memory_order_relaxed);
            return BufferPtr(buffer_id, allocator_.get());
        }
        
        return BufferPtr();
    }
    
    /**
     * @brief 从指定池分配 Buffer
     * 
     * @param pool_name 池名称
     * @return BufferPtr 智能指针，失败时返回无效指针
     */
    BufferPtr allocate_from_pool(const std::string& pool_name) {
        if (!initialized_ || !allocator_) {
            return BufferPtr();
        }
        
        // 查找池 ID
        auto pool_it = pool_name_to_id_.find(pool_name);
        if (pool_it == pool_name_to_id_.end()) {
            return BufferPtr();  // 池不存在
        }
        PoolId pool_id = pool_it->second;
        
        // 获取池指针
        auto pool_ptr = get_pool(pool_name);
        if (!pool_ptr || !pool_ptr->header()) {
            return BufferPtr();
        }
        
        // 从池中分配一个块
        int32_t block_index = pool_ptr->allocate_block();
        if (block_index < 0) {
            return BufferPtr();
        }
        
        // 在 BufferMetadata 表中分配槽位
        int32_t meta_slot = registry_->buffer_metadata_table.allocate_slot();
        if (meta_slot < 0) {
            // 回收池中的块
            pool_ptr->free_block(block_index);
            return BufferPtr();
        }
        
        // 初始化 BufferMetadata
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        meta.pool_id = pool_id;
        meta.block_index = static_cast<uint32_t>(block_index);
        meta.size = pool_ptr->header()->block_size;  // 使用池的 block_size
        meta.ref_count.store(1, std::memory_order_release);  // 初始引用计数为 1
        meta.data_shm_offset = pool_ptr->get_block_offset(block_index);
        meta.creator_process = process_id_;
        meta.alloc_time_ns = Timestamp::now().to_nanoseconds();
        meta.has_time_range = false;
        meta.set_valid(true);
        
        allocation_count_.fetch_add(1, std::memory_order_relaxed);
        
        return BufferPtr(meta.buffer_id, allocator_.get());
    }
    
    // ===== 池管理 =====
    
    /**
     * @brief 添加新的内存池
     * 
     * @param config 池配置
     * @return true 成功，false 失败
     */
    bool add_pool(const PoolConfig& config) {
        if (!registry_) {
            return false;
        }
        
        // 检查名称是否已存在
        if (pool_name_to_id_.find(config.name) != pool_name_to_id_.end()) {
            return false;  // 名称已存在
        }
        
        // 创建 BufferPool（共享内存）
        auto pool = std::make_shared<BufferPool>();
        std::string shm_name = config_.name_prefix + config.name;
        
        // 先在 GlobalRegistry 中注册（获取 pool_id）
        PoolId pool_id = registry_->buffer_pool_registry.register_pool(
            config.block_size,
            config.block_count,
            shm_name.c_str()
        );
        if (pool_id == INVALID_POOL_ID) {
            return false;
        }
        
        // 使用 GlobalRegistry 返回的 pool_id 创建 BufferPool
        if (!pool->create(shm_name.c_str(), pool_id, config.block_size, config.block_count)) {
            return false;
        }
        
        // 在 Allocator 中注册（使用 GlobalRegistry 的 pool_id）
        // 注意：allocator_ 应该在 initialize() 中已经创建
        if (!allocator_) {
            return false;  // allocator_ 必须存在
        }
        if (!allocator_->register_pool(pool_id, shm_name.c_str())) {
            return false;  // 注册失败
        }
        
        // 添加到映射
        pools_[pool_id] = pool;
        pool_name_to_id_[config.name] = pool_id;
        pool_id_to_name_[pool_id] = config.name;
        
        return true;
    }
    
    /**
     * @brief 移除内存池
     * 
     * @param name 池名称
     */
    void remove_pool(const std::string& name) {
        auto it = pool_name_to_id_.find(name);
        if (it == pool_name_to_id_.end()) {
            return;  // 不存在
        }
        
        PoolId pool_id = it->second;
        
        // 从映射中移除
        pool_name_to_id_.erase(name);
        pool_id_to_name_.erase(pool_id);
        pools_.erase(pool_id);
        
        // 注意：这里不清理共享内存，因为可能有其他进程在使用
    }
    
    /**
     * @brief 获取池指针
     * 
     * @param name 池名称
     * @return BufferPool 指针，不存在时返回 nullptr
     */
    BufferPool* get_pool(const std::string& name) {
        auto it = pool_name_to_id_.find(name);
        if (it == pool_name_to_id_.end()) {
            return nullptr;
        }
        
        PoolId pool_id = it->second;
        auto pool_it = pools_.find(pool_id);
        if (pool_it == pools_.end()) {
            return nullptr;
        }
        
        return pool_it->second.get();
    }
    
    /**
     * @brief 列出所有池名称
     */
    std::vector<std::string> list_pools() const {
        std::vector<std::string> names;
        for (const auto& pair : pool_name_to_id_) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    // ===== 统计信息 =====
    
    /**
     * @brief 获取统计信息
     */
    ShmStats get_stats() const {
        ShmStats stats;
        
        stats.total_pools = pools_.size();
        stats.allocation_count = allocation_count_.load(std::memory_order_relaxed);
        stats.deallocation_count = deallocation_count_.load(std::memory_order_relaxed);
        
        // 收集每个池的统计信息
        for (const auto& pair : pools_) {
            PoolId pool_id = pair.first;
            BufferPool* pool = pair.second.get();
            
            if (!pool) {
                continue;
            }
            
            PoolStats pool_stat;
            
            // 获取池名称
            auto name_it = pool_id_to_name_.find(pool_id);
            if (name_it != pool_id_to_name_.end()) {
                pool_stat.name = name_it->second;
            } else {
                pool_stat.name = "pool_" + std::to_string(pool_id);
            }
            
            pool_stat.pool_id = pool_id;
            
            // 访问 BufferPool 的 header
            if (pool->header()) {
                pool_stat.block_size = pool->header()->block_size;
                pool_stat.block_count = pool->header()->block_count;
                pool_stat.blocks_free = pool->header()->free_count.load(std::memory_order_relaxed);
                pool_stat.blocks_used = pool_stat.block_count - pool_stat.blocks_free;
            } else {
                pool_stat.block_size = 0;
                pool_stat.block_count = 0;
                pool_stat.blocks_free = 0;
                pool_stat.blocks_used = 0;
            }
            
            if (pool_stat.block_count > 0) {
                pool_stat.utilization = static_cast<double>(pool_stat.blocks_used) / pool_stat.block_count;
            } else {
                pool_stat.utilization = 0.0;
            }
            
            // 累加总容量
            stats.total_capacity += pool_stat.block_size * pool_stat.block_count;
            stats.total_allocated += pool_stat.block_size * pool_stat.blocks_used;
            stats.total_free += pool_stat.block_size * pool_stat.blocks_free;
            
            stats.pool_stats.push_back(pool_stat);
        }
        
        return stats;
    }
    
    /**
     * @brief 打印统计信息到标准输出
     */
    void print_stats() const {
        ShmStats stats = get_stats();
        
        std::cout << "========== ShmManager 统计信息 ==========" << std::endl;
        std::cout << "总池数: " << stats.total_pools << std::endl;
        std::cout << "总容量: " << (stats.total_capacity / 1024.0 / 1024.0) << " MB" << std::endl;
        std::cout << "已分配: " << (stats.total_allocated / 1024.0 / 1024.0) << " MB" << std::endl;
        std::cout << "空闲: " << (stats.total_free / 1024.0 / 1024.0) << " MB" << std::endl;
        std::cout << "分配次数: " << stats.allocation_count << std::endl;
        std::cout << "释放次数: " << stats.deallocation_count << std::endl;
        std::cout << std::endl;
        
        std::cout << "各池详情:" << std::endl;
        for (const auto& pool_stat : stats.pool_stats) {
            std::cout << "  [" << pool_stat.name << "]" << std::endl;
            std::cout << "    Pool ID: " << pool_stat.pool_id << std::endl;
            std::cout << "    Block 大小: " << pool_stat.block_size << " bytes" << std::endl;
            std::cout << "    Block 总数: " << pool_stat.block_count << std::endl;
            std::cout << "    已使用: " << pool_stat.blocks_used << std::endl;
            std::cout << "    空闲: " << pool_stat.blocks_free << std::endl;
            std::cout << "    利用率: " << (pool_stat.utilization * 100.0) << "%" << std::endl;
            std::cout << std::endl;
        }
        
        std::cout << "========================================" << std::endl;
    }
    
    // ===== 访问器 =====
    
    /**
     * @brief 获取底层的 SharedBufferAllocator
     */
    SharedBufferAllocator* allocator() {
        return allocator_.get();
    }
    
    /**
     * @brief 获取配置
     */
    const ShmConfig& config() const {
        return config_;
    }
    
private:
    /**
     * @brief 为指定大小选择合适的池
     * 
     * @param size 所需大小
     * @return PoolId，如果没有合适的池则返回 INVALID_POOL_ID
     */
    PoolId select_pool_for_size(size_t size) const {
        // 选择第一个 block_size >= size 的池
        PoolId best_pool = INVALID_POOL_ID;
        size_t best_block_size = SIZE_MAX;
        
        for (const auto& pair : pools_) {
            PoolId pool_id = pair.first;
            BufferPool* pool = pair.second.get();
            
            if (!pool) {
                continue;
            }
            
            if (!pool->header()) {
                continue;
            }
            
            size_t block_size = pool->header()->block_size;
            
            // 如果 block_size >= size 且比当前最佳更小
            if (block_size >= size && block_size < best_block_size) {
                best_pool = pool_id;
                best_block_size = block_size;
            }
        }
        
        return best_pool;
    }
    
    ShmConfig config_;                                      ///< 配置
    GlobalRegistry* registry_;                              ///< GlobalRegistry 指针
    ProcessId process_id_;                                  ///< 当前进程 ID
    
    std::unique_ptr<SharedBufferAllocator> allocator_;     ///< Buffer 分配器
    
    std::map<PoolId, std::shared_ptr<BufferPool>> pools_;  ///< 池映射（PoolId -> BufferPool）
    std::map<std::string, PoolId> pool_name_to_id_;        ///< 名称到 ID 的映射
    std::map<PoolId, std::string> pool_id_to_name_;        ///< ID 到名称的映射
    
    PoolId next_pool_id_;                                   ///< 下一个可用的 Pool ID
    
    std::atomic<uint64_t> allocation_count_;                ///< 分配计数
    std::atomic<uint64_t> deallocation_count_;              ///< 释放计数
    
    bool initialized_;                                      ///< 是否已初始化
};

}  // namespace multiqueue

