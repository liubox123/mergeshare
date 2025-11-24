/**
 * @file block.hpp
 * @brief Block 基类定义
 * 
 * Block 是流处理框架的基本处理单元
 */

#pragma once

#include "types.hpp"
#include "port.hpp"
#include "buffer_ptr.hpp"
#include "buffer_allocator.hpp"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace multiqueue {

/**
 * @brief Block 配置
 */
struct BlockConfig {
    std::string name;              ///< Block 名称
    BlockType type;                ///< Block 类型
    LogLevel log_level;            ///< 日志级别
    
    BlockConfig()
        : name()
        , type(BlockType::PROCESSING)
        , log_level(LogLevel::INFO)
    {}
    
    BlockConfig(const std::string& n, BlockType t)
        : name(n)
        , type(t)
        , log_level(LogLevel::INFO)
    {}
};

/**
 * @brief Block 基类
 * 
 * 所有处理 Block 的基类，定义了标准接口
 */
class Block {
public:
    /**
     * @brief 构造函数
     * 
     * @param config Block 配置
     * @param allocator Buffer 分配器
     */
    Block(const BlockConfig& config, SharedBufferAllocator* allocator)
        : block_id_(INVALID_BLOCK_ID)
        , config_(config)
        , state_(BlockState::CREATED)
        , allocator_(allocator)
        , input_ports_()
        , output_ports_()
        , input_port_map_()
        , output_port_map_()
    {}
    
    virtual ~Block() = default;
    
    // 禁用拷贝和移动
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;
    
    // ===== 基本信息 =====
    
    /**
     * @brief 获取 Block ID
     */
    BlockId id() const { return block_id_; }
    
    /**
     * @brief 设置 Block ID（由 Runtime 调用）
     */
    void set_id(BlockId id) { block_id_ = id; }
    
    /**
     * @brief 获取 Block 名称
     */
    const std::string& name() const { return config_.name; }
    
    /**
     * @brief 获取 Block 类型
     */
    BlockType type() const { return config_.type; }
    
    /**
     * @brief 获取 Block 状态
     */
    BlockState state() const { return state_; }
    
    /**
     * @brief 设置 Block 状态
     */
    void set_state(BlockState state) { state_ = state; }
    
    // ===== 端口管理 =====
    
    /**
     * @brief 添加输入端口
     * 
     * @param port_config 端口配置
     * @return 端口 ID
     */
    PortId add_input_port(const PortConfig& port_config) {
        PortId port_id = static_cast<PortId>(input_ports_.size() + 1);
        auto port = std::make_unique<InputPort>(port_id, port_config);
        
        input_port_map_[port_config.name] = port_id;
        input_ports_.push_back(std::move(port));
        
        return port_id;
    }
    
    /**
     * @brief 添加输出端口
     * 
     * @param port_config 端口配置
     * @return 端口 ID
     */
    PortId add_output_port(const PortConfig& port_config) {
        PortId port_id = static_cast<PortId>(output_ports_.size() + 1);
        auto port = std::make_unique<OutputPort>(port_id, port_config);
        
        output_port_map_[port_config.name] = port_id;
        output_ports_.push_back(std::move(port));
        
        return port_id;
    }
    
    /**
     * @brief 获取输入端口（按索引）
     */
    InputPort* get_input_port(size_t index) {
        if (index >= input_ports_.size()) {
            return nullptr;
        }
        return input_ports_[index].get();
    }
    
    /**
     * @brief 获取输出端口（按索引）
     */
    OutputPort* get_output_port(size_t index) {
        if (index >= output_ports_.size()) {
            return nullptr;
        }
        return output_ports_[index].get();
    }
    
    /**
     * @brief 获取输入端口（按名称）
     */
    InputPort* get_input_port(const std::string& name) {
        auto it = input_port_map_.find(name);
        if (it == input_port_map_.end()) {
            return nullptr;
        }
        PortId port_id = it->second;
        return input_ports_[port_id - 1].get();
    }
    
    /**
     * @brief 获取输出端口（按名称）
     */
    OutputPort* get_output_port(const std::string& name) {
        auto it = output_port_map_.find(name);
        if (it == output_port_map_.end()) {
            return nullptr;
        }
        PortId port_id = it->second;
        return output_ports_[port_id - 1].get();
    }
    
    /**
     * @brief 获取输入端口数量
     */
    size_t input_port_count() const { return input_ports_.size(); }
    
    /**
     * @brief 获取输出端口数量
     */
    size_t output_port_count() const { return output_ports_.size(); }
    
    // ===== Buffer 操作 =====
    
    /**
     * @brief 分配输出 Buffer
     * 
     * @param size Buffer 大小
     * @return Buffer 智能指针
     */
    BufferPtr allocate_output_buffer(size_t size) {
        if (!allocator_) {
            return BufferPtr();
        }
        
        BufferId buffer_id = allocator_->allocate(size);
        if (buffer_id == INVALID_BUFFER_ID) {
            return BufferPtr();
        }
        
        return BufferPtr(buffer_id, allocator_);
    }
    
    /**
     * @brief 从输入端口读取 Buffer
     * 
     * @param port_index 端口索引
     * @param timeout_ms 超时时间（毫秒），0 表示阻塞
     * @return Buffer 智能指针
     */
    BufferPtr consume_input(size_t port_index, uint32_t timeout_ms = 0) {
        InputPort* port = get_input_port(port_index);
        if (!port || !allocator_) {
            return BufferPtr();
        }
        
        BufferPtr buffer;
        bool success;
        
        if (timeout_ms > 0) {
            success = port->read_with_timeout(buffer, allocator_, timeout_ms);
        } else {
            success = port->read(buffer, allocator_);
        }
        
        return success ? buffer : BufferPtr();
    }
    
    /**
     * @brief 向输出端口写入 Buffer
     * 
     * @param port_index 端口索引
     * @param buffer Buffer 智能指针
     * @param timeout_ms 超时时间（毫秒），0 表示阻塞
     * @return true 成功，false 失败
     */
    bool produce_output(size_t port_index, const BufferPtr& buffer, uint32_t timeout_ms = 0) {
        OutputPort* port = get_output_port(port_index);
        if (!port) {
            return false;
        }
        
        if (timeout_ms > 0) {
            return port->write_with_timeout(buffer, timeout_ms);
        } else {
            return port->write(buffer);
        }
    }
    
    // ===== 生命周期方法（子类可重写）=====
    
    /**
     * @brief 初始化（在 Block 注册后调用）
     * 
     * @return true 成功，false 失败
     */
    virtual bool initialize() {
        return true;
    }
    
    /**
     * @brief 启动（在开始处理前调用）
     * 
     * @return true 成功，false 失败
     */
    virtual bool start() {
        state_ = BlockState::RUNNING;
        return true;
    }
    
    /**
     * @brief 停止（在停止处理时调用）
     */
    virtual void stop() {
        state_ = BlockState::STOPPED;
    }
    
    /**
     * @brief 清理（在 Block 注销前调用）
     */
    virtual void cleanup() {
        // 默认不做任何事
    }
    
    /**
     * @brief 工作方法（核心处理逻辑，由子类实现）
     * 
     * 此方法会被 Scheduler 反复调用
     * 
     * @return 工作结果
     */
    virtual WorkResult work() = 0;
    
protected:
    BlockId block_id_;                                       ///< Block ID
    BlockConfig config_;                                     ///< Block 配置
    BlockState state_;                                       ///< Block 状态
    SharedBufferAllocator* allocator_;                       ///< Buffer 分配器
    
    std::vector<std::unique_ptr<InputPort>> input_ports_;    ///< 输入端口列表
    std::vector<std::unique_ptr<OutputPort>> output_ports_;  ///< 输出端口列表
    
    std::unordered_map<std::string, PortId> input_port_map_;  ///< 输入端口名称映射
    std::unordered_map<std::string, PortId> output_port_map_; ///< 输出端口名称映射
};

/**
 * @brief Source Block 基类
 * 
 * 只有输出端口，没有输入端口
 */
class SourceBlock : public Block {
public:
    SourceBlock(const BlockConfig& config, SharedBufferAllocator* allocator)
        : Block(config, allocator)
    {
        config_.type = BlockType::SOURCE;
    }
};

/**
 * @brief Sink Block 基类
 * 
 * 只有输入端口，没有输出端口
 */
class SinkBlock : public Block {
public:
    SinkBlock(const BlockConfig& config, SharedBufferAllocator* allocator)
        : Block(config, allocator)
    {
        config_.type = BlockType::SINK;
    }
};

/**
 * @brief Processing Block 基类
 * 
 * 有输入和输出端口
 */
class ProcessingBlock : public Block {
public:
    ProcessingBlock(const BlockConfig& config, SharedBufferAllocator* allocator)
        : Block(config, allocator)
    {
        config_.type = BlockType::PROCESSING;
    }
};

}  // namespace multiqueue

