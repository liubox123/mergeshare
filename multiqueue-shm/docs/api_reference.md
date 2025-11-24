# API 参考文档

## 目录

1. [核心类型](#核心类型)
2. [RingQueue<T>](#ringqueuet)
3. [QueueManager](#queuemanager)
4. [AsyncQueueWrapper<T>](#asyncqueuewrappert)
5. [辅助类型](#辅助类型)
6. [错误处理](#错误处理)
7. [Python API](#python-api)

---

## 核心类型

### QueueConfig

队列配置结构，用于创建队列时指定参数。

```cpp
struct QueueConfig {
    // 基本配置
    size_t capacity = 1024;                          // 队列容量（元素数量）
    BlockingMode blocking_mode = BlockingMode::BLOCKING;  // 阻塞模式
    uint32_t timeout_ms = 1000;                      // 超时时间（毫秒）
    bool has_timestamp = false;                      // 是否带时间戳
    
    // 队列信息
    std::string queue_name;                          // 队列名称
    std::vector<std::string> extra_queue_names;      // 关联队列名称
    std::string user_metadata;                       // 用户元数据
    
    // 异步线程配置
    bool enable_async = false;                       // 启用异步线程
    size_t async_buffer_size = 256;                  // 异步缓冲大小
    int async_thread_count = 1;                      // 工作线程数量
};
```

### BlockingMode

阻塞模式枚举。

```cpp
enum class BlockingMode {
    BLOCKING,       // 阻塞模式：队列满时生产者阻塞，队列空时消费者阻塞
    NON_BLOCKING    // 非阻塞模式：队列满时覆盖旧数据，队列空时立即返回
};
```

### QueueMetadata

队列元数据结构（存储在共享内存头部）。

```cpp
struct QueueMetadata {
    // 标识信息
    uint64_t magic_number;              // 魔数: 0x4D515348_4D454D00
    uint32_t version;                   // 版本号
    
    // 队列配置
    size_t element_size;                // 单个元素大小（字节）
    size_t capacity;                    // 队列容量
    bool has_timestamp;                 // 是否带时间戳
    BlockingMode blocking_mode;         // 阻塞模式
    uint32_t timeout_ms;                // 超时时间
    
    // 队列信息
    char queue_name[64];                // 队列名称
    char extra_queue_names[256];        // 额外队列名称
    char user_metadata[512];            // 用户元数据
    uint64_t created_at;                // 创建时间戳
    
    // 验证方法
    bool is_valid() const;
    void initialize(const QueueConfig& config);
};
```

### QueueStats

队列统计信息。

```cpp
struct QueueStats {
    uint64_t total_pushed;              // 总写入数量
    uint64_t total_popped;              // 总读取数量
    uint64_t overwrite_count;           // 覆盖次数（非阻塞模式）
    uint32_t producer_count;            // 当前生产者数量
    uint32_t consumer_count;            // 当前消费者数量
    size_t current_size;                // 当前队列大小
    size_t capacity;                    // 队列容量
    uint64_t created_at;                // 创建时间
    uint64_t last_write_time;           // 最后写入时间
    uint64_t last_read_time;            // 最后读取时间
};
```

---

## RingQueue\<T\>

环形队列模板类，核心的共享内存队列实现。

### 构造函数

```cpp
RingQueue(const std::string& name, const QueueConfig& config);
```

**参数**:
- `name`: 队列名称（共享内存段名称）
- `config`: 队列配置

**异常**:
- `std::runtime_error`: 共享内存创建失败
- `std::invalid_argument`: 配置参数无效

**示例**:
```cpp
QueueConfig config;
config.capacity = 1024;
config.blocking_mode = BlockingMode::BLOCKING;
RingQueue<int> queue("my_queue", config);
```

---

### 生产者接口

#### push

```cpp
bool push(const T& data, uint64_t timestamp = 0);
```

阻塞写入数据（根据配置的超时时间）。

**参数**:
- `data`: 要写入的数据
- `timestamp`: 时间戳（可选，如果队列不使用时间戳则忽略）

**返回值**:
- `true`: 写入成功
- `false`: 超时或失败

**线程安全**: 是（支持多生产者）

**示例**:
```cpp
int data = 42;
if (queue.push(data)) {
    std::cout << "Data written" << std::endl;
} else {
    std::cout << "Timeout or error" << std::endl;
}
```

---

#### try_push

```cpp
bool try_push(const T& data, uint64_t timestamp = 0);
```

尝试写入数据（不阻塞）。

**参数**: 同 `push`

**返回值**:
- `true`: 写入成功
- `false`: 队列满或失败

**线程安全**: 是

---

#### push_with_timeout

```cpp
bool push_with_timeout(const T& data, uint32_t timeout_ms, uint64_t timestamp = 0);
```

带自定义超时时间的写入。

**参数**:
- `data`: 要写入的数据
- `timeout_ms`: 超时时间（毫秒）
- `timestamp`: 时间戳（可选）

**返回值**: 同 `push`

**线程安全**: 是

---

#### push_batch

```cpp
size_t push_batch(const T* data, size_t count);
```

批量写入数据。

**参数**:
- `data`: 数据数组指针
- `count`: 数据数量

**返回值**: 实际写入的数据数量

**线程安全**: 是

**注意**: 批量操作在阻塞模式下可能只写入部分数据

---

### 消费者接口

#### pop

```cpp
bool pop(T& data, uint64_t* timestamp = nullptr);
```

阻塞读取数据（根据配置的超时时间）。

**参数**:
- `data`: 输出参数，存储读取的数据
- `timestamp`: 输出参数，存储时间戳（可选）

**返回值**:
- `true`: 读取成功
- `false`: 超时或队列已空

**线程安全**: 是（支持多消费者）

**示例**:
```cpp
int data;
uint64_t timestamp;
if (queue.pop(data, &timestamp)) {
    std::cout << "Received: " << data << " at " << timestamp << std::endl;
}
```

---

#### try_pop

```cpp
bool try_pop(T& data, uint64_t* timestamp = nullptr);
```

尝试读取数据（不阻塞）。

**参数**: 同 `pop`

**返回值**:
- `true`: 读取成功
- `false`: 队列空

**线程安全**: 是

---

#### pop_with_timeout

```cpp
bool pop_with_timeout(T& data, uint32_t timeout_ms, uint64_t* timestamp = nullptr);
```

带自定义超时时间的读取。

**参数**:
- `data`: 输出参数
- `timeout_ms`: 超时时间（毫秒）
- `timestamp`: 输出参数（可选）

**返回值**: 同 `pop`

**线程安全**: 是

---

#### pop_batch

```cpp
size_t pop_batch(T* data, size_t max_count);
```

批量读取数据。

**参数**:
- `data`: 数据数组指针
- `max_count`: 最多读取的数据数量

**返回值**: 实际读取的数据数量

**线程安全**: 是

---

#### peek

```cpp
bool peek(T& data, uint64_t* timestamp = nullptr);
```

查看队首元素（不移除）。

**参数**: 同 `pop`

**返回值**:
- `true`: 成功查看
- `false`: 队列空

**线程安全**: 是

**注意**: 在多消费者场景下，`peek` 之后元素可能被其他消费者取走

---

### 查询接口

#### size

```cpp
size_t size() const;
```

获取当前队列中的元素数量。

**返回值**: 元素数量

**线程安全**: 是（但返回值可能立即过时）

---

#### empty

```cpp
bool empty() const;
```

检查队列是否为空。

**返回值**: `true` 如果队列空

**线程安全**: 是

---

#### full

```cpp
bool full() const;
```

检查队列是否已满。

**返回值**: `true` 如果队列满

**线程安全**: 是

---

#### capacity

```cpp
size_t capacity() const;
```

获取队列容量。

**返回值**: 队列容量

**线程安全**: 是

---

#### metadata

```cpp
const QueueMetadata& metadata() const;
```

获取队列元数据。

**返回值**: 元数据引用

**线程安全**: 是（只读）

---

#### get_stats

```cpp
QueueStats get_stats() const;
```

获取队列统计信息。

**返回值**: 统计信息结构

**线程安全**: 是

---

## QueueManager

队列管理器，用于管理多个队列。

### 构造函数

```cpp
QueueManager();
```

创建队列管理器实例。

---

### create_or_open

```cpp
template<typename T>
std::shared_ptr<RingQueue<T>> create_or_open(
    const std::string& name,
    const QueueConfig& config
);
```

创建或打开队列。

**参数**:
- `name`: 队列名称
- `config`: 队列配置

**返回值**: 队列的共享指针

**异常**: `std::runtime_error` 如果创建/打开失败

**示例**:
```cpp
QueueManager manager;
auto queue = manager.create_or_open<int>("my_queue", config);
```

---

### merge_queues

```cpp
template<typename T>
MergedQueueView<T> merge_queues(
    const std::vector<std::string>& queue_names,
    uint32_t sync_timeout_ms
);
```

合并多个队列（按时间戳同步）。

**参数**:
- `queue_names`: 队列名称列表
- `sync_timeout_ms`: 同步超时时间

**返回值**: 合并队列视图

**异常**: `std::invalid_argument` 如果队列不存在或配置不匹配

**示例**:
```cpp
std::vector<std::string> names = {"queue1", "queue2", "queue3"};
auto merged = manager.merge_queues<SensorData>(names, 100);

SensorData data;
while (merged.next(data)) {
    // 数据按时间戳排序
}
```

---

### get_stats

```cpp
QueueStats get_stats(const std::string& name);
```

获取指定队列的统计信息。

**参数**: `name` - 队列名称

**返回值**: 统计信息

---

### remove_queue

```cpp
bool remove_queue(const std::string& name);
```

删除队列（清除共享内存）。

**参数**: `name` - 队列名称

**返回值**: `true` 如果成功删除

**注意**: 所有进程都关闭队列后才能删除

---

## AsyncQueueWrapper\<T\>

异步队列包装器，提供独立线程消费数据的能力。

### 构造函数

```cpp
AsyncQueueWrapper(const std::string& name, const QueueConfig& config);
```

创建异步队列包装器。

**注意**: `config.enable_async` 应设置为 `true`

---

### set_callback

```cpp
void set_callback(std::function<void(const T&)> callback);
```

设置数据处理回调函数。

**参数**: `callback` - 回调函数（在工作线程中调用）

---

### start

```cpp
void start();
```

启动异步工作线程。

**异常**: `std::runtime_error` 如果已经启动

---

### stop

```cpp
void stop();
```

停止异步工作线程（等待工作线程退出）。

---

### get_drop_count

```cpp
uint64_t get_drop_count() const;
```

获取因缓冲区满而丢弃的数据数量。

**返回值**: 丢弃计数

---

## 辅助类型

### MergedQueueView\<T\>

多队列合并视图。

```cpp
class MergedQueueView {
public:
    // 获取下一个元素（按时间戳排序）
    bool next(T& data);
    
    // 获取同步统计
    SyncStats get_sync_stats() const;
};
```

---

### SyncStats

同步统计信息。

```cpp
struct SyncStats {
    uint64_t total_synced;              // 总同步数量
    uint64_t timeout_count;             // 超时次数
    uint64_t timestamp_rewind_count;    // 时间戳回退次数
};
```

---

## 错误处理

### 异常类型

库使用标准异常类型：

- `std::runtime_error`: 运行时错误（如共享内存创建失败）
- `std::invalid_argument`: 参数错误（如配置无效）
- `std::logic_error`: 逻辑错误（如重复启动）

### 错误码

布尔返回值表示成功/失败：
- `true`: 操作成功
- `false`: 操作失败（超时、队列满/空等）

### 日志

所有错误都会通过 `MPLogger` 记录，建议初始化日志系统：

```cpp
#include "mp_logger.hpp"

int main() {
    // 初始化日志
    MPLogger::init("app.log", LogLevel::INFO);
    
    // 使用库...
}
```

---

## Python API

Python API 通过 pybind11 绑定，接口与 C++ 类似。

### 导入模块

```python
import multiqueue_shm as mq
```

### 创建队列

```python
# 配置队列
config = mq.QueueConfig()
config.capacity = 1024
config.blocking_mode = mq.BlockingMode.BLOCKING
config.timeout_ms = 1000

# 创建队列（使用 bytes 类型）
queue = mq.RingQueue("my_queue", config)
```

### 写入数据

```python
# Python 中使用 bytes 类型传输数据
data = b"hello world"
if queue.push(data):
    print("Data written")
```

### 读取数据

```python
data = queue.pop()
if data is not None:
    print(f"Received: {data}")
```

### 使用 NumPy 数组

```python
import numpy as np

# 将 NumPy 数组转为 bytes
arr = np.array([1.0, 2.0, 3.0], dtype=np.float32)
queue.push(arr.tobytes())

# 读取并转回 NumPy 数组
data = queue.pop()
if data:
    arr_received = np.frombuffer(data, dtype=np.float32)
    print(arr_received)
```

### 多进程

```python
import multiprocessing

def producer(queue_name):
    queue = mq.RingQueue(queue_name, config)
    for i in range(1000):
        queue.push(f"data_{i}".encode())

def consumer(queue_name):
    queue = mq.RingQueue(queue_name, config)
    for _ in range(1000):
        data = queue.pop()
        if data:
            print(data.decode())

if __name__ == "__main__":
    queue_name = "mp_queue"
    p1 = multiprocessing.Process(target=producer, args=(queue_name,))
    p2 = multiprocessing.Process(target=consumer, args=(queue_name,))
    p1.start()
    p2.start()
    p1.join()
    p2.join()
```

---

## 性能建议

1. **选择合适的容量**: 容量过小导致频繁阻塞，容量过大浪费内存
2. **使用批量操作**: 批量读写减少系统调用开销
3. **避免频繁创建队列**: 队列创建开销较大，建议复用
4. **选择合适的阻塞模式**: 根据业务场景选择阻塞或非阻塞
5. **监控统计信息**: 使用 `get_stats()` 监控队列状态

---

**文档版本**: 1.0  
**最后更新**: 2025-11-18


