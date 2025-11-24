# MultiQueue-SHM 完整架构总结

> **版本**: v2.0  
> **日期**: 2025-11-24  
> **状态**: 设计完成，待审阅

---

## 🎯 核心需求

✅ **多进程/多线程架构**  
✅ **支持多入多出**  
✅ **时间戳同步/异步模式**  
✅ **零拷贝共享内存**  
✅ **跨进程引用计数**  
✅ **流速不匹配处理**

---

## ⚠️ 核心设计原则：多进程优先

> **严格要求：按多进程模式设计，单进程多线程只是特例**

```
┌──────────────────────────────────────┐
│  多进程架构（核心设计）              │
│  - 所有状态在共享内存                │
│  - interprocess_mutex/atomic         │
│  - 跨进程引用计数                    │
│  - Buffer ID 传递                    │
└───────────────┬──────────────────────┘
                │
                ↓ 自动支持
                │
┌───────────────┴──────────────────────┐
│  单进程多线程（特例，可选优化）       │
│  - 共享内存在进程内也是"共享"的      │
│  - 原子操作在线程间也是安全的        │
│  - 可选：std::mutex 替代 interprocess│
└──────────────────────────────────────┘
```

### 关键要求

| 要求 | 说明 | 状态 |
|------|------|------|
| **所有状态在共享内存** | BufferMetadata、Registry、PortQueue 全部在共享内存 | ✅ |
| **跨进程同步原语** | interprocess_mutex、interprocess_condition、std::atomic（在共享内存中） | ✅ |
| **不依赖进程内存** | 不使用进程内的指针、std::mutex（只能线程间）、进程本地状态 | ✅ |
| **只传递 Buffer ID** | 不传递指针，使用 uint64_t Buffer ID | ✅ |
| **使用相对偏移** | 数据地址使用相对于共享内存基地址的偏移量 | ✅ |
| **跨进程引用计数** | BufferMetadata 中的 ref_count 是共享内存中的 atomic | ✅ |
| **进程崩溃清理** | 心跳机制检测死进程，清理其持有的资源 | ✅ |

### 详细设计文档

参见：[多进程 Buffer 管理详细设计](./MULTIPROCESS_BUFFER_MANAGEMENT.md)

---

## 📐 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    Shared Memory Region                          │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Global Registry (全局注册表)                              │ │
│  │  ├─ Process Registry     (进程注册表)                      │ │
│  │  ├─ Block Registry       (Block 注册表)                    │ │
│  │  ├─ Connection Registry  (连接注册表)                      │ │
│  │  ├─ BufferPool Registry  (Buffer 池注册表)                │ │
│  │  └─ MessageBus           (消息总线)                        │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Buffer Pools (多个内存池)                                 │ │
│  │  ├─ BufferMetadata Table (元数据表 - 包含时间戳)          │ │
│  │  ├─ Pool 4KB  (1024 blocks)                               │ │
│  │  ├─ Pool 64KB (512 blocks)                                │ │
│  │  └─ Pool 1MB  (128 blocks)                                │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Port Queues (端口队列 - 支持时间戳)                       │ │
│  │  - 每个输入端口一个队列                                     │ │
│  │  - interprocess_mutex + interprocess_condition             │ │
│  │  - 存储 Buffer ID（不是指针）                              │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                          ↑           ↑           ↑
                          │           │           │
              ┌───────────┴─┐    ┌───┴────────┐  │
              │  Process 1  │    │ Process 2  │  │
              │             │    │            │  │
              │ ┌─────────┐ │    │┌─────────┐│  │
              │ │Block A  │ │    ││Block B  ││  │
              │ │(Source) │ │    ││(Sync    ││  │
              │ │         │ │    ││Merger)  ││  │
              │ │- 3 Out  │ │    ││- 3 In   ││  │
              │ │  Ports  │ │    ││- 1 Out  ││  │
              │ └─────────┘ │    │└─────────┘│  │
              │             │    │            │  │
              │ Heartbeat   │    │ Heartbeat  │  │
              │ Thread      │    │ Thread     │  │
              └─────────────┘    └────────────┘  │
                                                  │
                                    ┌─────────────┴──┐
                                    │   Process 3    │
                                    │                │
                                    │  ┌──────────┐  │
                                    │  │ Block C  │  │
                                    │  │ (Sink)   │  │
                                    │  │ - 1 In   │  │
                                    │  └──────────┘  │
                                    │                │
                                    │  Heartbeat     │
                                    └────────────────┘
```

---

## 🔑 核心设计决策

### 1. 多进程架构 🔄

**特点**：
- 每个进程独立运行一个或多个 Block
- 进程崩溃不影响其他进程
- 支持分布式部署
- 进程间通过共享内存通信

**进程类型**：
```cpp
enum class ProcessRole {
    STANDALONE,      // 独立 Block 进程
    RUNTIME_SERVER,  // 运行时服务器（监控、清理）
    WORKER           // 工作进程（多个 Block）
};
```

### 2. 跨进程引用计数 🔢

**关键设计**：
```cpp
struct BufferMetadata {  // 在共享内存中
    uint64_t buffer_id;
    std::atomic<uint32_t> ref_count;  // 跨进程原子操作 ✅
    uint64_t data_offset;             // 相对偏移（不是指针）✅
    Timestamp timestamp;              // 时间戳信息 ✅
    // ...
};
```

**优点**：
- 支持多消费者（广播模式）
- 自动回收（引用计数归零）
- 进程退出自动清理

### 3. 多入多出 Block 🔀

**端口设计**：
```cpp
class Block {
    // 支持任意数量的输入/输出端口
    void add_input_port(const std::string& name, PortConfig config);
    void add_output_port(const std::string& name);
    
    // 多输入读取
    BufferPtr get_input(const std::string& port_name, uint32_t timeout_ms);
    
    // 多输出发布
    void produce_output(const std::string& port_name, BufferPtr buffer);
};
```

**典型模式**：
- **多入单出**：合并器（Merger）
- **单入多出**：分离器（Splitter）
- **多入多出**：路由器（Router）

### 4. 时间戳同步 ⏱️

**时间戳类型**：
```cpp
enum class TimestampType {
    NONE,           // 无时间戳（异步模式）
    ABSOLUTE,       // 绝对时间（Unix 纳秒）
    RELATIVE,       // 相对时间
    SAMPLE_INDEX    // 采样索引（固定采样率）
};
```

**同步策略**：
```cpp
enum class SyncPolicy {
    EXACT_MATCH,            // 精确匹配（容差内）
    NEAREST_NEIGHBOR,       // 最近邻选择
    LINEAR_INTERPOLATION,   // 线性插值
    WINDOW_AGGREGATION      // 窗口聚合
};
```

**工作模式**：
```cpp
enum class BlockWorkMode {
    ASYNC,   // 异步模式（自由流，高吞吐）
    SYNC,    // 同步模式（时间对齐，保证一致性）
    HYBRID   // 混合模式
};
```

### 5. 全局注册表 📋

**作用**：
- 进程发现和注册
- Block 注册和查询
- 连接关系维护
- Buffer 池管理

**存储位置**：共享内存（所有进程可见）

### 6. 心跳和容错 💓

**机制**：
```cpp
// 每个进程定期更新心跳
void heartbeat_thread() {
    while (running) {
        registry->update_heartbeat(process_slot);
        sleep(1s);
    }
}

// Runtime Server 检测僵尸进程
void cleanup_thread() {
    while (running) {
        for (each process) {
            if (heartbeat_timeout > 5s) {
                cleanup_process_resources();
            }
        }
        sleep(2s);
    }
}
```

---

## 📊 关键数据结构

### 1. GlobalRegistry（共享内存）

```cpp
struct GlobalRegistry {
    uint64_t magic_number;
    uint32_t version;
    
    ProcessRegistry process_registry;
    BlockRegistry block_registry;
    ConnectionRegistry connection_registry;
    BufferPoolRegistry buffer_pool_registry;
    BufferMetadataTable buffer_metadata_table;
    
    MessageBus message_bus;
    
    std::atomic<uint64_t> total_buffers_allocated;
    std::atomic<uint64_t> total_bytes_transferred;
};
```

### 2. BufferMetadata（共享内存）

```cpp
struct alignas(64) BufferMetadata {
    uint64_t buffer_id;
    
    uint32_t pool_id;
    uint32_t offset_in_pool;
    size_t size;
    
    std::atomic<uint32_t> ref_count;  // 跨进程引用计数
    
    // 时间戳信息
    Timestamp timestamp;
    Timestamp start_time;
    Timestamp end_time;
    bool has_time_range;
    
    uint64_t data_offset;  // 相对于共享内存基地址
    std::atomic<bool> valid;
};
```

### 3. PortQueue（共享内存）

```cpp
struct alignas(64) PortQueue {
    interprocess_mutex mutex;
    interprocess_condition not_empty;
    interprocess_condition not_full;
    
    uint32_t capacity;
    std::atomic<uint32_t> size;
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    
    uint64_t buffer_ids[MAX_QUEUE_SIZE];  // 存储 ID，不是指针
};
```

### 4. Block（进程内）

```cpp
class Block {
    uint64_t block_id_;
    GlobalRegistry* registry_;
    
    std::unordered_map<std::string, PortQueue*> input_queues_;
    std::unordered_map<std::string, std::vector<uint64_t>> output_connections_;
    
    virtual WorkResult work() = 0;
};
```

### 5. SyncBlock（进程内，支持时间同步）

```cpp
class SyncBlock : public Block {
    BlockWorkMode work_mode_;
    SyncPolicy sync_policy_;
    uint64_t sync_tolerance_ns_;
    
    WorkResult work_sync();  // 同步工作
    WorkResult work_async(); // 异步工作
    
    bool align_buffers(...);  // 时间戳对齐
};
```

---

## 🔄 数据流

### 典型场景：3 路同步合并

```
Process 1 (FileSource A)
    ↓ [Buffer ID: 1001, Timestamp: 100ms]
    ↓ → PortQueue (in1)
             ↓
Process 2 (FileSource B)           Process 3 (SyncMerger)
    ↓ [Buffer ID: 2001, Timestamp: 100ms]    ← 读取 3 个端口
    ↓ → PortQueue (in2) ──────────────────→  - 检查时间戳
             ↓                                 - 对齐数据
Process 4 (FileSource C)                     - 合并输出
    ↓ [Buffer ID: 3001, Timestamp: 100ms]    
    ↓ → PortQueue (in3) ──────────────────→  [Buffer ID: 4001, Timestamp: 100ms]
                                                    ↓
                                              Process 5 (FileSink)
```

**时间戳对齐过程**：

1. **读取输入**：从 3 个端口读取数据到缓冲区
2. **检查时间戳**：
   - in1: [99ms, 100ms, 101ms, 102ms]
   - in2: [100ms, 101ms, 102ms]
   - in3: [100ms, 101ms, 103ms]
3. **对齐**（EXACT_MATCH，容差 1ms）：
   - 选择 100ms 作为目标时间戳
   - in1 → 100ms
   - in2 → 100ms
   - in3 → 100ms
4. **合并**：将 3 个 Buffer 合并为 1 个
5. **输出**：设置输出时间戳为 100ms

---

## 🚀 使用示例

### 示例 1：多进程数据处理链

```bash
# 1. 初始化 Runtime
$ mqruntime init --name my_runtime

# 2. 启动数据源（3 个进程）
$ ./block_process my_runtime FileSource src1 --file input1.dat &
$ ./block_process my_runtime FileSource src2 --file input2.dat &
$ ./block_process my_runtime FileSource src3 --file input3.dat &

# 3. 启动同步合并器（1 个进程）
$ ./block_process my_runtime SyncMerger merger1 \
    --mode sync \
    --policy exact_match \
    --tolerance 1ms &

# 4. 启动处理器（1 个进程）
$ ./block_process my_runtime Amplifier amp1 --gain 2.0 &

# 5. 启动接收器（1 个进程）
$ ./block_process my_runtime FileSink sink1 --file output.dat &

# 6. 连接
$ mqruntime connect --from src1:out --to merger1:in0
$ mqruntime connect --from src2:out --to merger1:in1
$ mqruntime connect --from src3:out --to merger1:in2
$ mqruntime connect --from merger1:out --to amp1:in
$ mqruntime connect --from amp1:out --to sink1:in

# 7. 监控
$ mqruntime monitor --runtime my_runtime
```

### 示例 2：Python API

```python
import multiqueue_shm as mq

# 连接到共享内存
registry = mq.GlobalRegistry.open("my_runtime")

# 创建 Block
block = mq.SyncMerger(input_count=3)
block.set_sync_policy(mq.SyncPolicy.EXACT_MATCH)
block.set_sync_tolerance(1000000)  # 1ms

# 注册 Block
process_slot = registry.register_process(os.getpid(), mq.ProcessRole.WORKER, "merger")
block_id = registry.register_block(process_slot, "merger1", mq.BlockType.PROCESSING)

block.set_id(block_id)
block.set_registry(registry)

# 初始化
block.initialize()

# 启动心跳
heartbeat_thread = threading.Thread(target=heartbeat_func, args=(registry, process_slot))
heartbeat_thread.start()

# 运行
try:
    while True:
        result = block.work()
        if result == mq.WorkResult.DONE:
            break
finally:
    block.cleanup()
    registry.unregister_block(block_id)
    registry.unregister_process(process_slot)
```

---

## 📈 性能特性

| 特性 | 指标 | 说明 |
|------|------|------|
| **数据延迟** | < 1ms | Block 间传递延迟 |
| **同步精度** | 可配置 | 通常 1ms 容差 |
| **吞吐量** | > 1GB/s | 单个 Block 处理能力 |
| **进程数** | 64+ | 支持的最大进程数 |
| **端口数** | 16 | 每个 Block 的最大端口数 |
| **Buffer 池** | 4096 | 最大 Buffer 数量 |
| **队列深度** | 256 | 每个端口队列深度 |

---

## 🛠️ 实施计划

### Phase 1: 共享内存基础（5-7 天）
- [ ] GlobalRegistry 设计和实现
- [ ] ProcessRegistry + 心跳机制
- [ ] BlockRegistry + 端口管理
- [ ] ConnectionRegistry
- [ ] BufferPoolRegistry
- [ ] BufferMetadataTable（含时间戳）
- [ ] PortQueue（进程间同步）

### Phase 2: Block 框架（3-5 天）
- [ ] Block 基类
- [ ] 多端口支持
- [ ] BufferPtr（跨进程引用计数）
- [ ] 内置 Block（FileSource、FileSink、Amplifier）

### Phase 3: 时间戳同步（3-5 天）
- [ ] Timestamp 结构
- [ ] SyncBlock 基类
- [ ] 同步策略实现（EXACT_MATCH、NEAREST_NEIGHBOR）
- [ ] SyncMerger Block
- [ ] Resampler Block

### Phase 4: 工具和测试（3-5 天）
- [ ] RuntimeManager 工具
- [ ] 命令行工具（mqruntime）
- [ ] Python 绑定
- [ ] 单元测试
- [ ] 集成测试
- [ ] 性能测试

**总计：14-22 天**

---

## ✅ 设计完整性检查

### 多进程/多线程 ✅
- [x] 进程独立运行
- [x] 进程间共享内存通信
- [x] 进程崩溃隔离
- [x] 心跳和僵尸清理

### 多入多出 ✅
- [x] 任意数量输入/输出端口
- [x] 多种连接模式（合并、分离、路由）
- [x] 灵活的数据流图

### 时间戳同步 ✅
- [x] 多种时间戳类型
- [x] 同步/异步模式
- [x] 多种对齐策略
- [x] 流速不匹配处理

### 零拷贝 ✅
- [x] 数据在共享内存中
- [x] 只传递 Buffer ID
- [x] 引用计数管理

### 跨进程引用计数 ✅
- [x] BufferMetadata 在共享内存
- [x] 原子操作
- [x] 自动清理

---

## 🎉 总结

这是一个**完整的、生产级别的流处理框架设计**，具备：

✅ **多进程架构** - 隔离性、可扩展性  
✅ **零拷贝** - 高性能  
✅ **时间同步** - 多流对齐  
✅ **灵活性** - 多入多出、动态连接  
✅ **健壮性** - 心跳、容错、自动清理  

**准备开始实施！** 🚀

