/**
 * @file message.hpp
 * @brief 消息定义
 * 
 * 定义控制消息、参数消息等
 */

#pragma once

#include "types.hpp"
#include "timestamp.hpp"
#include <string>
#include <variant>
#include <cstring>

namespace multiqueue {

/**
 * @brief 消息类型
 */
enum class MessageType : uint8_t {
    CONTROL = 0,      ///< 控制消息（启动、停止、暂停等）
    PARAMETER = 1,    ///< 参数消息（设置参数）
    STATUS = 2,       ///< 状态消息（Block 状态更新）
    ERROR = 3         ///< 错误消息
};

/**
 * @brief 控制命令
 */
enum class ControlCommand : uint8_t {
    START = 0,        ///< 启动
    STOP = 1,         ///< 停止
    PAUSE = 2,        ///< 暂停
    RESUME = 3        ///< 恢复
};

/**
 * @brief 消息头部
 */
struct MessageHeader {
    MessageType type;              ///< 消息类型
    BlockId source_block;          ///< 源 Block ID
    BlockId target_block;          ///< 目标 Block ID（0 表示广播）
    Timestamp timestamp;           ///< 时间戳
    uint32_t payload_size;         ///< 负载大小
    
    MessageHeader()
        : type(MessageType::CONTROL)
        , source_block(INVALID_BLOCK_ID)
        , target_block(INVALID_BLOCK_ID)
        , timestamp()
        , payload_size(0)
    {}
};

/**
 * @brief 控制消息负载
 */
struct ControlMessagePayload {
    ControlCommand command;        ///< 控制命令
    char data[64];                 ///< 附加数据
    
    ControlMessagePayload()
        : command(ControlCommand::START)
    {
        memset(data, 0, sizeof(data));
    }
};

/**
 * @brief 参数消息负载
 */
struct ParameterMessagePayload {
    char param_name[32];           ///< 参数名称
    char param_value[64];          ///< 参数值（字符串形式）
    
    ParameterMessagePayload()
    {
        memset(param_name, 0, sizeof(param_name));
        memset(param_value, 0, sizeof(param_value));
    }
};

/**
 * @brief 状态消息负载
 */
struct StatusMessagePayload {
    BlockState state;              ///< Block 状态
    char status_text[96];          ///< 状态文本
    
    StatusMessagePayload()
        : state(BlockState::CREATED)
    {
        memset(status_text, 0, sizeof(status_text));
    }
};

/**
 * @brief 错误消息负载
 */
struct ErrorMessagePayload {
    uint32_t error_code;           ///< 错误码
    char error_message[96];        ///< 错误消息
    
    ErrorMessagePayload()
        : error_code(0)
    {
        memset(error_message, 0, sizeof(error_message));
    }
};

/**
 * @brief 消息（简化版本，进程本地使用）
 */
class Message {
public:
    using Payload = std::variant<
        ControlMessagePayload,
        ParameterMessagePayload,
        StatusMessagePayload,
        ErrorMessagePayload
    >;
    
    Message()
        : header_()
        , payload_()
    {}
    
    Message(MessageType type, BlockId source, BlockId target = INVALID_BLOCK_ID)
        : header_()
        , payload_()
    {
        header_.type = type;
        header_.source_block = source;
        header_.target_block = target;
        header_.timestamp = Timestamp::now();
    }
    
    const MessageHeader& header() const { return header_; }
    MessageHeader& header() { return header_; }
    
    const Payload& payload() const { return payload_; }
    Payload& payload() { return payload_; }
    
    template<typename T>
    void set_payload(const T& p) {
        payload_ = p;
    }
    
    template<typename T>
    const T* get_payload() const {
        return std::get_if<T>(&payload_);
    }
    
private:
    MessageHeader header_;
    Payload payload_;
};

}  // namespace multiqueue

