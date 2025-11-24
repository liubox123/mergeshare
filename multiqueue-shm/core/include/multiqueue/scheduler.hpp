/**
 * @file scheduler.hpp
 * @brief Scheduler 调度器
 * 
 * 负责调度 Block 的执行
 */

#pragma once

#include "types.hpp"
#include "block.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <queue>
#include <unordered_map>

namespace multiqueue {

/**
 * @brief Scheduler 配置
 */
struct SchedulerConfig {
    size_t num_threads;            ///< 工作线程数量（0 表示自动检测）
    uint32_t idle_sleep_ms;        ///< 空闲时休眠时间（毫秒）
    bool enable_work_stealing;     ///< 是否启用工作窃取（暂未实现）
    
    SchedulerConfig()
        : num_threads(0)
        , idle_sleep_ms(1)
        , enable_work_stealing(false)
    {}
};

/**
 * @brief Scheduler 调度器
 * 
 * 管理工作线程并调度 Block 执行
 */
class Scheduler {
public:
    /**
     * @brief 构造函数
     * 
     * @param config 调度器配置
     */
    explicit Scheduler(const SchedulerConfig& config = SchedulerConfig())
        : config_(config)
        , running_(false)
        , blocks_()
        , blocks_mutex_()
        , threads_()
    {
        // 自动检测线程数
        if (config_.num_threads == 0) {
            config_.num_threads = std::thread::hardware_concurrency();
            if (config_.num_threads == 0) {
                config_.num_threads = 4;  // 默认 4 个线程
            }
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~Scheduler() {
        stop();
    }
    
    // 禁用拷贝和移动
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;
    
    /**
     * @brief 注册 Block
     * 
     * @param block Block 指针
     * @return true 成功，false 失败
     */
    bool register_block(Block* block) {
        if (!block) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(blocks_mutex_);
        
        BlockId block_id = block->id();
        if (blocks_.find(block_id) != blocks_.end()) {
            return false;  // 已存在
        }
        
        blocks_[block_id] = block;
        return true;
    }
    
    /**
     * @brief 注销 Block
     * 
     * @param block_id Block ID
     */
    void unregister_block(BlockId block_id) {
        std::lock_guard<std::mutex> lock(blocks_mutex_);
        blocks_.erase(block_id);
    }
    
    /**
     * @brief 启动调度器
     * 
     * @return true 成功，false 失败
     */
    bool start() {
        if (running_.load(std::memory_order_acquire)) {
            return false;  // 已在运行
        }
        
        running_.store(true, std::memory_order_release);
        
        // 创建工作线程
        for (size_t i = 0; i < config_.num_threads; ++i) {
            threads_.emplace_back(&Scheduler::worker_thread, this, i);
        }
        
        return true;
    }
    
    /**
     * @brief 停止调度器
     */
    void stop() {
        if (!running_.load(std::memory_order_acquire)) {
            return;  // 未在运行
        }
        
        running_.store(false, std::memory_order_release);
        
        // 等待所有线程结束
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        threads_.clear();
    }
    
    /**
     * @brief 是否正在运行
     */
    bool is_running() const {
        return running_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取工作线程数量
     */
    size_t num_threads() const {
        return config_.num_threads;
    }
    
    /**
     * @brief 获取注册的 Block 数量
     */
    size_t block_count() const {
        std::lock_guard<std::mutex> lock(blocks_mutex_);
        return blocks_.size();
    }
    
private:
    /**
     * @brief 工作线程函数
     * 
     * @param thread_id 线程 ID
     */
    void worker_thread(size_t thread_id) {
        (void)thread_id;  // 未使用
        
        while (running_.load(std::memory_order_acquire)) {
            bool did_work = false;
            
            // 遍历所有 Block 并执行 work()
            {
                std::lock_guard<std::mutex> lock(blocks_mutex_);
                
                for (auto& pair : blocks_) {
                    Block* block = pair.second;
                    
                    if (!block || block->state() != BlockState::RUNNING) {
                        continue;
                    }
                    
                    // 执行 Block 的 work() 方法
                    WorkResult result = block->work();
                    
                    // 处理结果
                    switch (result) {
                        case WorkResult::OK:
                            did_work = true;
                            break;
                            
                        case WorkResult::DONE:
                            // Block 完成工作
                            block->set_state(BlockState::STOPPED);
                            break;
                            
                        case WorkResult::INSUFFICIENT_INPUT:
                        case WorkResult::INSUFFICIENT_OUTPUT:
                            // 暂时无法工作，继续尝试其他 Block
                            break;
                            
                        case WorkResult::ERROR:
                            // 发生错误
                            block->set_state(BlockState::ERROR);
                            break;
                    }
                }
            }
            
            // 如果没有做任何工作，休眠一段时间
            if (!did_work && config_.idle_sleep_ms > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.idle_sleep_ms)
                );
            }
        }
    }
    
    SchedulerConfig config_;                           ///< 配置
    std::atomic<bool> running_;                        ///< 是否正在运行
    
    std::unordered_map<BlockId, Block*> blocks_;      ///< Block 映射
    mutable std::mutex blocks_mutex_;                  ///< Block 映射锁
    
    std::vector<std::thread> threads_;                 ///< 工作线程
};

}  // namespace multiqueue

