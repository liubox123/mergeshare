# Python API 设计文档

本文档定义 multiqueue-shm 库的 Python API 规范。

## 设计原则

1. **Pythonic**: API 符合 Python 习惯和风格
2. **类型提示**: 使用 Python 类型提示（Type Hints）
3. **简洁性**: 简化常见操作，同时保留高级功能
4. **一致性**: 与 C++ API 保持概念一致
5. **文档完整**: 所有公开接口都有完整的文档字符串

## 模块结构

```python
multiqueue_shm/
├── __init__.py          # 模块入口
├── _core.so             # C++ 扩展模块（pybind11 生成）
├── queue.py             # 队列封装
├── manager.py           # 队列管理器
├── config.py            # 配置类
└── exceptions.py        # 异常定义
```

## 核心 API

### 1. 配置类

#### BlockingMode (枚举)

```python
from enum import Enum

class BlockingMode(Enum):
    """阻塞模式枚举"""
    BLOCKING = 0      # 阻塞模式
    NON_BLOCKING = 1  # 非阻塞模式
```

#### QueueConfig (配置类)

```python
from dataclasses import dataclass
from typing import List, Optional

@dataclass
class QueueConfig:
    """
    队列配置
    
    Attributes:
        capacity: 队列容量（元素数量）
        blocking_mode: 阻塞模式
        timeout_ms: 超时时间（毫秒）
        has_timestamp: 是否启用时间戳
        queue_name: 队列名称
        extra_queue_names: 额外关联的队列名称
        user_metadata: 用户元数据
        enable_async: 是否启用异步线程
        async_buffer_size: 异步缓冲区大小
        async_thread_count: 异步工作线程数量
    """
    capacity: int = 1024
    blocking_mode: BlockingMode = BlockingMode.BLOCKING
    timeout_ms: int = 1000
    has_timestamp: bool = False
    queue_name: str = ""
    extra_queue_names: List[str] = None
    user_metadata: str = ""
    enable_async: bool = False
    async_buffer_size: int = 256
    async_thread_count: int = 1
    
    def __post_init__(self):
        if self.extra_queue_names is None:
            self.extra_queue_names = []
    
    def is_valid(self) -> bool:
        """验证配置是否有效"""
        pass
    
    def is_power_of_two(self) -> bool:
        """检查容量是否为 2 的幂次"""
        pass
    
    def round_up_capacity_to_power_of_two(self) -> None:
        """将容量向上取整到最近的 2 的幂次"""
        pass
```

### 2. 环形队列

#### RingQueue (队列类)

```python
from typing import Optional, Tuple, Any
import numpy as np

class RingQueue:
    """
    环形队列
    
    支持多生产者-多消费者模式的共享内存队列
    
    示例:
        >>> config = QueueConfig(capacity=1024)
        >>> queue = RingQueue("my_queue", config)
        >>> queue.push(b"hello")
        True
        >>> data = queue.pop()
        >>> print(data)
        b'hello'
    """
    
    def __init__(self, name: str, config: QueueConfig):
        """
        创建或打开队列
        
        Args:
            name: 队列名称
            config: 队列配置
            
        Raises:
            RuntimeError: 如果创建/打开失败
            ValueError: 如果配置无效
        """
        pass
    
    def push(self, data: bytes, timestamp: Optional[int] = None) -> bool:
        """
        写入数据
        
        Args:
            data: 要写入的数据（bytes 类型）
            timestamp: 时间戳（纳秒），如果为 None 则自动生成
            
        Returns:
            True 如果写入成功，False 如果超时或失败
            
        示例:
            >>> queue.push(b"data")
            True
            >>> queue.push(np.array([1, 2, 3]).tobytes())
            True
        """
        pass
    
    def try_push(self, data: bytes, timestamp: Optional[int] = None) -> bool:
        """
        尝试写入数据（不阻塞）
        
        Args:
            data: 要写入的数据
            timestamp: 时间戳
            
        Returns:
            True 如果写入成功，False 如果队列满
        """
        pass
    
    def push_with_timeout(
        self,
        data: bytes,
        timeout_ms: int,
        timestamp: Optional[int] = None
    ) -> bool:
        """
        带自定义超时时间的写入
        
        Args:
            data: 要写入的数据
            timeout_ms: 超时时间（毫秒）
            timestamp: 时间戳
            
        Returns:
            True 如果写入成功，False 如果超时
        """
        pass
    
    def pop(self) -> Optional[Tuple[bytes, int]]:
        """
        读取数据
        
        Returns:
            (data, timestamp) 元组，如果队列空或超时则返回 None
            
        示例:
            >>> result = queue.pop()
            >>> if result:
            ...     data, timestamp = result
            ...     print(f"Data: {data}, Time: {timestamp}")
        """
        pass
    
    def try_pop(self) -> Optional[Tuple[bytes, int]]:
        """
        尝试读取数据（不阻塞）
        
        Returns:
            (data, timestamp) 元组，如果队列空则返回 None
        """
        pass
    
    def pop_with_timeout(self, timeout_ms: int) -> Optional[Tuple[bytes, int]]:
        """
        带自定义超时时间的读取
        
        Args:
            timeout_ms: 超时时间（毫秒）
            
        Returns:
            (data, timestamp) 元组，如果超时则返回 None
        """
        pass
    
    def peek(self) -> Optional[Tuple[bytes, int]]:
        """
        查看队首元素（不移除）
        
        Returns:
            (data, timestamp) 元组，如果队列空则返回 None
        """
        pass
    
    def size(self) -> int:
        """获取当前队列大小"""
        pass
    
    def empty(self) -> bool:
        """检查队列是否为空"""
        pass
    
    def full(self) -> bool:
        """检查队列是否已满"""
        pass
    
    def capacity(self) -> int:
        """获取队列容量"""
        pass
    
    def get_stats(self) -> 'QueueStats':
        """获取队列统计信息"""
        pass
    
    def close(self) -> None:
        """关闭队列"""
        pass
    
    def is_closed(self) -> bool:
        """检查队列是否已关闭"""
        pass
    
    @property
    def name(self) -> str:
        """获取队列名称"""
        pass
    
    # 上下文管理器支持
    def __enter__(self) -> 'RingQueue':
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False
    
    # 迭代器支持
    def __iter__(self) -> 'RingQueue':
        return self
    
    def __next__(self) -> Tuple[bytes, int]:
        result = self.pop()
        if result is None:
            raise StopIteration
        return result
    
    # NumPy 数组支持
    def push_array(self, arr: np.ndarray, timestamp: Optional[int] = None) -> bool:
        """
        写入 NumPy 数组
        
        Args:
            arr: NumPy 数组
            timestamp: 时间戳
            
        Returns:
            True 如果写入成功
            
        示例:
            >>> arr = np.array([1.0, 2.0, 3.0], dtype=np.float32)
            >>> queue.push_array(arr)
            True
        """
        return self.push(arr.tobytes(), timestamp)
    
    def pop_array(self, dtype: np.dtype, shape: Tuple[int, ...]) -> Optional[Tuple[np.ndarray, int]]:
        """
        读取为 NumPy 数组
        
        Args:
            dtype: 数组数据类型
            shape: 数组形状
            
        Returns:
            (array, timestamp) 元组，如果队列空则返回 None
            
        示例:
            >>> result = queue.pop_array(np.float32, (3,))
            >>> if result:
            ...     arr, timestamp = result
            ...     print(arr)
        """
        result = self.pop()
        if result is None:
            return None
        data, timestamp = result
        arr = np.frombuffer(data, dtype=dtype).reshape(shape)
        return (arr, timestamp)
```

### 3. 队列统计

#### QueueStats (统计信息类)

```python
@dataclass
class QueueStats:
    """
    队列统计信息
    
    Attributes:
        total_pushed: 总写入数量
        total_popped: 总读取数量
        overwrite_count: 覆盖次数
        producer_count: 当前生产者数量
        consumer_count: 当前消费者数量
        current_size: 当前队列大小
        capacity: 队列容量
        created_at: 创建时间
        last_write_time: 最后写入时间
        last_read_time: 最后读取时间
        is_closed: 队列是否已关闭
    """
    total_pushed: int
    total_popped: int
    overwrite_count: int
    producer_count: int
    consumer_count: int
    current_size: int
    capacity: int
    created_at: int
    last_write_time: int
    last_read_time: int
    is_closed: bool
```

### 4. 队列管理器

#### QueueManager (管理器类)

```python
from typing import List, Optional

class QueueManager:
    """
    队列管理器
    
    管理多个共享内存队列
    
    示例:
        >>> manager = QueueManager()
        >>> queue1 = manager.create_or_open("queue1", config)
        >>> queue2 = manager.create_or_open("queue2", config)
        >>> merged = manager.merge_queues(["queue1", "queue2"], sync_timeout_ms=100)
    """
    
    def __init__(self):
        """创建队列管理器"""
        pass
    
    def create_or_open(self, name: str, config: QueueConfig) -> RingQueue:
        """
        创建或打开队列
        
        Args:
            name: 队列名称
            config: 队列配置
            
        Returns:
            队列对象
            
        Raises:
            RuntimeError: 如果创建/打开失败
        """
        pass
    
    def merge_queues(
        self,
        queue_names: List[str],
        sync_timeout_ms: int
    ) -> 'MergedQueueView':
        """
        合并多个队列（按时间戳同步）
        
        Args:
            queue_names: 队列名称列表
            sync_timeout_ms: 同步超时时间（毫秒）
            
        Returns:
            合并队列视图
            
        Raises:
            ValueError: 如果队列不存在或配置不匹配
        """
        pass
    
    def get_stats(self, name: str) -> QueueStats:
        """获取队列统计信息"""
        pass
    
    def remove_queue(self, name: str) -> bool:
        """删除队列"""
        pass
    
    def list_queues(self) -> List[str]:
        """列出所有队列名称"""
        pass
    
    def exists(self, name: str) -> bool:
        """检查队列是否存在"""
        pass
    
    def close_queue(self, name: str) -> bool:
        """关闭指定队列"""
        pass
    
    def close_all(self) -> None:
        """关闭所有队列"""
        pass
```

### 5. 合并队列视图

#### MergedQueueView (合并视图类)

```python
class MergedQueueView:
    """
    多队列合并视图
    
    将多个队列按时间戳合并为一个逻辑队列
    
    示例:
        >>> merged = manager.merge_queues(["queue1", "queue2"], 100)
        >>> for data, timestamp in merged:
        ...     print(f"Data: {data}, Time: {timestamp}")
    """
    
    def __init__(self, queues: List[RingQueue], sync_timeout_ms: int):
        """
        创建合并视图
        
        Args:
            queues: 队列列表
            sync_timeout_ms: 同步超时时间
        """
        pass
    
    def next(self) -> Optional[Tuple[bytes, int]]:
        """
        获取下一个元素（按时间戳排序）
        
        Returns:
            (data, timestamp) 元组，如果所有队列都空则返回 None
        """
        pass
    
    def has_more(self) -> bool:
        """检查是否还有数据"""
        pass
    
    def get_sync_stats(self) -> 'SyncStats':
        """获取同步统计信息"""
        pass
    
    def reset_stats(self) -> None:
        """重置统计信息"""
        pass
    
    # 迭代器支持
    def __iter__(self) -> 'MergedQueueView':
        return self
    
    def __next__(self) -> Tuple[bytes, int]:
        result = self.next()
        if result is None:
            raise StopIteration
        return result
```

### 6. 同步统计

#### SyncStats (同步统计类)

```python
@dataclass
class SyncStats:
    """
    同步统计信息
    
    Attributes:
        total_synced: 总同步数量
        timeout_count: 超时次数
        timestamp_rewind_count: 时间戳回退次数
    """
    total_synced: int
    timeout_count: int
    timestamp_rewind_count: int
```

### 7. 时间戳工具

#### TimestampSynchronizer (时间戳工具类)

```python
class TimestampSynchronizer:
    """时间戳同步器工具"""
    
    @staticmethod
    def now() -> int:
        """获取当前时间戳（纳秒）"""
        pass
    
    @staticmethod
    def now_micros() -> int:
        """获取当前时间戳（微秒）"""
        pass
    
    @staticmethod
    def now_millis() -> int:
        """获取当前时间戳（毫秒）"""
        pass
    
    @staticmethod
    def nanos_to_micros(nanos: int) -> int:
        """纳秒转微秒"""
        pass
    
    @staticmethod
    def nanos_to_millis(nanos: int) -> int:
        """纳秒转毫秒"""
        pass
    
    @staticmethod
    def is_timestamp_valid(timestamp: int, tolerance_ms: int = 10000) -> bool:
        """检查时间戳是否有效"""
        pass
```

## 异常定义

```python
class MultiQueueError(Exception):
    """MultiQueue 基础异常"""
    pass

class QueueFullError(MultiQueueError):
    """队列已满异常"""
    pass

class QueueEmptyError(MultiQueueError):
    """队列为空异常"""
    pass

class QueueTimeoutError(MultiQueueError):
    """队列操作超时异常"""
    pass

class QueueClosedError(MultiQueueError):
    """队列已关闭异常"""
    pass

class InvalidConfigError(MultiQueueError):
    """配置无效异常"""
    pass
```

## 使用示例

### 示例 1: 基本使用

```python
import multiqueue_shm as mq

# 配置队列
config = mq.QueueConfig(
    capacity=1024,
    blocking_mode=mq.BlockingMode.BLOCKING,
    timeout_ms=1000
)

# 创建队列
queue = mq.RingQueue("my_queue", config)

# 写入数据
queue.push(b"hello world")

# 读取数据
result = queue.pop()
if result:
    data, timestamp = result
    print(f"Received: {data.decode()}")
```

### 示例 2: 多进程

```python
import multiqueue_shm as mq
import multiprocessing

def producer(queue_name):
    config = mq.QueueConfig(capacity=1024)
    queue = mq.RingQueue(queue_name, config)
    
    for i in range(1000):
        queue.push(f"message_{i}".encode())

def consumer(queue_name):
    config = mq.QueueConfig(capacity=1024)
    queue = mq.RingQueue(queue_name, config)
    
    count = 0
    while count < 1000:
        result = queue.pop()
        if result:
            data, _ = result
            print(data.decode())
            count += 1

if __name__ == "__main__":
    queue_name = "mp_queue"
    p1 = multiprocessing.Process(target=producer, args=(queue_name,))
    p2 = multiprocessing.Process(target=consumer, args=(queue_name,))
    
    p1.start()
    p2.start()
    p1.join()
    p2.join()
```

### 示例 3: NumPy 数组

```python
import multiqueue_shm as mq
import numpy as np

config = mq.QueueConfig(capacity=1024)
queue = mq.RingQueue("numpy_queue", config)

# 发送 NumPy 数组
arr = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
queue.push_array(arr)

# 接收 NumPy 数组
result = queue.pop_array(np.float32, (4,))
if result:
    arr_received, timestamp = result
    print(arr_received)
```

### 示例 4: 上下文管理器

```python
import multiqueue_shm as mq

config = mq.QueueConfig(capacity=1024)

with mq.RingQueue("my_queue", config) as queue:
    queue.push(b"data")
    result = queue.pop()
    # 自动关闭队列
```

### 示例 5: 迭代器

```python
import multiqueue_shm as mq

config = mq.QueueConfig(capacity=1024, timeout_ms=100)
queue = mq.RingQueue("my_queue", config)

# 写入数据
for i in range(10):
    queue.push(f"message_{i}".encode())

# 使用迭代器读取
for data, timestamp in queue:
    print(data.decode())
    # 当队列空时自动停止
```

---

**文档版本**: 1.0  
**最后更新**: 2025-11-18  
**维护者**: Python 开发者

