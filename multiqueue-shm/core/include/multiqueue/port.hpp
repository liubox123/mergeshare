/**
 * @file port.hpp
 * @brief Block 端口定义
 * 
 * 定义 Block 的输入和输出端口，支持广播模式
 */

#pragma once

#include "types.hpp"
#include "buffer_ptr.hpp"
#include "port_queue.hpp"
#include <string>
#include <memory>

namespace multiqueue {

/**
 * @brief 端口配置
 */
struct PortConfig {
    std::string name;              ///< 端口名称
    PortType type;                 ///< 端口类型（输入/输出）
    SyncMode sync_mode;            ///< 同步模式
    size_t queue_capacity;         ///< 队列容量
    
    PortConfig()
        : name()
        , type(PortType::INPUT)
        , sync_mode(SyncMode::ASYNC)
        , queue_capacity(DEFAULT_PORT_QUEUE_SIZE)
    {}
    
    PortConfig(const std::string& n, PortType t, SyncMode sm = SyncMode::ASYNC)
        : name(n)
        , type(t)
        , sync_mode(sm)
        , queue_capacity(DEFAULT_PORT_QUEUE_SIZE)
    {}
};

/**
 * @brief 端口基类
 */
class Port {
public:
    /**
     * @brief 构造函数
     */
    Port(PortId id, const PortConfig& config)
        : port_id_(id)
        , config_(config)
        , queue_(nullptr)
        , connected_(false)
    {}
    
    virtual ~Port() = default;
    
    /**
     * @brief 获取端口 ID
     */
    PortId id() const { return port_id_; }
    
    /**
     * @brief 获取端口名称
     */
    const std::string& name() const { return config_.name; }
    
    /**
     * @brief 获取端口类型
     */
    PortType type() const { return config_.type; }
    
    /**
     * @brief 获取同步模式
     */
    SyncMode sync_mode() const { return config_.sync_mode; }
    
    /**
     * @brief 获取队列容量
     */
    size_t queue_capacity() const { return config_.queue_capacity; }
    
    /**
     * @brief 是否已连接
     */
    bool is_connected() const { return connected_; }
    
    /**
     * @brief 获取端口队列
     */
    PortQueue* queue() { return queue_; }
    const PortQueue* queue() const { return queue_; }
    
protected:
    PortId port_id_;               ///< 端口 ID
    PortConfig config_;            ///< 端口配置
    PortQueue* queue_;             ///< 端口队列指针
    bool connected_;               ///< 是否已连接
};

/**
 * @brief 输入端口（支持广播模式）
 */
class InputPort : public Port {
public:
    InputPort(PortId id, const PortConfig& config)
        : Port(id, config)
        , consumer_id_(INVALID_CONSUMER_ID)
    {
        config_.type = PortType::INPUT;
    }
    
    /**
     * @brief 析构函数，自动注销消费者
     */
    ~InputPort() {
        disconnect();
    }
    
    /**
     * @brief 设置端口队列并注册为消费者
     */
    void set_queue(PortQueue* queue) {
        // 如果已经连接，先断开
        if (connected_) {
            disconnect();
        }
        
        queue_ = queue;
        
        if (queue_) {
            // 注册为消费者
            consumer_id_ = queue_->register_consumer();
            connected_ = (consumer_id_ != INVALID_CONSUMER_ID);
        } else {
            connected_ = false;
            consumer_id_ = INVALID_CONSUMER_ID;
        }
    }
    
    /**
     * @brief 断开连接并注销消费者
     */
    void disconnect() {
        if (queue_ && consumer_id_ != INVALID_CONSUMER_ID) {
            queue_->unregister_consumer(consumer_id_);
            consumer_id_ = INVALID_CONSUMER_ID;
        }
        queue_ = nullptr;
        connected_ = false;
    }
    
    /**
     * @brief 获取消费者 ID
     */
    ConsumerId consumer_id() const { return consumer_id_; }
    
    /**
     * @brief 从端口读取 Buffer（非阻塞）
     * 
     * @param buffer 输出参数，Buffer 指针
     * @param allocator Buffer 分配器
     * @return true 成功，false 失败
     */
    bool read(BufferPtr& buffer, SharedBufferAllocator* allocator) {
        if (!queue_ || !allocator || consumer_id_ == INVALID_CONSUMER_ID) {
            return false;
        }
        
        BufferId buffer_id;
        if (!queue_->pop(consumer_id_, buffer_id)) {
            return false;
        }
        
        // 创建 BufferPtr（引用计数已在 pop 中处理）
        buffer = BufferPtr(buffer_id, allocator);
        return buffer.valid();
    }
    
    /**
     * @brief 从端口读取 Buffer（超时）
     * 
     * @param buffer 输出参数，Buffer 指针
     * @param allocator Buffer 分配器
     * @param timeout_ms 超时时间（毫秒）
     * @return true 成功，false 失败
     */
    bool read_with_timeout(BufferPtr& buffer, SharedBufferAllocator* allocator, 
                           uint32_t timeout_ms) {
        if (!queue_ || !allocator || consumer_id_ == INVALID_CONSUMER_ID) {
            return false;
        }
        
        BufferId buffer_id;
        if (!queue_->pop_with_timeout(consumer_id_, buffer_id, timeout_ms)) {
            return false;
        }
        
        // 创建 BufferPtr（引用计数已在 pop 中处理）
        buffer = BufferPtr(buffer_id, allocator);
        return buffer.valid();
    }
    
    /**
     * @brief 检查是否有数据可读
     */
    bool has_data() const {
        if (!queue_ || consumer_id_ == INVALID_CONSUMER_ID) {
            return false;
        }
        return !queue_->empty(consumer_id_);
    }
    
    /**
     * @brief 获取队列中的数据数量（对于当前消费者）
     */
    size_t available() const {
        if (!queue_ || consumer_id_ == INVALID_CONSUMER_ID) {
            return 0;
        }
        return queue_->size(consumer_id_);
    }

private:
    ConsumerId consumer_id_;       ///< 消费者 ID（广播模式）
};

/**
 * @brief 输出端口
 */
class OutputPort : public Port {
public:
    OutputPort(PortId id, const PortConfig& config)
        : Port(id, config)
    {
        config_.type = PortType::OUTPUT;
    }
    
    /**
     * @brief 设置端口队列
     */
    void set_queue(PortQueue* queue) {
        queue_ = queue;
        connected_ = (queue != nullptr);
    }
    
    /**
     * @brief 向端口写入 Buffer（阻塞）
     * 
     * @param buffer Buffer 指针
     * @return true 成功，false 失败
     */
    bool write(const BufferPtr& buffer) {
        if (!queue_ || !buffer.valid()) {
            return false;
        }
        
        // 只传递 Buffer ID（引用计数在 push 中处理）
        return queue_->push(buffer.id());
    }
    
    /**
     * @brief 向端口写入 Buffer（超时）
     * 
     * @param buffer Buffer 指针
     * @param timeout_ms 超时时间（毫秒）
     * @return true 成功，false 失败
     */
    bool write_with_timeout(const BufferPtr& buffer, uint32_t timeout_ms) {
        if (!queue_ || !buffer.valid()) {
            return false;
        }
        
        // 只传递 Buffer ID（引用计数在 push 中处理）
        return queue_->push_with_timeout(buffer.id(), timeout_ms);
    }
    
    /**
     * @brief 检查是否可以写入
     */
    bool can_write() const {
        // 在广播模式下，需要检查最慢的消费者是否还有空间
        // 这个逻辑在 PortQueue::push 中处理
        return queue_ && !queue_->is_closed();
    }
    
    /**
     * @brief 获取消费者数量
     */
    uint32_t consumer_count() const {
        return queue_ ? queue_->get_consumer_count() : 0;
    }
};

}  // namespace multiqueue

