# Logger 组件

## 概述

多进程安全的日志组件，支持多个进程同时写入同一日志文件，避免数据竞争和日志混乱。

## 特性

- ✅ **多进程安全**: 使用文件锁确保原子写入
- ✅ **多级日志**: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- ✅ **格式化输出**: 支持 printf 风格和 fmt 库格式化
- ✅ **自动包含上下文**: 文件名、行号、函数名、进程ID、线程ID
- ✅ **日志滚动**: 按大小或时间滚动
- ✅ **异步写入** (可选): 批量写入减少 I/O 开销
- ✅ **跨平台**: Linux, macOS, Windows

## 使用示例

### 基本使用

```cpp
#include "mp_logger.hpp"

int main() {
    // 初始化日志系统
    multiqueue::logger::MPLogger::init("app.log", multiqueue::logger::LogLevel::INFO);
    
    // 使用宏记录日志
    LOG_INFO("Application started");
    LOG_WARN("This is a warning message");
    LOG_ERROR("Error occurred: {}", error_code);
    
    // 带变量的日志
    int count = 42;
    std::string name = "test";
    LOG_DEBUG("Processing {} items for {}", count, name);
    
    return 0;
}
```

### 多进程场景

```cpp
// 进程 A
#include "mp_logger.hpp"

void process_a() {
    multiqueue::logger::MPLogger::init("shared.log", multiqueue::logger::LogLevel::INFO);
    for (int i = 0; i < 1000; ++i) {
        LOG_INFO("Process A: iteration {}", i);
    }
}

// 进程 B
void process_b() {
    multiqueue::logger::MPLogger::init("shared.log", multiqueue::logger::LogLevel::INFO);
    for (int i = 0; i < 1000; ++i) {
        LOG_INFO("Process B: iteration {}", i);
    }
}

// 两个进程可以安全地同时写入 shared.log
```

### 配置选项

```cpp
#include "mp_logger.hpp"

int main() {
    using namespace multiqueue::logger;
    
    // 配置日志
    LogConfig config;
    config.log_file = "app.log";
    config.level = LogLevel::DEBUG;
    config.enable_console = true;        // 同时输出到控制台
    config.enable_async = true;          // 启用异步写入
    config.max_file_size = 10 * 1024 * 1024;  // 10MB 滚动
    config.max_backup_files = 5;         // 保留 5 个备份
    
    MPLogger::init(config);
    
    LOG_INFO("Logger initialized with custom config");
    
    return 0;
}
```

## 日志格式

默认日志格式：
```
[2025-11-18 14:30:45.123] [INFO] [process_id:12345] [thread_id:67890] [main.cpp:42 main] Application started
```

格式说明：
- `[时间戳]`: 精确到毫秒
- `[日志级别]`: TRACE/DEBUG/INFO/WARN/ERROR/FATAL
- `[process_id:xxx]`: 进程 ID
- `[thread_id:xxx]`: 线程 ID
- `[文件:行号 函数名]`: 代码位置
- 日志消息内容

## 性能考虑

### 同步模式
- 每次日志立即写入磁盘
- 适合低频日志，确保不丢失
- 性能: ~10,000 logs/sec

### 异步模式
- 日志先写入内存缓冲区
- 后台线程批量刷新到磁盘
- 适合高频日志，更高性能
- 性能: ~100,000 logs/sec
- **注意**: 程序崩溃可能丢失部分日志

## API 参考

### 日志级别

```cpp
enum class LogLevel {
    TRACE,   // 最详细，用于追踪
    DEBUG,   // 调试信息
    INFO,    // 一般信息
    WARN,    // 警告
    ERROR,   // 错误
    FATAL    // 致命错误
};
```

### 宏定义

```cpp
LOG_TRACE(fmt, ...)   // 追踪日志
LOG_DEBUG(fmt, ...)   // 调试日志
LOG_INFO(fmt, ...)    // 信息日志
LOG_WARN(fmt, ...)    // 警告日志
LOG_ERROR(fmt, ...)   // 错误日志
LOG_FATAL(fmt, ...)   // 致命错误日志
```

### 配置类

```cpp
struct LogConfig {
    std::string log_file = "app.log";     // 日志文件路径
    LogLevel level = LogLevel::INFO;      // 日志级别
    bool enable_console = true;           // 是否输出到控制台
    bool enable_async = false;            // 是否启用异步
    size_t max_file_size = 100 * 1024 * 1024;  // 最大文件大小（字节）
    int max_backup_files = 3;             // 最大备份文件数
    size_t async_buffer_size = 8192;      // 异步缓冲区大小
};
```

### MPLogger 类

```cpp
class MPLogger {
public:
    // 初始化日志系统（简化版）
    static void init(const std::string& log_file, LogLevel level);
    
    // 初始化日志系统（完整配置）
    static void init(const LogConfig& config);
    
    // 获取单例
    static MPLogger& instance();
    
    // 记录日志
    void log(LogLevel level, const char* file, int line, 
             const char* func, const std::string& message);
    
    // 设置日志级别
    void set_level(LogLevel level);
    
    // 刷新缓冲区
    void flush();
    
    // 关闭日志系统
    void shutdown();
};
```

## 实现细节

### 文件锁机制

- **Linux/macOS**: 使用 `flock()` 实现文件锁
- **Windows**: 使用 `LockFile()` 实现文件锁

### 线程安全

- 单例模式使用 `std::call_once` 保证线程安全初始化
- 异步模式使用互斥锁保护缓冲区
- 文件写入使用文件锁保证多进程安全

### 日志滚动

当日志文件超过配置的大小时：
1. 关闭当前日志文件
2. 重命名为 `app.log.1`
3. 旧的备份文件递增 (`app.log.1` -> `app.log.2`)
4. 删除最旧的备份（超过 `max_backup_files`）
5. 创建新的 `app.log`

## 依赖

- C++17 标准库
- 文件系统支持 (`<filesystem>`)
- 线程支持 (`<thread>`, `<mutex>`)

## 注意事项

1. **异步模式注意事项**
   - 程序退出前调用 `flush()` 或 `shutdown()` 确保日志写入
   - 程序崩溃可能丢失缓冲区中的日志

2. **性能优化建议**
   - 生产环境建议使用 INFO 级别
   - 调试时使用 DEBUG 或 TRACE 级别
   - 高频日志场景启用异步模式

3. **磁盘空间管理**
   - 合理设置 `max_file_size` 和 `max_backup_files`
   - 定期清理旧日志文件

## 测试

参见 `tests/cpp/test_logger.cpp` 中的单元测试和多进程测试。

## 维护者

Logger 组件维护者: 开发者

---

**版本**: 0.1.0  
**最后更新**: 2025-11-18


