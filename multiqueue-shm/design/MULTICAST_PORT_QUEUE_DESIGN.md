# PortQueue 广播模式设计

## 问题分析

### 当前实现的问题
当前 `PortQueue` 是标准的 MPMC（多生产者多消费者）队列：
- 只有**一个** `head` 指针（读位置）
- 多个消费者**竞争**读取数据
- 每个 Buffer 只能被**一个**消费者拿到

### 用户需求
- **广播/多播模式**：同一个 Buffer 应该被**所有**消费者都能拿到
- 每个消费者应该看到**完整的**数据流
- 这是典型的 **Pub-Sub**（发布-订阅）模式

## 设计方案

### 方案 1：独立读指针 + 引用计数（推荐）

#### 数据结构
```cpp
struct PortQueueHeader {
    // 基本信息
    uint32_t magic_number;
    PortId port_id;
    size_t capacity;
    
    // 写指针（生产者共享）
    std::atomic<size_t> tail;
    
    // 消费者管理
    std::atomic<uint32_t> consumer_count;  // 已注册的消费者数量
    static constexpr uint32_t MAX_CONSUMERS = 16;  // 最大消费者数
    
    // 每个消费者的读指针（独立）
    std::atomic<size_t> consumer_heads[MAX_CONSUMERS];
    std::atomic<bool> consumer_active[MAX_CONSUMERS];  // 消费者是否活跃
    
    // 同步原语
    interprocess_mutex mutex;
    interprocess_condition not_empty[MAX_CONSUMERS];  // 每个消费者独立的条件变量
    interprocess_condition not_full;
    
    // 状态
    std::atomic<bool> initialized;
    std::atomic<bool> closed;
};
```

#### 内存布局
```
[PortQueueHeader]
[BufferId array: capacity]
```

#### 核心逻辑

##### 1. 消费者注册
```cpp
ConsumerId register_consumer() {
    scoped_lock<interprocess_mutex> lock(header_->mutex);
    
    // 查找空闲槽位
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        if (!header_->consumer_active[i].load()) {
            // 初始化消费者读指针为当前 tail
            header_->consumer_heads[i].store(
                header_->tail.load(), 
                std::memory_order_release
            );
            header_->consumer_active[i].store(true, std::memory_order_release);
            header_->consumer_count.fetch_add(1, std::memory_order_release);
            return i;
        }
    }
    
    return INVALID_CONSUMER_ID;  // 消费者数量已满
}
```

##### 2. 生产者 push
```cpp
bool push(BufferId buffer_id) {
    scoped_lock<interprocess_mutex> lock(header_->mutex);
    
    // 检查队列是否满：找到最慢的消费者
    size_t min_head = header_->tail.load();
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        if (header_->consumer_active[i].load()) {
            size_t head = header_->consumer_heads[i].load();
            if (is_earlier(head, min_head)) {  // 考虑环形队列的回绕
                min_head = head;
            }
        }
    }
    
    // 如果最慢的消费者落后 capacity，则队列满
    size_t tail = header_->tail.load();
    if ((tail - min_head + capacity) % capacity >= capacity) {
        header_->not_full.wait(lock);
        // 重新检查...
    }
    
    // 写入数据
    data_[tail % capacity] = buffer_id;
    
    // 增加 buffer 引用计数（关键！）
    increment_ref_count(buffer_id, header_->consumer_count.load());
    
    // 更新 tail
    header_->tail.fetch_add(1, std::memory_order_release);
    
    // 通知所有消费者
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        if (header_->consumer_active[i].load()) {
            header_->not_empty[i].notify_one();
        }
    }
    
    return true;
}
```

##### 3. 消费者 pop
```cpp
bool pop(ConsumerId consumer_id, BufferId& buffer_id) {
    scoped_lock<interprocess_mutex> lock(header_->mutex);
    
    // 获取该消费者的读指针
    size_t head = header_->consumer_heads[consumer_id].load();
    size_t tail = header_->tail.load();
    
    // 检查是否有数据
    if (head == tail) {
        header_->not_empty[consumer_id].wait(lock);
        // 重新检查...
    }
    
    // 读取数据
    buffer_id = data_[head % capacity];
    
    // 更新该消费者的读指针
    header_->consumer_heads[consumer_id].fetch_add(1, std::memory_order_release);
    
    // 检查是否是最后一个消费者读取
    bool all_consumed = true;
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        if (header_->consumer_active[i].load()) {
            if (header_->consumer_heads[i].load() <= head) {
                all_consumed = false;
                break;
            }
        }
    }
    
    // 如果所有消费者都读取了该位置，通知生产者
    if (all_consumed) {
        header_->not_full.notify_one();
    }
    
    return true;
}
```

##### 4. 消费者注销
```cpp
void unregister_consumer(ConsumerId consumer_id) {
    scoped_lock<interprocess_mutex> lock(header_->mutex);
    
    // 释放该消费者尚未读取的所有 buffer 的引用
    size_t head = header_->consumer_heads[consumer_id].load();
    size_t tail = header_->tail.load();
    for (size_t i = head; i < tail; ++i) {
        BufferId buffer_id = data_[i % capacity];
        decrement_ref_count(buffer_id);
    }
    
    // 标记为非活跃
    header_->consumer_active[consumer_id].store(false, std::memory_order_release);
    header_->consumer_count.fetch_sub(1, std::memory_order_release);
    
    // 通知生产者（可能现在有空间了）
    header_->not_full.notify_one();
}
```

### 方案 2：为每个消费者创建独立队列（备选）

#### 优点
- 实现简单，每个消费者有独立的 PortQueue
- 消费者之间完全隔离，无锁竞争

#### 缺点
- **内存开销大**：N 个消费者需要 N 倍内存
- **引用计数复杂**：需要等所有队列都消费完才能释放 Buffer
- **动态添加消费者困难**

## 推荐方案

**方案 1（独立读指针 + 引用计数）**是更好的选择：

### 优点
1. ✅ 内存效率高：只需一个队列
2. ✅ 真正的广播：所有消费者看到相同数据
3. ✅ 支持动态添加/删除消费者
4. ✅ 引用计数自动管理 Buffer 生命周期

### 需要注意
1. ⚠️ 最大消费者数量限制（可配置，如 16）
2. ⚠️ 慢消费者会阻塞快消费者（需要监控）
3. ⚠️ 需要在 `push` 时正确设置 Buffer 引用计数

## 实现计划

### Phase 1: 修改 PortQueue
- [ ] 更新 `PortQueueHeader` 结构
- [ ] 实现 `register_consumer()` / `unregister_consumer()`
- [ ] 修改 `push()` 逻辑（引用计数）
- [ ] 修改 `pop()` 逻辑（消费者独立读取）

### Phase 2: 修改 Block
- [ ] `OutputPort` 在连接时注册消费者
- [ ] `InputPort` 在断开时注销消费者
- [ ] 传递 `ConsumerId` 给 `pop()`

### Phase 3: 测试
- [ ] 单元测试：单生产者 + 多消费者
- [ ] 压力测试：快/慢消费者混合
- [ ] 多进程测试：跨进程广播

## API 变更

### PortQueue
```cpp
class PortQueue {
public:
    // 新增：消费者管理
    ConsumerId register_consumer();
    void unregister_consumer(ConsumerId id);
    
    // 修改：pop 需要传入 consumer_id
    bool pop(ConsumerId consumer_id, BufferId& buffer_id);
    bool pop_with_timeout(ConsumerId consumer_id, BufferId& buffer_id, uint32_t timeout_ms);
    
    // 不变：push
    bool push(BufferId buffer_id);
    bool push_with_timeout(BufferId buffer_id, uint32_t timeout_ms);
};
```

### Block (InputPort)
```cpp
class InputPort {
private:
    ConsumerId consumer_id_;  // 新增
    
public:
    bool connect(PortQueue* queue) {
        queue_ = queue;
        consumer_id_ = queue->register_consumer();  // 注册
        return consumer_id_ != INVALID_CONSUMER_ID;
    }
    
    void disconnect() {
        if (queue_) {
            queue_->unregister_consumer(consumer_id_);  // 注销
            queue_ = nullptr;
        }
    }
    
    BufferPtr pop() {
        BufferId buffer_id;
        if (queue_->pop(consumer_id_, buffer_id)) {  // 传入 consumer_id
            return BufferPtr(allocator_, buffer_id);
        }
        return BufferPtr();
    }
};
```

## 测试用例设计

### 测试 1：单生产者 + 2 消费者
```cpp
// 生产者生产 100 个 buffer
// 消费者 A 应该收到 100 个
// 消费者 B 应该收到 100 个
// 总计：200 次读取，但只有 100 个 buffer
```

### 测试 2：单生产者 + 3 消费者（慢消费者）
```cpp
// 消费者 A: 正常速度
// 消费者 B: 慢速（每次 sleep）
// 消费者 C: 快速
// 验证：所有消费者都收到完整数据
```

### 测试 3：动态添加消费者
```cpp
// 生产者开始生产
// 消费者 A 从头消费
// 生产到一半时，添加消费者 B
// 验证：B 从注册时刻开始收到数据
```

## 性能考虑

### 锁争用
- 每次 push/pop 都需要锁
- 可以考虑使用读写锁优化

### 慢消费者问题
- 最慢的消费者会限制队列大小
- 需要监控和告警机制
- 可选：支持"丢弃模式"（慢消费者跳过旧数据）

### 引用计数开销
- 每次 push 需要增加 N 次引用计数（N = 消费者数）
- 可以批量操作优化

## 总结

这个设计将 `PortQueue` 从**竞争模式**改为**广播模式**，是实现多消费者架构的关键。需要仔细处理引用计数和同步逻辑，确保线程安全和正确性。



