#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace multiqueue {

/**
 * @brief 队列角色枚举
 */
enum class QueueRole : uint8_t {
    PRODUCER = 0,       ///< 生产者角色：可以创建共享内存
    CONSUMER = 1        ///< 消费者角色：只能打开已存在的共享内存
};

/**
 * @brief 阻塞模式枚举
 */
enum class BlockingMode : uint8_t {
    BLOCKING = 0,       ///< 阻塞模式：队列满时生产者等待，队列空时消费者等待
    NON_BLOCKING = 1    ///< 非阻塞模式：队列满时覆盖旧数据，队列空时立即返回
};

/**
 * @brief 日志级别枚举
 */
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

/**
 * @brief 队列配置结构
 * 
 * 用于创建队列时指定各种参数
 */
struct QueueConfig {
    // ========== 基本配置 ==========
    
    /// 队列容量（元素数量）
    size_t capacity = 1024;
    
    /// 队列角色（生产者或消费者）
    QueueRole queue_role = QueueRole::PRODUCER;
    
    /// 阻塞模式
    BlockingMode blocking_mode = BlockingMode::BLOCKING;
    
    /// 超时时间（毫秒）
    uint32_t timeout_ms = 1000;
    
    /// 是否启用时间戳
    bool has_timestamp = false;
    
    // ========== 消费者打开重试配置 ==========
    
    /// 消费者打开共享内存的最大重试次数（0 表示不重试）
    uint32_t open_retry_count = 10;
    
    /// 消费者打开共享内存的重试间隔（毫秒）
    uint32_t open_retry_interval_ms = 100;
    
    // ========== 队列信息 ==========
    
    /// 队列名称
    std::string queue_name;
    
    /// 额外关联的队列名称（用于多队列同步）
    std::vector<std::string> extra_queue_names;
    
    /// 用户自定义元数据（最大 512 字节）
    std::string user_metadata;
    
    // ========== 异步线程配置 ==========
    
    /// 是否启用异步线程模式
    bool enable_async = false;
    
    /// 异步缓冲队列大小
    size_t async_buffer_size = 256;
    
    /// 异步工作线程数量
    int async_thread_count = 1;
    
    // ========== 性能优化配置 ==========
    
    /// 是否启用批量操作优化
    bool enable_batch_optimization = true;
    
    /// 是否启用自旋等待（短时间内自旋而不是立即阻塞）
    bool enable_spin_wait = true;
    
    /// 自旋等待的最大迭代次数
    uint32_t spin_wait_iterations = 1000;
    
    /**
     * @brief 默认构造函数
     */
    QueueConfig() = default;
    
    /**
     * @brief 带容量的构造函数
     * @param cap 队列容量
     */
    explicit QueueConfig(size_t cap) : capacity(cap) {}
    
    /**
     * @brief 验证配置是否有效
     * @return true 如果配置有效
     */
    bool is_valid() const {
        // 容量必须大于 0
        if (capacity == 0) {
            return false;
        }
        
        // 容量建议为 2 的幂次（性能优化）
        // 但不强制要求
        
        // 超时时间不能太大（防止溢出）
        if (timeout_ms > 3600000) {  // 最大 1 小时
            return false;
        }
        
        // 用户元数据不能超过 512 字节
        if (user_metadata.size() > 512) {
            return false;
        }
        
        // 异步线程数量必须合理
        if (enable_async) {
            if (async_thread_count <= 0 || async_thread_count > 64) {
                return false;
            }
            if (async_buffer_size == 0) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * @brief 检查容量是否为 2 的幂次
     * @return true 如果容量是 2 的幂次
     */
    bool is_power_of_two() const {
        return (capacity != 0) && ((capacity & (capacity - 1)) == 0);
    }
    
    /**
     * @brief 将容量向上取整到最近的 2 的幂次
     */
    void round_up_capacity_to_power_of_two() {
        if (is_power_of_two()) {
            return;
        }
        
        size_t power = 1;
        while (power < capacity) {
            power <<= 1;
        }
        capacity = power;
    }
};

/**
 * @brief 日志配置结构
 */
struct LogConfig {
    /// 日志文件路径
    std::string log_file = "multiqueue.log";
    
    /// 日志级别
    LogLevel level = LogLevel::INFO;
    
    /// 是否同时输出到控制台
    bool enable_console = true;
    
    /// 是否启用异步日志
    bool enable_async = false;
    
    /// 最大日志文件大小（字节）
    size_t max_file_size = 100 * 1024 * 1024;  // 100MB
    
    /// 最大备份文件数量
    int max_backup_files = 3;
    
    /// 异步日志缓冲区大小
    size_t async_buffer_size = 8192;
    
    /**
     * @brief 默认构造函数
     */
    LogConfig() = default;
};

/**
 * @brief 性能配置结构
 * 
 * 用于 Tracy Profiler 等性能监控工具的配置
 */
struct PerformanceConfig {
    /// 是否启用 Tracy Profiler
    bool enable_tracy = false;
    
    /// Tracy 服务器地址
    std::string tracy_server = "localhost";
    
    /// Tracy 服务器端口
    uint16_t tracy_port = 8086;
    
    /**
     * @brief 默认构造函数
     */
    PerformanceConfig() = default;
};

} // namespace multiqueue


