/**
 * @file runtime.hpp
 * @brief Runtime 核心管理器
 * 
 * Runtime 是框架的核心，管理所有 Block、连接和调度
 */

#pragma once

#include "types.hpp"
#include "global_registry.hpp"
#include "buffer_allocator.hpp"
#include "buffer_pool.hpp"
#include "port_queue.hpp"
#include "block.hpp"
#include "scheduler.hpp"
#include "msgbus.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace multiqueue {

/**
 * @brief Runtime 配置
 */
struct RuntimeConfig {
    std::string process_name;          ///< 进程名称
    size_t num_scheduler_threads;      ///< 调度器线程数量
    LogLevel log_level;                ///< 日志级别
    
    // Buffer Pool 配置
    struct PoolConfig {
        size_t block_size;
        size_t block_count;
    };
    std::vector<PoolConfig> pool_configs;  ///< Buffer Pool 配置列表
    
    RuntimeConfig()
        : process_name("MultiQueueSHM")
        , num_scheduler_threads(0)  // 自动检测
        , log_level(LogLevel::INFO)
    {
        // 默认池配置
        pool_configs.push_back({4096, 1024});    // 4KB, 1024 块
        pool_configs.push_back({65536, 512});    // 64KB, 512 块
        pool_configs.push_back({1048576, 128});  // 1MB, 128 块
    }
};

/**
 * @brief Runtime 核心管理器
 * 
 * 管理整个流处理系统的生命周期
 */
class Runtime {
public:
    /**
     * @brief 构造函数
     * 
     * @param config Runtime 配置
     */
    explicit Runtime(const RuntimeConfig& config = RuntimeConfig())
        : config_(config)
        , initialized_(false)
        , running_(false)
        , registry_(nullptr)
        , allocator_(nullptr)
        , scheduler_(nullptr)
        , msgbus_(nullptr)
        , process_id_(INVALID_PROCESS_ID)
        , process_slot_(-1)
        , blocks_()
        , port_queues_()
        , buffer_pools_()
    {}
    
    /**
     * @brief 析构函数
     */
    ~Runtime() {
        shutdown();
    }
    
    // 禁用拷贝和移动
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    
    /**
     * @brief 初始化 Runtime
     * 
     * @param create_registry 是否创建全局注册表（第一个进程设为 true）
     * @return true 成功，false 失败
     */
    bool initialize(bool create_registry = true) {
        if (initialized_) {
            return false;
        }
        
        try {
            // 初始化全局注册表
            if (!init_global_registry(create_registry)) {
                return false;
            }
            
            // 注册进程
            process_slot_ = registry_->process_registry.register_process(
                config_.process_name.c_str()
            );
            if (process_slot_ < 0) {
                return false;
            }
            process_id_ = registry_->process_registry.processes[process_slot_].process_id;
            
            // 初始化 Buffer Pool
            if (!init_buffer_pools(create_registry)) {
                return false;
            }
            
            // 创建 Buffer 分配器
            allocator_ = std::make_unique<SharedBufferAllocator>(registry_, process_id_);
            
            // 注册所有 Buffer Pool
            for (size_t i = 0; i < config_.pool_configs.size(); ++i) {
                std::string pool_name = get_pool_name(i);
                if (!allocator_->register_pool(static_cast<PoolId>(i), pool_name.c_str())) {
                    return false;
                }
            }
            
            // 创建 Scheduler
            SchedulerConfig scheduler_config;
            scheduler_config.num_threads = config_.num_scheduler_threads;
            scheduler_ = std::make_unique<Scheduler>(scheduler_config);
            
            // 创建 MsgBus
            msgbus_ = std::make_unique<MsgBus>();
            
            initialized_ = true;
            return true;
            
        } catch (const std::exception& e) {
            // 初始化失败
            return false;
        }
    }
    
    /**
     * @brief 注册 Block
     * 
     * @param block Block 智能指针
     * @return Block ID
     */
    BlockId register_block(std::unique_ptr<Block> block) {
        if (!initialized_ || !block) {
            return INVALID_BLOCK_ID;
        }
        
        // 在全局注册表中注册
        BlockId block_id = registry_->block_registry.register_block(
            block->name().c_str(),
            block->type(),
            process_id_
        );
        
        if (block_id == INVALID_BLOCK_ID) {
            return INVALID_BLOCK_ID;
        }
        
        // 设置 Block ID
        block->set_id(block_id);
        
        // 初始化 Block
        if (!block->initialize()) {
            registry_->block_registry.unregister_block(block_id);
            return INVALID_BLOCK_ID;
        }
        
        // 保存 Block
        blocks_[block_id] = std::move(block);
        
        return block_id;
    }
    
    /**
     * @brief 连接两个 Block
     * 
     * @param src_block 源 Block ID
     * @param src_port 源端口名称
     * @param dst_block 目标 Block ID
     * @param dst_port 目标端口名称
     * @return true 成功，false 失败
     */
    bool connect(BlockId src_block, const std::string& src_port,
                 BlockId dst_block, const std::string& dst_port) {
        if (!initialized_) {
            return false;
        }
        
        // 获取 Block
        Block* src = get_block(src_block);
        Block* dst = get_block(dst_block);
        if (!src || !dst) {
            return false;
        }
        
        // 获取端口
        OutputPort* out_port = src->get_output_port(src_port);
        InputPort* in_port = dst->get_input_port(dst_port);
        if (!out_port || !in_port) {
            return false;
        }
        
        // 创建 Port Queue
        std::string queue_name = get_queue_name(src_block, src_port, dst_block, dst_port);
        
        auto queue = std::make_unique<PortQueue>();
        if (!queue->create(queue_name.c_str(), out_port->id(), 
                          in_port->queue_capacity())) {
            return false;
        }
        
        // 连接端口
        out_port->set_queue(queue.get());
        in_port->set_queue(queue.get());
        
        // 保存队列
        port_queues_.push_back(std::move(queue));
        
        // 在全局注册表中注册连接
        registry_->connection_registry.create_connection(
            src_block, out_port->id(),
            dst_block, in_port->id()
        );
        
        return true;
    }
    
    /**
     * @brief 启动 Runtime
     * 
     * @return true 成功，false 失败
     */
    bool start() {
        if (!initialized_ || running_) {
            return false;
        }
        
        // 启动所有 Block
        for (auto& pair : blocks_) {
            Block* block = pair.second.get();
            if (!block->start()) {
                return false;
            }
            
            // 注册到 Scheduler
            scheduler_->register_block(block);
        }
        
        // 启动 MsgBus
        msgbus_->start();
        
        // 启动 Scheduler
        if (!scheduler_->start()) {
            return false;
        }
        
        running_ = true;
        return true;
    }
    
    /**
     * @brief 停止 Runtime
     */
    void stop() {
        if (!running_) {
            return;
        }
        
        // 停止 Scheduler
        scheduler_->stop();
        
        // 停止 MsgBus
        msgbus_->stop();
        
        // 停止所有 Block
        for (auto& pair : blocks_) {
            Block* block = pair.second.get();
            block->stop();
        }
        
        running_ = false;
    }
    
    /**
     * @brief 关闭 Runtime
     */
    void shutdown() {
        if (running_) {
            stop();
        }
        
        if (!initialized_) {
            return;
        }
        
        // 清理所有 Block
        for (auto& pair : blocks_) {
            Block* block = pair.second.get();
            block->cleanup();
            registry_->block_registry.unregister_block(block->id());
        }
        blocks_.clear();
        
        // 清理端口队列
        port_queues_.clear();
        
        // 清理 Scheduler 和 MsgBus
        scheduler_.reset();
        msgbus_.reset();
        
        // 清理 Buffer 分配器
        allocator_.reset();
        
        // 清理 Buffer Pool
        buffer_pools_.clear();
        
        // 注销进程
        if (process_slot_ >= 0) {
            registry_->process_registry.unregister_process(process_slot_);
        }
        
        initialized_ = false;
    }
    
    /**
     * @brief 是否已初始化
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * @brief 是否正在运行
     */
    bool is_running() const { return running_; }
    
    /**
     * @brief 获取进程 ID
     */
    ProcessId process_id() const { return process_id_; }
    
    /**
     * @brief 获取全局注册表
     */
    GlobalRegistry* registry() { return registry_; }
    
    /**
     * @brief 获取 Buffer 分配器
     */
    SharedBufferAllocator* allocator() { return allocator_.get(); }
    
    /**
     * @brief 获取 Scheduler
     */
    Scheduler* scheduler() { return scheduler_.get(); }
    
    /**
     * @brief 获取 MsgBus
     */
    MsgBus* msgbus() { return msgbus_.get(); }
    
    /**
     * @brief 获取 Block
     */
    Block* get_block(BlockId block_id) {
        auto it = blocks_.find(block_id);
        return (it != blocks_.end()) ? it->second.get() : nullptr;
    }
    
    /**
     * @brief 创建 Block（模板方法）
     * 
     * @tparam BlockType Block 类型
     * @tparam Args 构造参数类型
     * @param args 传递给 Block 构造函数的参数
     * @return BlockType* Block 指针，失败返回 nullptr
     * 
     * @note 自动分配 BlockId 并注册到 Scheduler
     */
    template<typename BlockType, typename... Args>
    BlockType* create_block(Args&&... args) {
        if (!initialized_) {
            return nullptr;
        }
        
        // 分配 BlockId
        BlockId block_id = allocate_block_id();
        if (block_id == INVALID_BLOCK_ID) {
            return nullptr;
        }
        
        // 创建 Block 实例
        try {
            auto block = std::make_unique<BlockType>(std::forward<Args>(args)...);
            
            // 设置 Block ID（如果 Block 支持）
            block->set_id(block_id);
            
            BlockType* block_ptr = block.get();
            
            // 添加到 blocks_ 映射
            blocks_[block_id] = std::move(block);
            
            // 注册到 Scheduler
            if (scheduler_) {
                scheduler_->register_block(block_ptr);
            }
            
            return block_ptr;
            
        } catch (const std::exception& e) {
            // 创建失败，回收 BlockId
            free_block_id(block_id);
            return nullptr;
        }
    }
    
    /**
     * @brief 移除 Block
     * 
     * @param block_id Block ID
     */
    void remove_block(BlockId block_id) {
        auto it = blocks_.find(block_id);
        if (it == blocks_.end()) {
            return;
        }
        
        // 从 Scheduler 注销
        if (scheduler_) {
            scheduler_->unregister_block(block_id);
        }
        
        // 移除 Block
        blocks_.erase(it);
        
        // 回收 BlockId
        free_block_id(block_id);
    }
    
private:
    /**
     * @brief 分配 BlockId
     */
    BlockId allocate_block_id() {
        static std::atomic<BlockId> next_block_id{1};
        return next_block_id.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 回收 BlockId（预留接口）
     */
    void free_block_id(BlockId /*block_id*/) {
        // 当前简单实现不回收 ID
        // 未来可以实现 ID 池复用
    }
    
    /**
     * @brief 初始化全局注册表
     */
    bool init_global_registry(bool /*create*/) {
        // 简化实现：假设已在外部创建
        // 实际应用中需要处理共享内存的创建/打开
        return true;
    }
    
    /**
     * @brief 初始化 Buffer Pool
     */
    bool init_buffer_pools(bool create) {
        for (size_t i = 0; i < config_.pool_configs.size(); ++i) {
            auto& pool_config = config_.pool_configs[i];
            std::string pool_name = get_pool_name(i);
            
            auto pool = std::make_unique<BufferPool>();
            
            if (create) {
                if (!pool->create(pool_name.c_str(), static_cast<PoolId>(i),
                                 pool_config.block_size, pool_config.block_count)) {
                    return false;
                }
                
                // 注册到全局注册表
                registry_->buffer_pool_registry.register_pool(
                    pool_config.block_size,
                    pool_config.block_count,
                    pool_name.c_str()
                );
            } else {
                if (!pool->open(pool_name.c_str())) {
                    return false;
                }
            }
            
            buffer_pools_.push_back(std::move(pool));
        }
        
        return true;
    }
    
    /**
     * @brief 获取 Pool 名称
     */
    std::string get_pool_name(size_t index) const {
        return std::string(BUFFER_POOL_SHM_PREFIX) + std::to_string(index);
    }
    
    /**
     * @brief 获取 Queue 名称
     */
    std::string get_queue_name(BlockId src_block, const std::string& src_port,
                                BlockId dst_block, const std::string& dst_port) const {
        return std::string(PORT_QUEUE_SHM_PREFIX) +
               std::to_string(src_block) + "_" + src_port + "_" +
               std::to_string(dst_block) + "_" + dst_port;
    }
    
    RuntimeConfig config_;                                    ///< 配置
    bool initialized_;                                        ///< 是否已初始化
    bool running_;                                            ///< 是否正在运行
    
    GlobalRegistry* registry_;                                ///< 全局注册表
    std::unique_ptr<SharedBufferAllocator> allocator_;        ///< Buffer 分配器
    std::unique_ptr<Scheduler> scheduler_;                    ///< 调度器
    std::unique_ptr<MsgBus> msgbus_;                          ///< 消息总线
    
    ProcessId process_id_;                                    ///< 进程 ID
    int32_t process_slot_;                                    ///< 进程槽位
    
    std::unordered_map<BlockId, std::unique_ptr<Block>> blocks_;          ///< Block 映射
    std::vector<std::unique_ptr<PortQueue>> port_queues_;                 ///< 端口队列
    std::vector<std::unique_ptr<BufferPool>> buffer_pools_;               ///< Buffer Pool
};

}  // namespace multiqueue

