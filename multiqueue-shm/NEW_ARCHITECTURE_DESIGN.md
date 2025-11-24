# MultiQueue-SHM 新架构设计方案

基于 `sharedatastream` 项目的成熟架构，重新设计 `multiqueue-shm`

## 1. 设计目标

### 核心原则
1. **多消费者广播模式**：每个消费者独立维护读指针，都能读到所有数据
2. **生产者保护**：生产者指针不能追越最慢的消费者指针
3. **消费者保护**：消费者指针不能超越生产者指针
4. **进程崩溃保护**：检测并清理僵尸共享内存
5. **跨平台支持**：Windows 和 POSIX 系统

### 参考架构优点（来自 sharedatastream）
- ✅ 使用 `interprocess_mutex` 和 `interprocess_condition` 实现进程间同步
- ✅ 链式节点结构（Node）+ Header，灵活管理内存
- ✅ 消费者ID分配机制（`next_consumer_id`）
- ✅ 生产者 PID 检测，防止僵尸共享内存
- ✅ Python 装饰器接口，易用性好
- ✅ 内部线程队列（cache），解耦共享内存操作和 Python 回调

---

## 2. 核心数据结构设计

### 2.1 共享内存布局

```
+-------------------+
| RingQueueHeader   |  <- 控制信息
+-------------------+
| Node[0]           |  <- 第一个数据节点
| +-- Metadata      |
| +-- Data[]        |
+-------------------+
| Node[1]           |
| +-- Metadata      |
| +-- Data[]        |
+-------------------+
| ...               |
+-------------------+
| Node[N-1]         |
| +-- Metadata      |
| +-- Data[]        |
+-------------------+
```

### 2.2 RingQueueHeader 结构

```cpp
struct alignas(64) RingQueueHeader {
    // ===== 魔数和版本 =====
    uint64_t magic_number;           // 0x4D5153484D454D00 "MQSHMEM\0"
    uint32_t version;                // 版本号
    
    // ===== 同步原语 =====
    interprocess_mutex global_mutex;           // 全局互斥锁
    interprocess_condition cond_not_empty;     // 队列非空条件变量
    interprocess_condition cond_not_full;      // 队列非满条件变量
    
    // ===== 核心指针 =====
    std::atomic<uint32_t> producer_head;       // 生产者头指针（offset）
    std::atomic<uint32_t> producer_tail;       // 生产者尾指针（offset）
    
    // ===== 队列配置 =====
    uint32_t node_count;             // 节点总数
    uint32_t node_size;              // 单节点大小（含头部）
    uint32_t data_block_size;        // 数据块大小
    size_t shm_size;                 // 共享内存总大小
    
    // ===== 生产者信息 =====
    std::atomic<uint64_t> producer_pid;        // 生产者进程 PID
    std::atomic<bool> producer_active;         // 生产者是否活跃
    uint64_t created_at;             // 创建时间戳
    
    // ===== 消费者注册表 =====
    std::atomic<uint32_t> next_consumer_id;    // 下一个消费者ID
    std::atomic<uint32_t> active_consumer_count; // 活跃消费者数量
    
    struct ConsumerSlot {
        std::atomic<bool> active;                  // 是否活跃
        std::atomic<uint32_t> head_offset;         // 消费者头指针
        std::atomic<uint64_t> last_access_time;    // 最后访问时间
        char padding[64 - sizeof(std::atomic<bool>) - 
                    sizeof(std::atomic<uint32_t>) - 
                    sizeof(std::atomic<uint64_t>)];
    } consumer_slots[MAX_CONSUMERS];  // 最多 32 个消费者
    
    // ===== 元数据 =====
    char metadata[256];              // 用户自定义元数据
    
    // ===== 统计信息 =====
    std::atomic<uint64_t> total_pushed;        // 总推送数
    std::atomic<uint64_t> total_popped;        // 总弹出数
    std::atomic<uint64_t> overwrite_count;     // 覆盖次数
};
```

### 2.3 Node 结构

```cpp
struct alignas(8) Node {
    // ===== 节点元数据 =====
    std::atomic<uint64_t> sequence;       // 序列号（生产者递增）
    std::atomic<uint32_t> data_size;      // 实际数据大小
    std::atomic<uint64_t> timestamp;      // 时间戳（纳秒）
    std::atomic<bool> valid;              // 数据是否有效
    uint32_t next_offset;                 // 下一个节点的偏移量
    
    // ===== 数据区 =====
    char data[];  // 柔性数组，实际大小由 data_block_size 决定
};
```

---

## 3. 核心算法设计

### 3.1 生产者 Push 逻辑

```cpp
bool SharedRingQueueProducer::push(const void* data, uint32_t size) {
    if (size > data_block_size_) {
        throw std::invalid_argument("Data size exceeds block size");
    }
    
    scoped_lock<interprocess_mutex> lock(header_->global_mutex);
    
    while (true) {
        // 1. 获取当前生产者尾指针
        uint32_t tail_offset = header_->producer_tail.load(std::memory_order_acquire);
        Node* tail_node = queue_->node_at_offset(tail_offset);
        
        // 2. 计算下一个写入位置
        uint32_t next_offset = tail_node->next_offset;
        
        // 3. 检查是否会覆盖最慢消费者
        uint32_t slowest_consumer_head = get_slowest_consumer_head();
        if (next_offset == slowest_consumer_head) {
            // 队列满（相对于最慢消费者）
            
            // 根据阻塞模式决定行为
            if (blocking_mode_ == BlockingMode::BLOCKING) {
                // 阻塞等待消费者消费
                cond_not_full.wait(lock);
                continue;
            } else {
                // 非阻塞模式：返回失败（不覆盖消费者数据）
                return false;
            }
        }
        
        // 4. 写入数据到当前尾节点
        tail_node->sequence.store(++sequence_counter_, std::memory_order_release);
        tail_node->data_size.store(size, std::memory_order_release);
        tail_node->timestamp.store(get_timestamp_ns(), std::memory_order_release);
        std::memcpy(tail_node->data, data, size);
        tail_node->valid.store(true, std::memory_order_release);
        
        // 5. 推进生产者尾指针
        header_->producer_tail.store(next_offset, std::memory_order_release);
        header_->total_pushed.fetch_add(1, std::memory_order_relaxed);
        
        // 6. 通知等待的消费者
        header_->cond_not_empty.notify_all();
        
        return true;
    }
}

uint32_t get_slowest_consumer_head() {
    uint32_t slowest = header_->producer_head.load(std::memory_order_acquire);
    
    for (int i = 0; i < MAX_CONSUMERS; ++i) {
        if (!header_->consumer_slots[i].active.load(std::memory_order_acquire)) {
            continue;
        }
        
        uint32_t consumer_head = header_->consumer_slots[i].head_offset.load(
            std::memory_order_acquire
        );
        
        // 计算消费者落后的距离（环形队列）
        if (is_behind(consumer_head, slowest)) {
            slowest = consumer_head;
        }
    }
    
    return slowest;
}
```

### 3.2 消费者 Pop 逻辑

```cpp
int SharedRingQueueConsumer::pop(void* data_buf, uint32_t& out_size) {
    if (!registered_) {
        return -1;  // 未注册
    }
    
    scoped_lock<interprocess_mutex> lock(header_->global_mutex);
    
    ConsumerSlot& my_slot = header_->consumer_slots[consumer_id_];
    
    while (true) {
        // 1. 获取我的头指针
        uint32_t my_head = my_slot.head_offset.load(std::memory_order_acquire);
        
        // 2. 获取生产者尾指针
        uint32_t producer_tail = header_->producer_tail.load(std::memory_order_acquire);
        
        // 3. 检查是否有数据可读
        if (my_head == producer_tail) {
            // 队列空（对于当前消费者）
            
            // 根据阻塞模式决定行为
            if (blocking_mode_ == BlockingMode::BLOCKING) {
                // 阻塞等待生产者生产
                bool timeout = !header_->cond_not_empty.timed_wait(
                    lock, 
                    boost::posix_time::milliseconds(timeout_ms_)
                );
                if (timeout) {
                    return 0;  // 超时
                }
                continue;
            } else {
                // 非阻塞模式：返回队列空
                return 0;
            }
        }
        
        // 4. 读取当前头节点的数据
        Node* head_node = queue_->node_at_offset(my_head);
        
        if (!head_node->valid.load(std::memory_order_acquire)) {
            // 数据无效（可能被覆盖）
            header_->overwrite_count.fetch_add(1, std::memory_order_relaxed);
            // 跳过这个节点
            my_slot.head_offset.store(head_node->next_offset, std::memory_order_release);
            continue;
        }
        
        // 5. 拷贝数据
        out_size = head_node->data_size.load(std::memory_order_acquire);
        std::memcpy(data_buf, head_node->data, out_size);
        
        // 6. 推进我的头指针
        my_slot.head_offset.store(head_node->next_offset, std::memory_order_release);
        my_slot.last_access_time.store(get_timestamp_ns(), std::memory_order_relaxed);
        
        header_->total_popped.fetch_add(1, std::memory_order_relaxed);
        
        // 7. 通知等待的生产者
        header_->cond_not_full.notify_all();
        
        return 1;  // 成功
    }
}
```

### 3.3 消费者注册逻辑

```cpp
int SharedRingQueueConsumer::register_consumer(ConsumerStartMode start_mode) {
    scoped_lock<interprocess_mutex> lock(header_->global_mutex);
    
    // 1. 分配消费者ID
    int id = -1;
    for (int i = 0; i < MAX_CONSUMERS; ++i) {
        bool expected = false;
        if (header_->consumer_slots[i].active.compare_exchange_strong(
                expected, true, std::memory_order_acquire)) {
            id = i;
            break;
        }
    }
    
    if (id < 0) {
        throw std::runtime_error("No available consumer slots");
    }
    
    // 2. 确定起始读取位置
    uint32_t start_offset;
    uint32_t producer_tail = header_->producer_tail.load(std::memory_order_acquire);
    uint32_t producer_head = header_->producer_head.load(std::memory_order_acquire);
    
    switch (start_mode) {
        case ConsumerStartMode::FROM_BEGINNING:
            // 从队列开头（producer_head）开始
            start_offset = producer_head;
            break;
            
        case ConsumerStartMode::FROM_LATEST:
            // 从当前写入位置（producer_tail）开始
            start_offset = producer_tail;
            break;
            
        case ConsumerStartMode::FROM_OLDEST_AVAILABLE:
            // 从最旧可用数据开始（考虑环形覆盖）
            if (is_queue_full()) {
                // 队列满，从 tail 的下一个位置开始（最旧数据）
                Node* tail_node = queue_->node_at_offset(producer_tail);
                start_offset = tail_node->next_offset;
            } else {
                // 队列未满，从 head 开始
                start_offset = producer_head;
            }
            break;
    }
    
    // 3. 初始化消费者槽位
    header_->consumer_slots[id].head_offset.store(start_offset, std::memory_order_release);
    header_->consumer_slots[id].last_access_time.store(
        get_timestamp_ns(), 
        std::memory_order_relaxed
    );
    
    header_->active_consumer_count.fetch_add(1, std::memory_order_relaxed);
    
    return id;
}
```

### 3.4 僵尸共享内存清理逻辑

```cpp
bool is_zombie_shared_memory(const std::string& shm_name) {
    try {
        shared_memory_object test_shm(open_only, shm_name.c_str(), read_only);
        mapped_region test_region(test_shm, read_only);
        
        RingQueueHeader* header = static_cast<RingQueueHeader*>(
            test_region.get_address()
        );
        
        uint64_t producer_pid = header->producer_pid.load(std::memory_order_acquire);
        
        // 检查生产者进程是否还在运行
        if (!is_process_running(producer_pid)) {
            std::cerr << "Found zombie shared memory: " << shm_name 
                      << " (dead producer PID: " << producer_pid << ")" 
                      << std::endl;
            return true;
        }
        
        return false;
    } catch (const interprocess_exception&) {
        // 共享内存不存在
        return false;
    }
}

void cleanup_zombie_shared_memory(const std::string& shm_name) {
    if (is_zombie_shared_memory(shm_name)) {
        shared_memory_object::remove(shm_name.c_str());
        std::cerr << "Cleaned up zombie shared memory: " << shm_name << std::endl;
    }
}
```

---

## 4. Python 接口设计

### 4.1 装饰器接口（保持兼容）

```python
# 生产者装饰器
@sharedmem_producer({
    'shm_name': 'my_queue',
    'node_count': 1024,
    'block_size': 4096,
    'interval': 0.01  # 无消费者时的调用间隔
})
def my_producer():
    # 生成数据
    data = generate_data()
    return data  # bytes

# 消费者装饰器
@sharedmem_consumer({
    'shm_name': 'my_queue',
    'node_count': 1024,
    'block_size': 4096,
    'batch_size': 10,
    'timeout_ms': 100,
    'start_mode': 'FROM_OLDEST_AVAILABLE'
})
def my_consumer(batch):
    # batch: list[bytes]
    for data in batch:
        process(data)

# 消费者+生产者（处理链）
@sharedmem_producer({
    'shm_name': 'output_queue',
    'node_count': 1024,
    'block_size': 4096
})
@sharedmem_consumer({
    'shm_name': 'input_queue',
    'node_count': 1024,
    'block_size': 4096,
    'batch_size': 10
})
def my_processor(batch):
    for data in batch:
        processed = process(data)
        yield processed  # bytes
```

### 4.2 底层 C++ 绑定接口

```cpp
PYBIND11_MODULE(multiqueue_shm, m) {
    py::enum_<ConsumerStartMode>(m, "ConsumerStartMode")
        .value("FROM_BEGINNING", ConsumerStartMode::FROM_BEGINNING)
        .value("FROM_LATEST", ConsumerStartMode::FROM_LATEST)
        .value("FROM_OLDEST_AVAILABLE", ConsumerStartMode::FROM_OLDEST_AVAILABLE);
    
    py::class_<SharedRingQueueProducer>(m, "Producer")
        .def(py::init<const std::string&, uint32_t, uint32_t, const std::string&>(),
             py::arg("shm_name"),
             py::arg("node_count"),
             py::arg("block_size"),
             py::arg("metadata") = "")
        .def("push", [](SharedRingQueueProducer& self, py::bytes data) {
            std::string s = data;
            return self.push(s.data(), s.size());
        })
        .def("metadata", &SharedRingQueueProducer::metadata)
        .def("is_active", &SharedRingQueueProducer::is_active);
    
    py::class_<SharedRingQueueConsumer>(m, "Consumer")
        .def(py::init<const std::string&, uint32_t, uint32_t, ConsumerStartMode>(),
             py::arg("shm_name"),
             py::arg("node_count"),
             py::arg("block_size"),
             py::arg("start_mode") = ConsumerStartMode::FROM_LATEST)
        .def("pop", [](SharedRingQueueConsumer& self) -> py::object {
            std::vector<char> buf(self.node_size());
            uint32_t size = 0;
            int result = self.pop(buf.data(), size);
            if (result > 0) {
                return py::bytes(buf.data(), size);
            } else if (result == 0) {
                return py::none();  // 超时或队列空
            } else {
                throw std::runtime_error("Pop failed");
            }
        })
        .def("unregister", &SharedRingQueueConsumer::unregister)
        .def("metadata", &SharedRingQueueConsumer::metadata);
    
    py::class_<SharedMemProcessor>(m, "Processor")
        .def(py::init<const std::string&, uint32_t, uint32_t,
                      const std::string&, uint32_t, uint32_t,
                      const std::string&, size_t, int>(),
             py::arg("in_shm"),
             py::arg("in_node_count"),
             py::arg("in_block_size"),
             py::arg("out_shm"),
             py::arg("out_node_count"),
             py::arg("out_block_size"),
             py::arg("metadata") = "",
             py::arg("batch_size") = 1,
             py::arg("timeout_ms") = 100)
        .def("register_callback", &SharedMemProcessor::register_callback)
        .def("start", &SharedMemProcessor::start)
        .def("stop", &SharedMemProcessor::stop);
}
```

---

## 5. 关键特性对比

| 特性 | 旧设计（当前） | 新设计（基于 sharedatastream） |
|------|--------------|-------------------------------|
| **同步机制** | `std::atomic` | `interprocess_mutex` + `interprocess_condition` |
| **阻塞支持** | 自旋等待 + `sleep` | 条件变量（高效） |
| **消费者注册** | ConsumerRegistry + CAS | 消费者ID分配 + 活跃标志 |
| **节点结构** | 固定大小元素 + Header | 链式 Node + 灵活大小 |
| **覆盖保护** | 检查 `slowest_offset` | 检查 `slowest_consumer_head` |
| **僵尸清理** | ❌ 无 | ✅ 检测生产者 PID |
| **Python 接口** | 直接绑定 | 装饰器 + 内部线程队列 |
| **多队列支持** | ❌ 未实现 | ✅ Processor 支持输入+输出 |

---

## 6. 实施计划

### Phase 1: 核心重构（1-2天）
1. 重写 `metadata.hpp` - 新的 `RingQueueHeader` 和 `Node` 结构
2. 重写 `ring_queue.hpp` - 使用 Boost.Interprocess 同步原语
3. 实现 `SharedRingQueueProducer` 和 `SharedRingQueueConsumer`
4. 实现僵尸共享内存检测和清理

### Phase 2: Python 绑定（1天）
1. 重写 `multiqueue_python.cpp` - 基础绑定
2. 实现 `SharedMemProcessor` - 输入/输出队列 + 回调机制
3. 实现装饰器接口 `processor_decorator.py`

### Phase 3: 测试和优化（1-2天）
1. 单元测试（C++）
2. 集成测试（Python）
3. 性能测试和压力测试
4. 跨平台测试（Windows + Linux + macOS）

### Phase 4: 文档和示例（1天）
1. API 文档
2. 使用示例
3. 迁移指南

---

## 7. 向后兼容性

### 保留接口（尽量兼容）
- `RingQueue` 类名保留，但内部实现完全重写
- `QueueConfig` 配置结构保留
- `push()` / `pop()` 方法签名保持一致

### 不兼容变更
- 移除 `QueueManager` 和 `TimestampSynchronizer`（Phase 2 重新实现）
- 移除基于模板的类型绑定（改用 `void*` + size）
- 移除 `enable_async` 配置（内部线程队列实现）

---

## 8. 下一步行动

**现在请确认：**
1. ✅ 是否接受这个新设计方案？
2. ✅ 是否立即开始重构？
3. ✅ 是否需要保留旧代码作为备份？

如果确认，我将：
1. 创建 `commit/2025-11-22_architecture_redesign_plan.md` 记录本次重大变更
2. 备份现有代码到 `multiqueue-shm-old/`
3. 开始实施 Phase 1 的核心重构

**请回复您的决定！** 🚀

