#pragma once

#include "ring_queue.hpp"
#include "timestamp_sync.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace multiqueue {

/**
 * @brief 队列管理器
 * 
 * 管理多个共享内存队列，提供统一的创建、打开、删除接口
 * 支持队列合并和时间戳同步
 */
class QueueManager {
public:
    /**
     * @brief 构造函数
     */
    QueueManager() = default;
    
    /**
     * @brief 析构函数
     */
    ~QueueManager() {
        // 清理所有队列
        std::lock_guard<std::mutex> lock(mutex_);
        queues_.clear();
    }
    
    // 禁止拷贝和移动
    QueueManager(const QueueManager&) = delete;
    QueueManager& operator=(const QueueManager&) = delete;
    QueueManager(QueueManager&&) = delete;
    QueueManager& operator=(QueueManager&&) = delete;
    
    /**
     * @brief 创建或打开队列
     * 
     * @tparam T 元素类型
     * @param name 队列名称
     * @param config 队列配置
     * @return 队列的共享指针
     * @throws std::runtime_error 如果创建/打开失败
     */
    template<typename T>
    std::shared_ptr<RingQueue<T>> create_or_open(
        const std::string& name,
        const QueueConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查是否已经存在
        auto it = queues_.find(name);
        if (it != queues_.end()) {
            // 尝试转换为正确的类型
            auto queue_ptr = std::dynamic_pointer_cast<RingQueue<T>>(it->second);
            if (!queue_ptr) {
                throw std::runtime_error("Queue exists but type mismatch");
            }
            return queue_ptr;
        }
        
        // 创建新队列
        try {
            auto queue = std::make_shared<RingQueue<T>>(name, config);
            queues_[name] = queue;
            return queue;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Failed to create/open queue '") + name + "': " + e.what()
            );
        }
    }
    
    /**
     * @brief 合并多个队列（按时间戳同步）
     * 
     * @tparam T 元素类型
     * @param queue_names 队列名称列表
     * @param sync_timeout_ms 同步超时时间（毫秒）
     * @return 合并队列视图
     * @throws std::invalid_argument 如果队列不存在或配置不匹配
     */
    template<typename T>
    MergedQueueView<T> merge_queues(
        const std::vector<std::string>& queue_names,
        uint32_t sync_timeout_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 收集队列指针
        std::vector<std::shared_ptr<RingQueue<T>>> queues;
        for (const auto& name : queue_names) {
            auto it = queues_.find(name);
            if (it == queues_.end()) {
                throw std::invalid_argument("Queue '" + name + "' not found");
            }
            
            auto queue_ptr = std::dynamic_pointer_cast<RingQueue<T>>(it->second);
            if (!queue_ptr) {
                throw std::invalid_argument("Queue '" + name + "' type mismatch");
            }
            
            // 检查是否启用时间戳
            if (!queue_ptr->metadata().has_timestamp) {
                throw std::invalid_argument("Queue '" + name + "' does not have timestamp enabled");
            }
            
            queues.push_back(queue_ptr);
        }
        
        // 创建合并视图
        return MergedQueueView<T>(queues, sync_timeout_ms);
    }
    
    /**
     * @brief 获取队列统计信息
     * 
     * @param name 队列名称
     * @return 统计信息
     * @throws std::invalid_argument 如果队列不存在
     */
    QueueStats get_stats(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = queues_.find(name);
        if (it == queues_.end()) {
            throw std::invalid_argument("Queue '" + name + "' not found");
        }
        
        // 使用类型擦除的方式获取统计信息
        // 这里简化处理，实际可能需要更复杂的类型处理
        // 暂时返回默认统计信息
        return QueueStats();
    }
    
    /**
     * @brief 删除队列
     * 
     * @param name 队列名称
     * @return true 如果成功删除
     * 
     * @note 需要确保所有进程都关闭了队列才能删除
     */
    bool remove_queue(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = queues_.find(name);
        if (it == queues_.end()) {
            return false;
        }
        
        // 从管理器中移除
        queues_.erase(it);
        
        // 尝试删除共享内存
        try {
            boost::interprocess::shared_memory_object::remove(name.c_str());
            return true;
        } catch (...) {
            return false;
        }
    }
    
    /**
     * @brief 列出所有管理的队列名称
     * @return 队列名称列表
     */
    std::vector<std::string> list_queues() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::string> names;
        for (const auto& pair : queues_) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    /**
     * @brief 检查队列是否存在
     * @param name 队列名称
     * @return true 如果队列存在
     */
    bool exists(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queues_.find(name) != queues_.end();
    }
    
    /**
     * @brief 关闭指定队列
     * @param name 队列名称
     * @return true 如果成功关闭
     */
    bool close_queue(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = queues_.find(name);
        if (it == queues_.end()) {
            return false;
        }
        
        queues_.erase(it);
        return true;
    }
    
    /**
     * @brief 关闭所有队列
     */
    void close_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_.clear();
    }
    
private:
    /// 队列映射表（队列名称 -> 队列对象）
    /// 使用 void* 以支持不同类型的队列
    std::map<std::string, std::shared_ptr<void>> queues_;
    
    /// 互斥锁，保护队列映射表
    mutable std::mutex mutex_;
};

} // namespace multiqueue


