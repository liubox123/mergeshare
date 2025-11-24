# 多消费者广播模式设计

**日期**: 2025-11-20  
**问题**: 当前实现是竞争消费模式，需要改为广播模式

---

## 🎯 需求分析

### 当前问题
- 所有消费者竞争同一个 `read_offset`
- 一个消费者读取后，其他消费者看不到该数据
- 这是 **竞争消费模式**（Competing Consumers），而不是 **广播模式**（Multicast）

### 用户需求
1. 每个消费者独立记录自己的读取位置
2. 所有消费者都能读到所有数据（广播）
3. 生产者不能覆盖最慢消费者还未读取的数据
4. 支持 Windows 和 POSIX 平台（Boost.Interprocess）

---

## 🏗️ 新架构设计

### 1. 消费者注册表

```cpp
// 最大消费者数量
constexpr size_t MAX_CONSUMERS = 32;

struct ConsumerSlot {
    std::atomic<uint64_t> read_offset;      // 消费者的读取位置
    std::atomic<bool> active;               // 是否活跃
    char consumer_id[32];                   // 消费者标识
    std::atomic<uint64_t> last_access_time; // 最后访问时间
    char padding[64 - sizeof(std::atomic<uint64_t>) * 2 - 
                 sizeof(std::atomic<bool>) - 32];
} __attribute__((aligned(64)));

struct ConsumerRegistry {
    ConsumerSlot slots[MAX_CONSUMERS];
    std::atomic<uint32_t> active_count;
};
```

### 2. 修改后的 ControlBlock

```cpp
struct ControlBlock {
    // 生产者控制
    std::atomic<uint64_t> write_offset;
    
    // 消费者注册表（内嵌）
    ConsumerRegistry consumers;
    
    // 统计信息
    std::atomic<uint64_t> total_pushed;
    std::atomic<uint32_t> producer_count;
    std::atomic<uint32_t> status_flags;
};
```

### 3. 生产者逻辑

```cpp
bool push(const T& data) {
    // 1. 获取当前写入位置
    uint64_t current_write = write_offset.load();
    
    // 2. 计算下一个写入位置
    uint64_t next_write = current_write + 1;
    
    // 3. 查找最慢的消费者
    uint64_t slowest_read = get_slowest_consumer_offset();
    
    // 4. 检查是否会覆盖未读数据
    if (next_write - slowest_read >= capacity) {
        // 阻塞模式：等待
        // 非阻塞模式：覆盖或拒绝
    }
    
    // 5. 写入数据
    write_element(current_write % capacity, data);
    
    // 6. 更新写入偏移
    write_offset.store(next_write);
}
```

### 4. 消费者逻辑

```cpp
bool pop(T& data) {
    // 1. 获取当前消费者的 slot
    ConsumerSlot& my_slot = get_my_slot();
    
    // 2. 读取我的位置
    uint64_t my_read = my_slot.read_offset.load();
    uint64_t current_write = write_offset.load();
    
    // 3. 检查是否有数据
    if (my_read >= current_write) {
        // 队列空
        return false;
    }
    
    // 4. 读取数据
    read_element(my_read % capacity, data);
    
    // 5. 更新我的读取位置
    my_slot.read_offset.store(my_read + 1);
    
    return true;
}
```

---

## 🔧 平台支持

### Windows 特殊处理

```cpp
#ifdef _WIN32
    // Windows: 使用 windows_shared_memory
    #include <boost/interprocess/windows_shared_memory.hpp>
    using shared_memory_type = boost::interprocess::windows_shared_memory;
#else
    // POSIX: 使用 shared_memory_object
    #include <boost/interprocess/shared_memory_object.hpp>
    using shared_memory_type = boost::interprocess::shared_memory_object;
#endif
```

### 原子操作

```cpp
#ifdef _WIN32
    #include <windows.h>
    // Windows 原子操作
#else
    #include <stdatomic.h>
    // POSIX 原子操作
#endif
```

---

## 📊 消费者管理 API

```cpp
class RingQueue {
public:
    // 注册消费者
    int register_consumer(const std::string& consumer_id);
    
    // 注销消费者
    void unregister_consumer(int slot_id);
    
    // 获取消费者状态
    ConsumerInfo get_consumer_info(int slot_id);
    
    // 获取最慢的消费者
    uint64_t get_slowest_consumer_offset();
    
private:
    int my_consumer_slot_ = -1;  // 当前消费者的 slot
};
```

---

## 🎯 使用示例

### 生产者

```cpp
RingQueue<int> queue("data_queue", config);

// 生产者不需要注册
queue.push(42);
```

### 消费者

```cpp
RingQueue<int> queue("data_queue", config);

// 每个消费者注册自己的 ID
int my_slot = queue.register_consumer("consumer_1");

// 读取数据
int data;
while (queue.pop(data)) {
    process(data);
}

// 退出时注销
queue.unregister_consumer(my_slot);
```

---

## ⚠️ 注意事项

1. **容量限制**: `capacity` 必须大于等于 `MAX_CONSUMERS`
2. **超时清理**: 长时间不活动的消费者应被自动清理
3. **内存开销**: 每个队列额外需要 `MAX_CONSUMERS * 64` 字节
4. **性能**: 生产者需要扫描所有活跃消费者，时间复杂度 O(n)

---

## 🚀 实施计划

1. **Phase 4.5**: 重构 metadata.hpp - 添加 ConsumerRegistry
2. **Phase 4.6**: 重构 ring_queue.hpp - 支持多消费者
3. **Phase 4.7**: 添加 Windows 平台宏控制
4. **Phase 4.8**: 更新测试用例
5. **Phase 4.9**: 更新 Python 绑定

---

**优先级**: ⭐⭐⭐⭐⭐ 高优先级  
**预计工作量**: 4-6 小时

