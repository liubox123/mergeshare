/**
 * @file types.hpp
 * @brief 基础类型定义
 * 
 * 定义了整个框架使用的基础类型、常量和枚举
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

namespace multiqueue {

// ===== 基础类型定义 =====

/// Process ID 类型
using ProcessId = uint32_t;

/// Block ID 类型
using BlockId = uint32_t;

/// Port ID 类型
using PortId = uint32_t;

/// Buffer ID 类型
using BufferId = uint64_t;

/// Pool ID 类型
using PoolId = uint32_t;

/// Connection ID 类型
using ConnectionId = uint64_t;

/// Timestamp 纳秒计数类型
using TimestampNs = uint64_t;

// ===== 常量定义 =====

/// 无效的 Process ID
constexpr ProcessId INVALID_PROCESS_ID = 0;

/// 无效的 Block ID
constexpr BlockId INVALID_BLOCK_ID = 0;

/// 无效的 Port ID
constexpr PortId INVALID_PORT_ID = 0;

/// 无效的 Buffer ID
constexpr BufferId INVALID_BUFFER_ID = 0;

/// 无效的 Pool ID
constexpr PoolId INVALID_POOL_ID = std::numeric_limits<PoolId>::max();

/// 无效的 Connection ID
constexpr ConnectionId INVALID_CONNECTION_ID = 0;

// ===== 容量限制 =====

/// 最大进程数
constexpr size_t MAX_PROCESSES = 64;

/// 最大 Block 数
constexpr size_t MAX_BLOCKS = 256;

/// 最大端口数（每个 Block）
constexpr size_t MAX_PORTS_PER_BLOCK = 16;

/// 最大连接数
constexpr size_t MAX_CONNECTIONS = 1024;

/// 最大 Buffer 数
constexpr size_t MAX_BUFFERS = 4096;

/// 最大 Buffer Pool 数
constexpr size_t MAX_BUFFER_POOLS = 8;

/// 端口队列默认大小
constexpr size_t DEFAULT_PORT_QUEUE_SIZE = 64;

/// 最大端口队列大小
constexpr size_t MAX_PORT_QUEUE_SIZE = 1024;

// ===== 共享内存名称 =====

/// 全局注册表共享内存名称
constexpr const char* GLOBAL_REGISTRY_SHM_NAME = "mqshm_global_registry";

/// Buffer Pool 共享内存名称前缀
constexpr const char* BUFFER_POOL_SHM_PREFIX = "mqshm_pool_";

/// Port Queue 共享内存名称前缀
constexpr const char* PORT_QUEUE_SHM_PREFIX = "mqshm_port_";

// ===== 缓存行大小 =====

/// 缓存行大小（用于对齐，避免 false sharing）
constexpr size_t CACHE_LINE_SIZE = 64;

// ===== 超时和重试 =====

/// 默认超时时间（毫秒）
constexpr uint32_t DEFAULT_TIMEOUT_MS = 1000;

/// 心跳间隔（毫秒）
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;

/// 进程死亡检测超时（毫秒）
constexpr uint32_t DEAD_PROCESS_TIMEOUT_MS = 3000;

/// 共享内存打开重试次数
constexpr uint32_t SHM_OPEN_RETRY_COUNT = 10;

/// 共享内存打开重试间隔（毫秒）
constexpr uint32_t SHM_OPEN_RETRY_INTERVAL_MS = 100;

// ===== 枚举类型 =====

/**
 * @brief Block 类型
 */
enum class BlockType : uint8_t {
    SOURCE = 0,       ///< 数据源 Block（只有输出端口）
    PROCESSING = 1,   ///< 处理 Block（有输入和输出端口）
    SINK = 2          ///< 数据接收 Block（只有输入端口）
};

/**
 * @brief 端口类型
 */
enum class PortType : uint8_t {
    INPUT = 0,   ///< 输入端口
    OUTPUT = 1   ///< 输出端口
};

/**
 * @brief 同步模式
 */
enum class SyncMode : uint8_t {
    ASYNC = 0,   ///< 异步模式（自由流，无时间戳要求）
    SYNC = 1     ///< 同步模式（基于时间戳对齐）
};

/**
 * @brief 时间戳对齐策略
 */
enum class AlignmentPolicy : uint8_t {
    NEAREST = 0,        ///< 选择最接近的样本
    INTERPOLATE = 1,    ///< 线性插值
    DROP = 2,           ///< 丢弃不对齐的样本
    HOLD = 3            ///< 保持上一个样本值
};

/**
 * @brief Block 状态
 */
enum class BlockState : uint8_t {
    CREATED = 0,     ///< 已创建，未注册
    REGISTERED = 1,  ///< 已注册到 Runtime
    READY = 2,       ///< 准备就绪，可以运行
    RUNNING = 3,     ///< 正在运行
    PAUSED = 4,      ///< 已暂停
    STOPPED = 5,     ///< 已停止
    ERROR = 6        ///< 错误状态
};

/**
 * @brief 进程状态
 */
enum class ProcessState : uint8_t {
    STARTING = 0,   ///< 启动中
    RUNNING = 1,    ///< 运行中
    STOPPING = 2,   ///< 停止中
    STOPPED = 3,    ///< 已停止
    DEAD = 4        ///< 已死亡（崩溃）
};

/**
 * @brief Work 方法返回值
 */
enum class WorkResult : uint8_t {
    OK = 0,           ///< 成功处理
    DONE = 1,         ///< 处理完成，无更多数据
    INSUFFICIENT_INPUT = 2,   ///< 输入数据不足
    INSUFFICIENT_OUTPUT = 3,  ///< 输出队列已满
    ERROR = 4         ///< 发生错误
};

/**
 * @brief 日志级别
 */
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
    OFF = 6
};

// ===== 魔数和版本 =====

/// 共享内存魔数（用于验证）
constexpr uint32_t SHM_MAGIC_NUMBER = 0x4D515348;  // "MQSH"

/// 框架版本号
constexpr uint32_t FRAMEWORK_VERSION = 0x00020000;  // v2.0.0

// ===== 平台检测 =====

#ifdef _WIN32
    #define MQSHM_PLATFORM_WINDOWS
#elif defined(__APPLE__)
    #define MQSHM_PLATFORM_MACOS
#elif defined(__linux__)
    #define MQSHM_PLATFORM_LINUX
#else
    #error "Unsupported platform"
#endif

// ===== 编译器属性 =====

#if defined(__GNUC__) || defined(__clang__)
    #define MQSHM_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define MQSHM_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define MQSHM_ALIGNED(n)  __attribute__((aligned(n)))
    #define MQSHM_PACKED      __attribute__((packed))
#elif defined(_MSC_VER)
    #define MQSHM_LIKELY(x)   (x)
    #define MQSHM_UNLIKELY(x) (x)
    #define MQSHM_ALIGNED(n)  __declspec(align(n))
    #define MQSHM_PACKED
#else
    #define MQSHM_LIKELY(x)   (x)
    #define MQSHM_UNLIKELY(x) (x)
    #define MQSHM_ALIGNED(n)
    #define MQSHM_PACKED
#endif

}  // namespace multiqueue
