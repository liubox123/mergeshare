#pragma once

/**
 * @file multiqueue_shm.hpp
 * @brief MultiQueue-SHM 库统一包含头文件
 * 
 * 包含所有必要的头文件，使用库时只需包含此文件即可
 * 
 * @example
 * ```cpp
 * #include <multiqueue/multiqueue_shm.hpp>
 * 
 * int main() {
 *     multiqueue::QueueConfig config;
 *     config.capacity = 1024;
 *     
 *     multiqueue::RingQueue<int> queue("my_queue", config);
 *     queue.push(42);
 *     
 *     int value;
 *     if (queue.pop(value)) {
 *         std::cout << "Received: " << value << std::endl;
 *     }
 *     
 *     return 0;
 * }
 * ```
 */

// 核心头文件
#include "config.hpp"
#include "metadata.hpp"
#include "ring_queue.hpp"
#include "queue_manager.hpp"
#include "timestamp_sync.hpp"

// 版本信息
namespace multiqueue {

/// 主版本号
constexpr int VERSION_MAJOR = 0;

/// 次版本号
constexpr int VERSION_MINOR = 1;

/// 修订版本号
constexpr int VERSION_PATCH = 0;

/**
 * @brief 获取版本字符串
 * @return 版本字符串，格式为 "major.minor.patch"
 */
inline std::string get_version_string() {
    return std::to_string(VERSION_MAJOR) + "." +
           std::to_string(VERSION_MINOR) + "." +
           std::to_string(VERSION_PATCH);
}

/**
 * @brief 获取完整的版本信息
 * @return 完整的版本信息字符串
 */
inline std::string get_full_version_string() {
    return "MultiQueue-SHM v" + get_version_string() +
           " (built on " + __DATE__ + " " + __TIME__ + ")";
}

} // namespace multiqueue

/**
 * @mainpage MultiQueue-SHM 文档
 * 
 * @section intro_sec 简介
 * 
 * MultiQueue-SHM 是一个高性能的跨平台共享内存队列库，支持多生产者-多消费者模式。
 * 
 * @section features_sec 主要特性
 * 
 * - **多对多模式**: 支持多个生产者和多个消费者同时操作
 * - **无锁设计**: 基于 CAS 原子操作的无锁算法
 * - **类型安全**: C++ 模板支持任意 POD 类型
 * - **时间戳同步**: 多队列精确时间对齐
 * - **灵活配置**: 阻塞/非阻塞、异步线程等
 * - **跨平台**: Linux、macOS、Windows
 * - **双语言**: C++ 和 Python 可独立或协同使用
 * 
 * @section install_sec 安装
 * 
 * @subsection install_cpp C++ 库
 * 
 * ```bash
 * mkdir build && cd build
 * cmake .. -DBUILD_PYTHON_BINDING=OFF
 * cmake --build .
 * sudo cmake --install .
 * ```
 * 
 * @subsection install_python Python 绑定
 * 
 * ```bash
 * cd python-binding
 * pip install .
 * ```
 * 
 * @section usage_sec 使用示例
 * 
 * @subsection usage_basic 基本使用
 * 
 * ```cpp
 * #include <multiqueue/multiqueue_shm.hpp>
 * 
 * int main() {
 *     using namespace multiqueue;
 *     
 *     // 配置队列
 *     QueueConfig config;
 *     config.capacity = 1024;
 *     config.blocking_mode = BlockingMode::BLOCKING;
 *     
 *     // 创建队列
 *     RingQueue<int> queue("my_queue", config);
 *     
 *     // 写入数据
 *     for (int i = 0; i < 10; ++i) {
 *         queue.push(i);
 *     }
 *     
 *     // 读取数据
 *     int value;
 *     while (queue.pop(value)) {
 *         std::cout << "Received: " << value << std::endl;
 *     }
 *     
 *     return 0;
 * }
 * ```
 * 
 * @subsection usage_multiprocess 多进程使用
 * 
 * 生产者进程：
 * ```cpp
 * QueueConfig config;
 * config.capacity = 1024;
 * RingQueue<int> queue("shared_queue", config);
 * 
 * for (int i = 0; i < 1000; ++i) {
 *     queue.push(i);
 * }
 * ```
 * 
 * 消费者进程：
 * ```cpp
 * QueueConfig config;
 * config.capacity = 1024;
 * RingQueue<int> queue("shared_queue", config);
 * 
 * int value;
 * while (queue.pop(value)) {
 *     std::cout << "Received: " << value << std::endl;
 * }
 * ```
 * 
 * @section api_sec API 参考
 * 
 * - @ref multiqueue::RingQueue "RingQueue<T>" - 环形队列
 * - @ref multiqueue::QueueManager "QueueManager" - 队列管理器
 * - @ref multiqueue::MergedQueueView "MergedQueueView<T>" - 合并队列视图
 * - @ref multiqueue::QueueConfig "QueueConfig" - 队列配置
 * - @ref multiqueue::QueueStats "QueueStats" - 队列统计信息
 * 
 * @section performance_sec 性能
 * 
 * - 吞吐量: > 1M ops/sec
 * - 延迟: P99 < 1us
 * - 支持队列数: > 1000
 * 
 * @section license_sec 许可证
 * 
 * MIT License
 * 
 * @section contact_sec 联系方式
 * 
 * - GitHub: https://github.com/your-org/multiqueue-shm
 * - Issues: https://github.com/your-org/multiqueue-shm/issues
 */


