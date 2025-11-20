# 架构设计文档

## 1. 系统架构概览

### 1.1 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                     应用层 (Application Layer)               │
│  ┌──────────────────┐              ┌──────────────────┐     │
│  │  Python 应用程序  │              │   C++ 应用程序    │     │
│  └────────┬─────────┘              └────────┬─────────┘     │
└───────────┼────────────────────────────────┼───────────────┘
            │                                │
┌───────────▼────────────────────────────────▼───────────────┐
│                    绑定层 (Binding Layer)                    │
│  ┌──────────────────┐              ┌──────────────────┐     │
│  │  pybind11 绑定   │              │  C++ Header-Only │     │
│  └────────┬─────────┘              └────────┬─────────┘     │
└───────────┼────────────────────────────────┼───────────────┘
            │                                │
            └────────────────┬───────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│                   核心层 (Core Layer)                        │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │          队列管理器 (QueueManager)                    │   │
│  │  - 队列注册表 (Queue Registry)                        │   │
│  │  - 时间戳同步器 (Timestamp Synchronizer)              │   │
│  │  - 队列合并器 (Queue Merger)                          │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         环形队列 (RingQueue<T>)                       │   │
│  │                                                        │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │   │
│  │  │ 元数据头部    │  │ 内存池管理    │  │ 偏移管理   │  │   │
│  │  │ (Metadata)   │  │ (MemoryPool) │  │ (Offsets)  │  │   │
│  │  └──────────────┘  └──────────────┘  └────────────┘  │   │
│  │                                                        │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │   │
│  │  │ 生产者协调    │  │ 消费者协调    │  │ 锁策略     │  │   │
│  │  │ (Producers)  │  │ (Consumers)  │  │ (Locking)  │  │   │
│  │  └──────────────┘  └──────────────┘  └────────────┘  │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │      异步线程池 (Async ThreadPool - Optional)         │   │
│  │  - 固定长度队列缓冲                                    │   │
│  │  - 工作线程管理                                        │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│                 基础设施层 (Infrastructure Layer)              │
│                                                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐   │
│  │   日志组件   │  │ Tracy集成   │  │  Boost Interprocess │   │
│  │  (Logger)   │  │ (Profiler)  │  │  (Shared Memory)    │   │
│  └─────────────┘  └─────────────┘  └─────────────────────┘   │
└────────────────────────────────────────────────────────────────┘
```

## 2. 核心组件设计

### 2.1 环形队列 (RingQueue<T>)

#### 2.1.1 内存布局

```
共享内存段布局:
┌─────────────────────────────────────────────────────────────┐
│                    元数据头部 (固定大小)                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ QueueMetadata:                                        │   │
│  │  - magic_number: uint64_t                            │   │
│  │  - version: uint32_t                                 │   │
│  │  - element_size: size_t                              │   │
│  │  - capacity: size_t                                  │   │
│  │  - has_timestamp: bool                               │   │
│  │  - blocking_mode: BlockingMode                       │   │
│  │  - timeout_ms: uint32_t                              │   │
│  │  - queue_name: char[64]                              │   │
│  │  - extra_queue_names: char[256]                      │   │
│  │  - user_metadata: char[512]                          │   │
│  │  - created_at: timestamp                             │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    控制区 (原子变量)                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ ControlBlock:                                         │   │
│  │  - write_offset: atomic<uint64_t>                    │   │
│  │  - read_offset: atomic<uint64_t>                     │   │
│  │  - producer_count: atomic<uint32_t>                  │   │
│  │  - consumer_count: atomic<uint32_t>                  │   │
│  │  - overwrite_count: atomic<uint64_t>                 │   │
│  │  - status_flags: atomic<uint32_t>                    │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                   数据区 (环形数组)                          │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Element[0]:                                           │   │
│  │  - timestamp: uint64_t (可选)                         │   │
│  │  - sequence_id: uint64_t                             │   │
│  │  - data_size: uint32_t                               │   │
│  │  - flags: uint32_t                                   │   │
│  │  - payload: T                                         │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │ Element[1]: ...                                       │   │
│  │ ...                                                   │   │
│  │ Element[capacity-1]: ...                              │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

#### 2.1.2 关键算法

**生产者写入算法 (阻塞模式)**:
```cpp
bool push_blocking(const T& data, uint64_t timestamp, uint32_t timeout_ms) {
    // 1. 获取当前写偏移
    uint64_t write_idx = write_offset.load(memory_order_acquire);
    uint64_t read_idx = read_offset.load(memory_order_acquire);
    
    // 2. 检查队列是否满
    auto start_time = now();
    while ((write_idx - read_idx) >= capacity) {
        if (now() - start_time > timeout_ms) {
            return false; // 超时
        }
        std::this_thread::yield();
        read_idx = read_offset.load(memory_order_acquire);
    }
    
    // 3. CAS 原子获取写入位置
    uint64_t next_write = write_idx + 1;
    if (!write_offset.compare_exchange_strong(write_idx, next_write)) {
        goto retry; // 其他生产者抢先，重试
    }
    
    // 4. 写入数据
    size_t slot = write_idx % capacity;
    elements[slot].timestamp = timestamp;
    elements[slot].sequence_id = write_idx;
    elements[slot].payload = data;
    elements[slot].flags = VALID_FLAG;
    
    return true;
}
```

**消费者读取算法 (多消费者)**:
```cpp
bool pop(T& data, uint64_t& timestamp) {
    // 1. 获取当前读偏移
    uint64_t read_idx = read_offset.load(memory_order_acquire);
    uint64_t write_idx = write_offset.load(memory_order_acquire);
    
    // 2. 检查队列是否空
    if (read_idx >= write_idx) {
        return false; // 队列空
    }
    
    // 3. CAS 原子获取读取位置
    uint64_t next_read = read_idx + 1;
    if (!read_offset.compare_exchange_strong(read_idx, next_read)) {
        goto retry; // 其他消费者抢先，重试
    }
    
    // 4. 读取数据
    size_t slot = read_idx % capacity;
    while (!(elements[slot].flags & VALID_FLAG)) {
        // 等待生产者完成写入
        std::this_thread::yield();
    }
    
    data = elements[slot].payload;
    timestamp = elements[slot].timestamp;
    elements[slot].flags = 0; // 标记为已读
    
    return true;
}
```

### 2.2 队列管理器 (QueueManager)

#### 职责
- 管理多个共享内存队列
- 提供统一的创建/打开/关闭接口
- 实现队列注册表
- 提供队列合并和时间戳同步功能

#### 核心接口
```cpp
class QueueManager {
public:
    // 创建或打开队列
    template<typename T>
    std::shared_ptr<RingQueue<T>> create_or_open(
        const std::string& name,
        size_t capacity,
        const QueueConfig& config
    );
    
    // 合并多个队列（按时间戳同步）
    template<typename T>
    MergedQueueView<T> merge_queues(
        const std::vector<std::string>& queue_names,
        uint32_t sync_timeout_ms
    );
    
    // 获取队列统计信息
    QueueStats get_stats(const std::string& name);
    
    // 删除队列
    bool remove_queue(const std::string& name);
};
```

### 2.3 时间戳同步器 (Timestamp Synchronizer)

#### 同步策略
多个队列合并时，需要根据时间戳对齐数据：

```
队列A: [t=100, data1] [t=102, data2] [t=105, data3]
队列B: [t=101, data4] [t=103, data5] [t=106, data6]
队列C: [无时间戳]

合并结果（按时间戳排序）:
t=100: data1 (from A)
t=101: data4 (from B)
t=102: data2 (from A)
t=103: data5 (from B)
t=105: data3 (from A)
t=106: data6 (from B)

队列C: 无法参与同步，等待超时
```

算法伪码:
```cpp
bool sync_next(MergedQueueView& view, Element& result) {
    uint64_t min_timestamp = UINT64_MAX;
    int selected_queue = -1;
    
    // 1. 扫描所有队列的头部元素
    for (int i = 0; i < view.queues.size(); ++i) {
        if (!view.queues[i].has_timestamp) {
            // 无时间戳队列，尝试等待
            if (!wait_for_data(view.queues[i], view.timeout_ms)) {
                return false; // 超时
            }
        }
        
        Element head;
        if (view.queues[i].peek(head)) {
            if (head.timestamp < min_timestamp) {
                min_timestamp = head.timestamp;
                selected_queue = i;
            }
        }
    }
    
    // 2. 从最小时间戳的队列中弹出
    if (selected_queue >= 0) {
        view.queues[selected_queue].pop(result);
        return true;
    }
    
    return false;
}
```

### 2.4 独立线程模式 (Async ThreadPool)

当启用独立线程时，队列会启动专用工作线程来消费数据，避免阻塞主线程。

#### 架构
```
生产者 → RingQueue → [固定长度缓冲队列] → 工作线程 → 用户回调
                           ↓
                      (避免阻塞)
```

#### 实现要点
```cpp
class AsyncQueueWrapper {
    RingQueue<T> shm_queue_;           // 共享内存队列
    std::queue<T> buffer_queue_;        // 固定长度缓冲
    std::thread worker_thread_;         // 工作线程
    std::function<void(T)> callback_;   // 用户回调
    
    void worker_loop() {
        while (running_) {
            T data;
            if (shm_queue_.pop(data, 10 /*ms*/)) {
                // 检查缓冲队列是否满
                {
                    std::lock_guard lock(mutex_);
                    if (buffer_queue_.size() >= max_buffer_size_) {
                        buffer_queue_.pop(); // 丢弃最旧数据
                        drop_count_++;
                    }
                    buffer_queue_.push(data);
                }
                cv_.notify_one();
            }
        }
    }
};
```

## 3. 日志组件设计

### 3.1 多进程安全日志

日志组件需要支持多进程写入同一日志文件，避免数据竞争。

#### 方案
- 使用文件锁（flock/LockFile）确保原子写入
- 每条日志带有进程ID和线程ID
- 支持日志级别过滤
- 支持异步批量写入（可选）

#### 接口设计
```cpp
class MPLogger {
public:
    static MPLogger& instance();
    
    void log(LogLevel level, const char* file, int line, 
             const std::string& msg);
    
    void set_level(LogLevel level);
    void set_output_file(const std::string& path);
    
    // 宏定义
    #define LOG_INFO(msg) \
        MPLogger::instance().log(LogLevel::INFO, __FILE__, __LINE__, msg)
};
```

## 4. Tracy 性能监控集成

### 4.1 监控点布局

关键路径插入 Tracy 监控点：

```cpp
void push_blocking(const T& data) {
    ZoneScoped;  // Tracy 函数监控
    
    {
        ZoneScopedN("AcquireWriteSlot");
        // 获取写入位置
    }
    
    {
        ZoneScopedN("WriteData");
        // 写入数据
    }
    
    TracyPlot("QueueSize", size());  // 队列大小监控
}
```

### 4.2 性能指标

监控的关键指标：
- 队列操作延迟 (push/pop)
- 队列大小变化
- 生产者/消费者数量
- 阻塞等待时间
- 覆盖事件计数

## 5. 错误处理和异常安全

### 5.1 错误处理策略

| 场景 | 策略 |
|-----|------|
| 共享内存创建失败 | 抛出异常，记录日志 |
| 队列满（阻塞模式） | 阻塞等待，超时返回false |
| 队列满（非阻塞模式） | 覆盖旧数据，记录覆盖计数 |
| 时间戳同步超时 | 返回false，记录日志 |
| 数据损坏检测 | 记录错误，跳过损坏数据 |

### 5.2 异常安全保证

- 所有公开接口提供 **基本异常安全** 保证
- 资源使用 RAII 管理
- 原子操作不抛出异常
- 锁使用 RAII (std::lock_guard)

## 6. 性能优化策略

### 6.1 无锁算法
- 使用原子变量 (`std::atomic`) 实现无锁队列
- CAS (Compare-And-Swap) 操作保证线程安全

### 6.2 缓存行优化
```cpp
// 避免伪共享 (false sharing)
struct alignas(64) ControlBlock {
    std::atomic<uint64_t> write_offset;
    char padding1[56];  // 填充到64字节
    
    std::atomic<uint64_t> read_offset;
    char padding2[56];
};
```

### 6.3 内存预分配
- 队列创建时一次性分配所有内存
- 避免运行时动态分配

## 7. 平台兼容性

### 7.1 跨平台抽象

| 功能 | Linux | macOS | Windows |
|-----|-------|-------|---------|
| 共享内存 | shm_open | shm_open | CreateFileMapping |
| 文件锁 | flock | flock | LockFile |
| 原子操作 | std::atomic | std::atomic | std::atomic |
| 高精度时间 | clock_gettime | mach_absolute_time | QueryPerformanceCounter |

使用 Boost.Interprocess 统一接口，屏蔽平台差异。

## 8. 安全性考虑

### 8.1 数据完整性
- Magic Number 验证
- 版本号检查
- CRC32 校验 (可选)

### 8.2 进程崩溃处理
- 使用命名共享内存，进程重启可恢复
- 记录生产者/消费者计数，检测异常退出
- 提供清理工具删除僵尸队列

---

**文档版本**: 1.0  
**最后更新**: 2025-11-18  
**维护者**: 架构师


