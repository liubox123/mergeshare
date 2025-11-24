/**
 * @file msgbus.hpp
 * @brief 消息总线
 * 
 * 用于 Block 之间的控制消息传递
 */

#pragma once

#include "types.hpp"
#include "message.hpp"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>

namespace multiqueue {

/**
 * @brief 消息处理器类型
 */
using MessageHandler = std::function<void(const Message&)>;

/**
 * @brief 消息总线
 * 
 * 简化版本：使用进程内的队列和回调机制
 * 未来可扩展为跨进程的共享内存消息队列
 */
class MsgBus {
public:
    /**
     * @brief 构造函数
     */
    MsgBus()
        : messages_()
        , messages_mutex_()
        , messages_cv_()
        , handlers_()
        , handlers_mutex_()
        , running_(false)
        , dispatch_thread_()
    {}
    
    /**
     * @brief 析构函数
     */
    ~MsgBus() {
        stop();
    }
    
    // 禁用拷贝和移动
    MsgBus(const MsgBus&) = delete;
    MsgBus& operator=(const MsgBus&) = delete;
    MsgBus(MsgBus&&) = delete;
    MsgBus& operator=(MsgBus&&) = delete;
    
    /**
     * @brief 启动消息总线
     */
    void start() {
        if (running_.load(std::memory_order_acquire)) {
            return;
        }
        
        running_.store(true, std::memory_order_release);
        dispatch_thread_ = std::thread(&MsgBus::dispatch_loop, this);
    }
    
    /**
     * @brief 停止消息总线
     */
    void stop() {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        
        running_.store(false, std::memory_order_release);
        messages_cv_.notify_all();
        
        if (dispatch_thread_.joinable()) {
            dispatch_thread_.join();
        }
    }
    
    /**
     * @brief 发布消息
     * 
     * @param message 消息
     */
    void publish(const Message& message) {
        {
            std::lock_guard<std::mutex> lock(messages_mutex_);
            messages_.push(message);
        }
        messages_cv_.notify_one();
    }
    
    /**
     * @brief 订阅消息
     * 
     * @param block_id Block ID（订阅特定 Block 的消息，0 表示订阅所有）
     * @param handler 消息处理器
     */
    void subscribe(BlockId block_id, MessageHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[block_id].push_back(handler);
    }
    
    /**
     * @brief 取消订阅（清除所有处理器）
     * 
     * @param block_id Block ID
     */
    void unsubscribe(BlockId block_id) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.erase(block_id);
    }
    
    /**
     * @brief 发送控制消息
     * 
     * @param source_block 源 Block ID
     * @param target_block 目标 Block ID
     * @param command 控制命令
     */
    void send_control(BlockId source_block, BlockId target_block, ControlCommand command) {
        Message msg(MessageType::CONTROL, source_block, target_block);
        
        ControlMessagePayload payload;
        payload.command = command;
        msg.set_payload(payload);
        
        publish(msg);
    }
    
    /**
     * @brief 发送参数消息
     * 
     * @param source_block 源 Block ID
     * @param target_block 目标 Block ID
     * @param param_name 参数名称
     * @param param_value 参数值
     */
    void send_parameter(BlockId source_block, BlockId target_block,
                        const std::string& param_name, const std::string& param_value) {
        Message msg(MessageType::PARAMETER, source_block, target_block);
        
        ParameterMessagePayload payload;
        strncpy(payload.param_name, param_name.c_str(), sizeof(payload.param_name) - 1);
        strncpy(payload.param_value, param_value.c_str(), sizeof(payload.param_value) - 1);
        msg.set_payload(payload);
        
        publish(msg);
    }
    
    /**
     * @brief 发送状态消息
     * 
     * @param source_block 源 Block ID
     * @param state Block 状态
     * @param status_text 状态文本
     */
    void send_status(BlockId source_block, BlockState state, const std::string& status_text = "") {
        Message msg(MessageType::STATUS, source_block, INVALID_BLOCK_ID);
        
        StatusMessagePayload payload;
        payload.state = state;
        if (!status_text.empty()) {
            strncpy(payload.status_text, status_text.c_str(), sizeof(payload.status_text) - 1);
        }
        msg.set_payload(payload);
        
        publish(msg);
    }
    
    /**
     * @brief 发送错误消息
     * 
     * @param source_block 源 Block ID
     * @param error_code 错误码
     * @param error_message 错误消息
     */
    void send_error(BlockId source_block, uint32_t error_code, const std::string& error_message) {
        Message msg(MessageType::ERROR, source_block, INVALID_BLOCK_ID);
        
        ErrorMessagePayload payload;
        payload.error_code = error_code;
        strncpy(payload.error_message, error_message.c_str(), sizeof(payload.error_message) - 1);
        msg.set_payload(payload);
        
        publish(msg);
    }
    
private:
    /**
     * @brief 消息分发循环
     */
    void dispatch_loop() {
        while (running_.load(std::memory_order_acquire)) {
            Message message;
            
            // 获取消息
            {
                std::unique_lock<std::mutex> lock(messages_mutex_);
                messages_cv_.wait(lock, [this]() {
                    return !messages_.empty() || !running_.load(std::memory_order_acquire);
                });
                
                if (!running_.load(std::memory_order_acquire)) {
                    break;
                }
                
                if (messages_.empty()) {
                    continue;
                }
                
                message = messages_.front();
                messages_.pop();
            }
            
            // 分发消息
            dispatch_message(message);
        }
    }
    
    /**
     * @brief 分发消息给处理器
     * 
     * @param message 消息
     */
    void dispatch_message(const Message& message) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        
        BlockId target = message.header().target_block;
        
        // 分发给特定目标
        if (target != INVALID_BLOCK_ID) {
            auto it = handlers_.find(target);
            if (it != handlers_.end()) {
                for (auto& handler : it->second) {
                    handler(message);
                }
            }
        }
        
        // 分发给广播订阅者（Block ID = 0）
        auto it = handlers_.find(INVALID_BLOCK_ID);
        if (it != handlers_.end()) {
            for (auto& handler : it->second) {
                handler(message);
            }
        }
    }
    
    std::queue<Message> messages_;                                ///< 消息队列
    std::mutex messages_mutex_;                                    ///< 消息队列锁
    std::condition_variable messages_cv_;                          ///< 消息队列条件变量
    
    std::unordered_map<BlockId, std::vector<MessageHandler>> handlers_;  ///< 消息处理器映射
    std::mutex handlers_mutex_;                                           ///< 处理器映射锁
    
    std::atomic<bool> running_;                                    ///< 是否正在运行
    std::thread dispatch_thread_;                                  ///< 分发线程
};

}  // namespace multiqueue

