/**
 * @file msgbus.hpp
 * @brief 消息总线（简化实现）
 * 
 * MsgBus 提供进程间消息传递和发布-订阅功能
 */

#pragma once

#include "types.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <memory>

namespace multiqueue {

/**
 * @brief 总线消息（用于发布-订阅）
 */
struct BusMessage {
    ProcessId from;                ///< 发送者进程 ID
    ProcessId to;                  ///< 接收者进程 ID（0 表示广播）
    std::string topic;             ///< 主题（用于发布-订阅）
    std::vector<char> data;        ///< 消息数据
    
    BusMessage()
        : from(INVALID_PROCESS_ID)
        , to(INVALID_PROCESS_ID)
    {}
    
    BusMessage(ProcessId f, ProcessId t, const std::string& tp, const void* d, size_t size)
        : from(f)
        , to(t)
        , topic(tp)
        , data(static_cast<const char*>(d), static_cast<const char*>(d) + size)
    {}
};

/**
 * @brief 消息总线（简化版本，仅用于单进程内通信）
 * 
 * 注意：当前实现是进程内版本，未来可扩展为跨进程版本
 */
class MsgBus {
public:
    /**
     * @brief 构造函数
     */
    MsgBus()
        : subscriptions_()
        , message_queues_()
        , mutex_()
    {}
    
    /**
     * @brief 析构函数
     */
    ~MsgBus() = default;
    
    // 禁用拷贝和移动
    MsgBus(const MsgBus&) = delete;
    MsgBus& operator=(const MsgBus&) = delete;
    MsgBus(MsgBus&&) = delete;
    MsgBus& operator=(MsgBus&&) = delete;
    
    /**
     * @brief 初始化
     */
    bool initialize() {
        // 简化实现：无需特殊初始化
        return true;
    }
    
    /**
     * @brief 启动（兼容性接口）
     */
    bool start() {
        return true;
    }
    
    /**
     * @brief 停止（兼容性接口）
     */
    void stop() {
        // 暂不需要特殊操作
    }
    
    /**
     * @brief 关闭
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.clear();
        message_queues_.clear();
    }
    
    // ===== 发布-订阅模式 =====
    
    /**
     * @brief 订阅主题
     * 
     * @param process_id 订阅者进程 ID
     * @param block_id 订阅者 Block ID
     * @param topic 主题名称
     * @return true 成功，false 失败
     */
    bool subscribe(ProcessId process_id, BlockId block_id, const std::string& topic) {
        (void)block_id;  // 简化实现暂不使用
        
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_[topic].insert(process_id);
        return true;
    }
    
    /**
     * @brief 取消订阅
     * 
     * @param process_id 订阅者进程 ID
     * @param topic 主题名称
     */
    void unsubscribe(ProcessId process_id, const std::string& topic) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = subscriptions_.find(topic);
        if (it != subscriptions_.end()) {
            it->second.erase(process_id);
            
            // 如果没有订阅者了，删除主题
            if (it->second.empty()) {
                subscriptions_.erase(it);
            }
        }
    }
    
    /**
     * @brief 发布消息到主题
     * 
     * @param topic 主题名称
     * @param data 消息数据
     * @param size 数据大小
     * @return true 成功，false 失败
     */
    bool publish(const std::string& topic, const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 查找订阅者
        auto it = subscriptions_.find(topic);
        if (it == subscriptions_.end() || it->second.empty()) {
            return true;  // 没有订阅者，视为成功
        }
        
        // 为每个订阅者创建消息
        for (ProcessId subscriber : it->second) {
            BusMessage msg(INVALID_PROCESS_ID, subscriber, topic, data, size);
            message_queues_[subscriber].push(std::move(msg));
        }
        
        return true;
    }
    
    // ===== 点对点消息 =====
    
    /**
     * @brief 发送点对点消息
     * 
     * @param from 发送者进程 ID
     * @param to 接收者进程 ID
     * @param data 消息数据
     * @param size 数据大小
     * @return true 成功，false 失败
     */
    bool send_message(ProcessId from, ProcessId to, const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        BusMessage msg(from, to, "", data, size);
        message_queues_[to].push(std::move(msg));
        
        return true;
    }
    
    /**
     * @brief 接收消息
     * 
     * @param process_id 接收者进程 ID
     * @param buffer 接收缓冲区
     * @param size 缓冲区大小（输入），实际接收大小（输出）
     * @return true 成功接收到消息，false 无消息
     */
    bool receive_message(ProcessId process_id, void* buffer, size_t& size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = message_queues_.find(process_id);
        if (it == message_queues_.end() || it->second.empty()) {
            return false;  // 无消息
        }
        
        BusMessage& msg = it->second.front();
        
        // 检查缓冲区是否足够大
        if (msg.data.size() > size) {
            return false;  // 缓冲区太小
        }
        
        // 拷贝数据
        std::memcpy(buffer, msg.data.data(), msg.data.size());
        size = msg.data.size();
        
        // 移除消息
        it->second.pop();
        
        return true;
    }
    
    /**
     * @brief 检查是否有消息
     * 
     * @param process_id 进程 ID
     * @return true 有消息，false 无消息
     */
    bool has_message(ProcessId process_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = message_queues_.find(process_id);
        return (it != message_queues_.end() && !it->second.empty());
    }
    
    /**
     * @brief 获取消息队列大小
     * 
     * @param process_id 进程 ID
     * @return 队列中的消息数量
     */
    size_t message_count(ProcessId process_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = message_queues_.find(process_id);
        return (it != message_queues_.end()) ? it->second.size() : 0;
    }
    
    /**
     * @brief 清空消息队列
     * 
     * @param process_id 进程 ID
     */
    void clear_messages(ProcessId process_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = message_queues_.find(process_id);
        if (it != message_queues_.end()) {
            while (!it->second.empty()) {
                it->second.pop();
            }
        }
    }
    
    // ===== 统计信息 =====
    
    /**
     * @brief 获取主题数量
     */
    size_t topic_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.size();
    }
    
    /**
     * @brief 获取订阅者数量
     * 
     * @param topic 主题名称
     * @return 订阅者数量
     */
    size_t subscriber_count(const std::string& topic) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = subscriptions_.find(topic);
        return (it != subscriptions_.end()) ? it->second.size() : 0;
    }
    
private:
    /// 订阅关系：topic -> set<ProcessId>
    std::unordered_map<std::string, std::unordered_set<ProcessId>> subscriptions_;
    
    /// 消息队列：ProcessId -> queue<BusMessage>
    std::unordered_map<ProcessId, std::queue<BusMessage>> message_queues_;
    
    /// 互斥锁（进程内同步）
    mutable std::mutex mutex_;
};

}  // namespace multiqueue
