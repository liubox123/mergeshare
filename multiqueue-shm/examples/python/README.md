# Python 使用示例

本目录包含 multiqueue-shm 库的 Python 使用示例。

## 前置要求

安装 Python 绑定:
```bash
cd python-binding
pip install .
```

## 示例列表

### 1. basic_usage.py
最基本的使用示例，演示如何创建队列、写入和读取数据。

```bash
python basic_usage.py
```

### 2. multi_process.py
多进程示例，演示生产者和消费者在不同进程中的使用。

```bash
python multi_process.py
```

### 3. numpy_array.py
使用 NumPy 数组的示例，演示如何传输数值数据。

```bash
python numpy_array.py
```

### 4. mixed_cpp_python/
混合 C++ 和 Python 的示例，演示跨语言的进程间通信。

```bash
# 终端 1: 运行 Python 消费者
python mixed_cpp_python/consumer.py

# 终端 2: 运行 C++ 生产者
../cpp/build/mixed_producer
```

### 5. async_processing.py
异步处理示例，演示如何在后台线程中处理数据。

```bash
python async_processing.py
```

### 6. performance_test.py
性能测试示例，测试 Python 绑定的性能。

```bash
python performance_test.py
```

## 常用模式

### 模式 1: 简单的生产者-消费者

```python
import multiqueue_shm as mq
import multiprocessing

def producer(queue_name):
    config = mq.QueueConfig()
    config.capacity = 1024
    queue = mq.RingQueue(queue_name, config)
    
    for i in range(1000):
        data = f"message_{i}".encode()
        queue.push(data)
    print("Producer finished")

def consumer(queue_name):
    config = mq.QueueConfig()
    config.capacity = 1024
    queue = mq.RingQueue(queue_name, config)
    
    count = 0
    while count < 1000:
        data = queue.pop()
        if data:
            print(f"Received: {data.decode()}")
            count += 1
    print("Consumer finished")

if __name__ == "__main__":
    queue_name = "test_queue"
    
    p1 = multiprocessing.Process(target=producer, args=(queue_name,))
    p2 = multiprocessing.Process(target=consumer, args=(queue_name,))
    
    p1.start()
    p2.start()
    p1.join()
    p2.join()
```

### 模式 2: 使用 NumPy 数组

```python
import multiqueue_shm as mq
import numpy as np

# 创建队列
config = mq.QueueConfig()
queue = mq.RingQueue("numpy_queue", config)

# 发送 NumPy 数组
arr = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
queue.push(arr.tobytes())

# 接收 NumPy 数组
data = queue.pop()
if data:
    arr_received = np.frombuffer(data, dtype=np.float32)
    print(arr_received)
```

### 模式 3: 上下文管理器

```python
import multiqueue_shm as mq

class QueueContext:
    def __init__(self, name, config):
        self.name = name
        self.config = config
        self.queue = None
    
    def __enter__(self):
        self.queue = mq.RingQueue(self.name, self.config)
        return self.queue
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        # 清理资源
        del self.queue

# 使用上下文管理器
with QueueContext("my_queue", config) as queue:
    queue.push(b"hello")
    data = queue.pop()
```

## 注意事项

1. **数据类型**: Python 绑定使用 `bytes` 类型传输数据
2. **编码**: 字符串需要编码为 bytes: `"hello".encode()`
3. **NumPy 数组**: 使用 `tobytes()` 和 `frombuffer()` 进行转换
4. **多进程**: 使用 `multiprocessing.Process` 而不是 `threading.Thread`
5. **共享内存清理**: Python 进程退出时自动清理

## 调试技巧

### 1. 启用日志
```python
import multiqueue_shm as mq

# 启用详细日志（如果库支持）
# mq.enable_logging()
```

### 2. 查看队列状态
```python
stats = queue.get_stats()
print(f"Queue size: {stats.current_size}")
print(f"Total pushed: {stats.total_pushed}")
print(f"Total popped: {stats.total_popped}")
```

### 3. 错误处理
```python
try:
    queue = mq.RingQueue("my_queue", config)
    queue.push(b"data")
except Exception as e:
    print(f"Error: {e}")
```

## 性能优化建议

1. **批量操作**: 如果需要高吞吐量，考虑批量读写
2. **避免频繁创建队列**: 重用队列对象
3. **选择合适的容量**: 根据数据大小和频率调整
4. **使用非阻塞模式**: 如果允许丢失数据，可以提高性能

---

**维护者**: Python 开发者


