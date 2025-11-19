# Core Library - 核心共享内存队列库

## 概述

Core Library 是 multiqueue-shm 项目的核心组件，提供高性能的多生产者-多消费者（MPMC）共享内存队列实现。

## 特性

- ✅ **Header-Only**: 纯头文件实现，方便集成
- ✅ **类型模板**: 支持任意 POD 类型和自定义结构
- ✅ **无锁设计**: 基于 CAS 原子操作的无锁算法
- ✅ **多对多**: 支持多个生产者和多个消费者同时操作
- ✅ **时间戳支持**: 内置时间戳，支持多队列时间同步
- ✅ **灵活模式**: 
  - 阻塞模式: 队列满时生产者等待
  - 非阻塞模式: 队列满时覆盖旧数据
- ✅ **异步线程** (可选): 独立线程消费数据，避免阻塞主线程
- ✅ **跨平台**: Linux, macOS, Windows

## 核心组件

### 1. RingQueue<T>
环形队列模板类，支持多生产者-多消费者模式。

**特点**:
- 基于统一内存池和数组实现
- 使用原子变量管理读写偏移
- 支持时间戳和序列号
- 内存布局：元数据头 + 控制块 + 数据区

### 2. QueueManager
队列管理器，负责创建、打开、删除和管理多个队列。

**特点**:
- 队列注册表
- 统一的生命周期管理
- 多队列合并和同步
- 统计信息收集

### 3. TimestampSynchronizer
时间戳同步器，用于多个队列按时间戳合并。

**特点**:
- 多路归并排序算法
- 超时机制
- 无时间戳队列的处理

### 4. AsyncQueueWrapper
异步队列包装器，提供独立线程消费数据的能力。

**特点**:
- 固定长度缓冲队列
- 工作线程管理
- 用户回调机制

## 目录结构

```
core/
├── include/                    # 公共头文件
│   ├── config.hpp             # 配置选项
│   ├── metadata.hpp           # 元数据结构
│   ├── ring_queue.hpp         # 环形队列模板类
│   ├── queue_manager.hpp      # 队列管理器
│   ├── timestamp_sync.hpp     # 时间戳同步器
│   ├── async_wrapper.hpp      # 异步包装器
│   └── multiqueue_shm.hpp     # 统一包含头文件
├── src/                       # 内部实现（如有需要）
├── CMakeLists.txt            # CMake 配置
└── README.md                 # 本文件
```

## 快速开始

### 基本使用

```cpp
#include "multiqueue_shm.hpp"
#include <iostream>
#include <string>

using namespace multiqueue;

// 定义数据结构
struct SensorData {
    uint64_t timestamp;
    float temperature;
    float humidity;
    int sensor_id;
};

int main() {
    // 配置队列
    QueueConfig config;
    config.capacity = 1024;
    config.blocking_mode = BlockingMode::BLOCKING;
    config.timeout_ms = 1000;
    config.has_timestamp = true;
    
    // 创建队列
    RingQueue<SensorData> queue("sensor_queue", config);
    
    // 生产者：写入数据
    SensorData data;
    data.timestamp = current_timestamp();
    data.temperature = 25.5;
    data.humidity = 60.0;
    data.sensor_id = 1;
    
    if (queue.push(data)) {
        std::cout << "Data written successfully" << std::endl;
    }
    
    // 消费者：读取数据
    SensorData received;
    if (queue.pop(received)) {
        std::cout << "Temperature: " << received.temperature << "°C" << std::endl;
        std::cout << "Humidity: " << received.humidity << "%" << std::endl;
    }
    
    return 0;
}
```

### 多进程场景

```cpp
// 生产者进程
#include "multiqueue_shm.hpp"

void producer_process() {
    QueueConfig config;
    config.capacity = 1024;
    RingQueue<int> queue("shared_queue", config);
    
    for (int i = 0; i < 10000; ++i) {
        queue.push(i);
    }
}

// 消费者进程
void consumer_process() {
    QueueConfig config;
    config.capacity = 1024;
    RingQueue<int> queue("shared_queue", config);  // 打开现有队列
    
    int data;
    while (queue.pop(data)) {
        std::cout << "Received: " << data << std::endl;
    }
}
```

### 多队列合并（时间戳同步）

```cpp
#include "multiqueue_shm.hpp"

int main() {
    QueueManager manager;
    
    // 创建多个队列
    auto queue1 = manager.create_or_open<SensorData>("sensor1", 1024, config);
    auto queue2 = manager.create_or_open<SensorData>("sensor2", 1024, config);
    auto queue3 = manager.create_or_open<SensorData>("sensor3", 1024, config);
    
    // 合并队列（按时间戳排序）
    std::vector<std::string> queue_names = {"sensor1", "sensor2", "sensor3"};
    auto merged = manager.merge_queues<SensorData>(queue_names, 100 /*timeout_ms*/);
    
    // 从合并视图中读取数据（自动按时间戳排序）
    SensorData data;
    while (merged.next(data)) {
        std::cout << "Timestamp: " << data.timestamp 
                  << ", Sensor: " << data.sensor_id 
                  << ", Temp: " << data.temperature << std::endl;
    }
    
    return 0;
}
```

### 异步线程模式

```cpp
#include "multiqueue_shm.hpp"

int main() {
    QueueConfig config;
    config.capacity = 1024;
    config.enable_async = true;          // 启用异步线程
    config.async_buffer_size = 256;      // 缓冲队列大小
    
    // 创建异步队列
    AsyncQueueWrapper<SensorData> async_queue("sensor_queue", config);
    
    // 设置回调函数
    async_queue.set_callback([](const SensorData& data) {
        // 在独立线程中处理数据
        std::cout << "Processing sensor " << data.sensor_id 
                  << " data in background thread" << std::endl;
    });
    
    // 启动异步线程
    async_queue.start();
    
    // 主线程继续其他工作...
    
    // 停止异步线程
    async_queue.stop();
    
    return 0;
}
```

## 配置选项

### QueueConfig

```cpp
struct QueueConfig {
    // 队列容量
    size_t capacity = 1024;
    
    // 阻塞模式
    BlockingMode blocking_mode = BlockingMode::BLOCKING;
    
    // 超时时间（毫秒）
    uint32_t timeout_ms = 1000;
    
    // 是否带时间戳
    bool has_timestamp = false;
    
    // 队列名称
    std::string queue_name;
    
    // 额外关联的队列名称（用于多队列同步）
    std::vector<std::string> extra_queue_names;
    
    // 用户自定义元数据
    std::string user_metadata;
    
    // 异步线程相关
    bool enable_async = false;          // 是否启用异步线程
    size_t async_buffer_size = 256;     // 异步缓冲队列大小
    int async_thread_count = 1;         // 异步工作线程数量
};
```

### BlockingMode

```cpp
enum class BlockingMode {
    BLOCKING,       // 阻塞模式：队列满时等待
    NON_BLOCKING    // 非阻塞模式：队列满时覆盖旧数据
};
```

## API 参考

### RingQueue<T>

#### 构造函数
```cpp
RingQueue(const std::string& name, const QueueConfig& config);
```

#### 生产者接口
```cpp
// 阻塞写入（直到成功或超时）
bool push(const T& data, uint64_t timestamp = 0);

// 尝试写入（不阻塞）
bool try_push(const T& data, uint64_t timestamp = 0);

// 带超时的写入
bool push_with_timeout(const T& data, uint32_t timeout_ms, uint64_t timestamp = 0);

// 批量写入
size_t push_batch(const T* data, size_t count);
```

#### 消费者接口
```cpp
// 阻塞读取（直到成功或超时）
bool pop(T& data, uint64_t* timestamp = nullptr);

// 尝试读取（不阻塞）
bool try_pop(T& data, uint64_t* timestamp = nullptr);

// 带超时的读取
bool pop_with_timeout(T& data, uint32_t timeout_ms, uint64_t* timestamp = nullptr);

// 批量读取
size_t pop_batch(T* data, size_t max_count);

// 查看队首元素（不移除）
bool peek(T& data, uint64_t* timestamp = nullptr);
```

#### 查询接口
```cpp
// 当前队列大小
size_t size() const;

// 是否为空
bool empty() const;

// 是否已满
bool full() const;

// 获取容量
size_t capacity() const;

// 获取元数据
const QueueMetadata& metadata() const;

// 获取统计信息
QueueStats get_stats() const;
```

### QueueStats

```cpp
struct QueueStats {
    uint64_t total_pushed;       // 总写入数量
    uint64_t total_popped;       // 总读取数量
    uint64_t overwrite_count;    // 覆盖次数（非阻塞模式）
    uint32_t producer_count;     // 当前生产者数量
    uint32_t consumer_count;     // 当前消费者数量
    size_t current_size;         // 当前队列大小
    uint64_t created_at;         // 创建时间
    uint64_t last_write_time;    // 最后写入时间
    uint64_t last_read_time;     // 最后读取时间
};
```

## 性能优化

### 1. 缓存行对齐
关键变量按 64 字节对齐，避免伪共享（false sharing）。

### 2. 无锁算法
使用 CAS 原子操作，避免锁竞争。

### 3. 内存预分配
队列创建时一次性分配所有内存。

### 4. 批量操作
支持批量读写，减少系统调用开销。

## 注意事项

### 1. 数据类型要求
- 必须是 POD 类型（Plain Old Data）或 trivially copyable
- 不支持包含指针、引用的复杂类型
- 如需传输复杂数据，建议序列化后传输

### 2. 队列容量选择
- 容量应为 2 的幂次，优化取模运算
- 根据数据大小和延迟要求选择容量
- 建议：低延迟场景使用较小容量（256-1024），高吞吐场景使用较大容量（4096-16384）

### 3. 阻塞模式选择
- **阻塞模式**: 适合数据不能丢失的场景
- **非阻塞模式**: 适合实时性要求高、允许丢失旧数据的场景

### 4. 时间戳同步注意事项
- 所有队列必须使用相同的时钟源
- 时间戳单位应统一（建议使用纳秒）
- 无时间戳的队列不能参与同步合并

## 依赖

- C++17 标准库
- Boost.Interprocess
- 线程支持（std::atomic, std::thread）

## 测试

参见 `tests/cpp/test_ring_queue.cpp` 中的详细测试用例。

## 维护者

Core Library 维护者: 架构师

---

**版本**: 0.1.0  
**最后更新**: 2025-11-18

