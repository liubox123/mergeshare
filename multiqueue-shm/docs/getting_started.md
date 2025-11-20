# 快速入门指南

本指南将帮助你快速上手 multiqueue-shm 库。

## 安装

### 方式 1: 从源码编译（推荐）

#### 安装依赖

**Ubuntu/Debian**:
```bash
sudo apt update
sudo apt install -y cmake g++ libboost-dev python3-dev
```

**macOS**:
```bash
brew install cmake boost python
```

**Windows**:
使用 vcpkg 安装依赖:
```bash
vcpkg install boost:x64-windows
```

#### 编译 C++ 库

```bash
git clone https://github.com/your-org/multiqueue-shm.git
cd multiqueue-shm
mkdir build && cd build
cmake .. -DBUILD_PYTHON_BINDING=OFF
cmake --build .
sudo cmake --install .
```

#### 编译 Python 绑定

```bash
cd python-binding
pip install .
```

### 方式 2: 使用包管理器

**Python** (发布后):
```bash
pip install multiqueue-shm
```

**C++** (发布后):
```bash
# Conan
conan install multiqueue-shm/0.1.0@

# vcpkg
vcpkg install multiqueue-shm
```

## 第一个示例

### C++ 示例

创建 `main.cpp`:

```cpp
#include <multiqueue_shm.hpp>
#include <iostream>

using namespace multiqueue;

int main() {
    // 1. 配置队列
    QueueConfig config;
    config.capacity = 1024;
    config.blocking_mode = BlockingMode::BLOCKING;
    config.timeout_ms = 1000;
    
    // 2. 创建队列
    RingQueue<int> queue("my_first_queue", config);
    
    // 3. 写入数据
    for (int i = 0; i < 10; ++i) {
        if (queue.push(i)) {
            std::cout << "Pushed: " << i << std::endl;
        }
    }
    
    // 4. 读取数据
    int value;
    while (queue.pop(value)) {
        std::cout << "Popped: " << value << std::endl;
    }
    
    return 0;
}
```

编译和运行:

```bash
g++ -std=c++17 main.cpp -lboost_system -pthread -o main
./main
```

### Python 示例

创建 `main.py`:

```python
import multiqueue_shm as mq

# 1. 配置队列
config = mq.QueueConfig()
config.capacity = 1024
config.blocking_mode = mq.BlockingMode.BLOCKING
config.timeout_ms = 1000

# 2. 创建队列
queue = mq.RingQueue("my_first_queue", config)

# 3. 写入数据
for i in range(10):
    data = f"message_{i}".encode()
    queue.push(data)
    print(f"Pushed: {data}")

# 4. 读取数据
while True:
    data = queue.pop()
    if data:
        print(f"Popped: {data.decode()}")
    else:
        break
```

运行:

```bash
python main.py
```

## 多进程示例

### C++ 多进程

**producer.cpp**:
```cpp
#include <multiqueue_shm.hpp>
#include <iostream>

int main() {
    multiqueue::QueueConfig config;
    config.capacity = 1024;
    multiqueue::RingQueue<int> queue("shared_queue", config);
    
    for (int i = 0; i < 1000; ++i) {
        queue.push(i);
        std::cout << "Produced: " << i << std::endl;
    }
    
    return 0;
}
```

**consumer.cpp**:
```cpp
#include <multiqueue_shm.hpp>
#include <iostream>

int main() {
    multiqueue::QueueConfig config;
    config.capacity = 1024;
    multiqueue::RingQueue<int> queue("shared_queue", config);
    
    int value;
    int count = 0;
    while (count < 1000) {
        if (queue.pop(value)) {
            std::cout << "Consumed: " << value << std::endl;
            count++;
        }
    }
    
    return 0;
}
```

编译:
```bash
g++ -std=c++17 producer.cpp -o producer -lboost_system -pthread
g++ -std=c++17 consumer.cpp -o consumer -lboost_system -pthread
```

运行:
```bash
# 终端 1
./consumer

# 终端 2
./producer
```

### Python 多进程

```python
import multiqueue_shm as mq
import multiprocessing
import time

def producer(queue_name):
    config = mq.QueueConfig()
    config.capacity = 1024
    queue = mq.RingQueue(queue_name, config)
    
    for i in range(1000):
        data = f"message_{i}".encode()
        queue.push(data)
        print(f"Produced: {i}")
        time.sleep(0.01)

def consumer(queue_name):
    config = mq.QueueConfig()
    config.capacity = 1024
    queue = mq.RingQueue(queue_name, config)
    
    count = 0
    while count < 1000:
        data = queue.pop()
        if data:
            print(f"Consumed: {data.decode()}")
            count += 1

if __name__ == "__main__":
    queue_name = "shared_queue"
    
    # 启动生产者和消费者进程
    p1 = multiprocessing.Process(target=producer, args=(queue_name,))
    p2 = multiprocessing.Process(target=consumer, args=(queue_name,))
    
    p1.start()
    p2.start()
    
    p1.join()
    p2.join()
    
    print("Done!")
```

## 混合 C++ 和 Python

C++ 程序可以与 Python 程序共享数据！

**C++ 生产者 (producer.cpp)**:
```cpp
#include <multiqueue_shm.hpp>
#include <string>

int main() {
    multiqueue::QueueConfig config;
    config.capacity = 1024;
    multiqueue::RingQueue<char[256]> queue("mixed_queue", config);
    
    for (int i = 0; i < 100; ++i) {
        char data[256];
        snprintf(data, sizeof(data), "Message from C++ #%d", i);
        queue.push(data);
    }
    
    return 0;
}
```

**Python 消费者 (consumer.py)**:
```python
import multiqueue_shm as mq

config = mq.QueueConfig()
config.capacity = 1024
queue = mq.RingQueue("mixed_queue", config)

count = 0
while count < 100:
    data = queue.pop()
    if data:
        print(f"Received from C++: {data.decode('utf-8', errors='ignore')}")
        count += 1
```

运行:
```bash
# 终端 1
python consumer.py

# 终端 2
./producer
```

## 核心概念

### 1. 队列配置 (QueueConfig)

```cpp
QueueConfig config;
config.capacity = 1024;              // 队列容量（元素数量）
config.blocking_mode = BlockingMode::BLOCKING;  // 阻塞模式
config.timeout_ms = 1000;            // 超时时间（毫秒）
config.has_timestamp = true;         // 启用时间戳
```

### 2. 阻塞模式

**阻塞模式 (BLOCKING)**:
- 队列满时，生产者等待
- 队列空时，消费者等待
- 适合不能丢失数据的场景

**非阻塞模式 (NON_BLOCKING)**:
- 队列满时，覆盖旧数据
- 队列空时，立即返回 false
- 适合实时性要求高的场景

### 3. 时间戳

如果启用时间戳，可以实现多队列时间同步：

```cpp
QueueConfig config;
config.has_timestamp = true;

RingQueue<SensorData> queue("sensor_queue", config);

SensorData data;
data.value = 25.5;
uint64_t timestamp = get_current_timestamp();

queue.push(data, timestamp);
```

### 4. 队列管理器

用于管理多个队列：

```cpp
QueueManager manager;

// 创建多个队列
auto queue1 = manager.create_or_open<int>("queue1", config);
auto queue2 = manager.create_or_open<int>("queue2", config);

// 合并队列（按时间戳同步）
std::vector<std::string> names = {"queue1", "queue2"};
auto merged = manager.merge_queues<int>(names, 100);

// 从合并视图中读取（自动按时间戳排序）
int value;
while (merged.next(value)) {
    std::cout << value << std::endl;
}
```

## 常见问题

### Q1: 如何清理共享内存？

**Linux/macOS**:
```bash
# 查看共享内存
ipcs -m

# 删除指定的共享内存段
ipcrm -m <shmid>
```

**Windows**:
共享内存在所有进程关闭后自动清理。

**编程方式**:
```cpp
QueueManager manager;
manager.remove_queue("queue_name");
```

### Q2: 队列容量应该设置多大？

建议根据以下因素决定：
- 数据大小: 单个元素的大小
- 延迟要求: 低延迟用小容量（256-1024）
- 吞吐量要求: 高吞吐用大容量（4096-16384）
- 内存限制: 总内存 = 元素大小 × 容量

### Q3: 如何处理队列满的情况？

**方式 1: 阻塞等待**
```cpp
config.blocking_mode = BlockingMode::BLOCKING;
config.timeout_ms = 5000;  // 等待 5 秒
```

**方式 2: 非阻塞覆盖**
```cpp
config.blocking_mode = BlockingMode::NON_BLOCKING;
// 队列满时自动覆盖旧数据
```

**方式 3: 增加容量**
```cpp
config.capacity = 8192;  // 增加容量
```

### Q4: 性能不达预期怎么办？

1. 使用 Tracy Profiler 分析性能瓶颈
2. 检查是否频繁阻塞
3. 考虑批量操作 (`push_batch`, `pop_batch`)
4. 调整队列容量
5. 使用非阻塞模式

### Q5: 如何调试多进程问题？

1. **启用日志**:
   ```cpp
   MPLogger::init("app.log", LogLevel::DEBUG);
   ```

2. **查看队列统计**:
   ```cpp
   auto stats = queue.get_stats();
   std::cout << "Size: " << stats.current_size << std::endl;
   std::cout << "Overwrite count: " << stats.overwrite_count << std::endl;
   ```

3. **使用 GDB 调试**:
   ```bash
   gdb --args ./consumer
   (gdb) attach <producer_pid>
   ```

## 下一步

- 阅读 [架构设计文档](architecture.md) 了解内部实现
- 查看 [API 参考文档](api_reference.md) 了解完整接口
- 查看 [examples/](../examples/) 目录中的更多示例
- 阅读 [性能优化指南](performance.md)

## 获取帮助

- GitHub Issues: 报告 Bug 和提问
- GitHub Discussions: 讨论和交流
- 文档: `docs/` 目录
- 示例代码: `examples/` 目录

---

**祝你使用愉快！** 🚀


