# MultiQueue-SHM 设计文档索引

## 📚 文档结构

### 1. 架构概览
- 📄 [共享内存架构.md](./共享内存架构.md) - 原始架构设计（Mermaid 流程图）
- 📄 [RUNTIME_ARCHITECTURE_REDESIGN.md](../RUNTIME_ARCHITECTURE_REDESIGN.md) - Runtime 框架架构重新设计

### 2. 详细设计文档

#### 最新架构：多进程/多线程 + 多流同步
📄 [MULTIPROCESS_DESIGN_V2.md](./MULTIPROCESS_DESIGN_V2.md) ⭐ **推荐阅读**
- 多进程架构
- 跨进程引用计数
- 多入多出支持
- 全局注册表设计

📄 [MULTISTREAM_SYNC_DESIGN.md](./MULTISTREAM_SYNC_DESIGN.md) ⭐ **推荐阅读**
- 时间戳设计
- 同步/异步模式
- 多流对齐算法
- 采样率转换

📄 [MULTIPROCESS_BUFFER_MANAGEMENT.md](./MULTIPROCESS_BUFFER_MANAGEMENT.md) 🔴 **核心重要**
- **多进程优先原则**（严格要求）
- 共享内存布局详解
- BufferMetadata 结构和引用计数
- 跨进程 Buffer 分配和传递
- 单进程多线程作为特例

📄 [ARCHITECTURE_SUMMARY.md](./ARCHITECTURE_SUMMARY.md) ⭐ **完整总结**
- 整体架构概览
- 核心设计原则
- 组件交互示例
- 性能优化策略

#### Part 1: 核心组件设计（单进程版本 - 参考）
📄 [DETAILED_DESIGN.md](./DETAILED_DESIGN.md)

**包含章节：**
1. 架构概览
2. Runtime 核心设计
3. 共享内存管理（ShmManager、BufferPool、Buffer）
4. 调度器设计（Scheduler、FlowGraph、TaskQueue）
5. 消息总线设计（MsgBus、发布-订阅、请求-响应）
6. Block 框架设计（Block 基类、内置 Block 示例）

#### Part 2: 实现细节
📄 [DETAILED_DESIGN_PART2.md](./DETAILED_DESIGN_PART2.md)

**包含章节：**
7. 线程模型（工作线程、同步机制、死锁预防）
8. 内存布局（进程内存 vs 共享内存、跨进程共享）
9. API 设计（C++ API、Python API、pybind11 绑定）
10. 性能优化（零拷贝、引用计数、内存池、工作窃取）
11. 错误处理（异常层次、错误码、日志系统）
12. 跨平台支持（Windows、Linux、macOS）
13. 总结和未来扩展

---

## 🔑 关键设计决策

### 1. 中心化 Runtime 管理
- **决策**：采用 Runtime 单例管理所有资源
- **理由**：统一管理、避免分散的队列、易于控制和监控
- **参考**：GNU Radio、GStreamer 的架构模式

### 2. 共享内存 + 零拷贝
- **决策**：数据存储在共享内存，通过指针传递
- **理由**：避免数据拷贝开销，提升性能
- **实现**：BufferPool + BufferPtr（智能指针）

### 3. 引用计数自动管理
- **决策**：使用 std::shared_ptr + 自定义删除器
- **理由**：支持多消费者、自动回收、简化内存管理
- **实现**：BufferMetadata 中维护引用计数

### 4. Block 独立封装
- **决策**：Block 作为独立的处理单元
- **理由**：模块化、可复用、易于测试和扩展
- **实现**：Block 基类 + work() 方法

### 5. 动态流图连接
- **决策**：运行时可以修改 Block 连接
- **理由**：灵活性高、支持动态调整
- **实现**：FlowGraph + Routing Table

### 6. 工作窃取调度
- **决策**：采用工作窃取算法
- **理由**：负载均衡、减少线程空闲时间
- **实现**：每个线程独立队列 + 窃取机制

---

## 📊 架构对比

| 组件 | 旧设计（点对点队列） | 新设计（Runtime 框架） |
|------|---------------------|----------------------|
| **管理方式** | 分散（每个队列独立） | 中心化（Runtime 统一） |
| **内存管理** | 队列内部 | ShmManager + 池化 |
| **调度** | 无（Block 自己轮询） | Scheduler 统一调度 |
| **连接** | 静态（队列名） | 动态（Runtime.connect） |
| **控制** | 无 | MsgBus 消息总线 |
| **多消费者** | 独立读指针 | 引用计数 |
| **性能** | 自旋等待 | 工作窃取 + 条件变量 |

---

## 🎯 核心类概览

### Runtime 层
```cpp
Runtime          // 核心管理器（单例）
├── Scheduler    // 调度器
│   ├── ThreadPool
│   ├── FlowGraph
│   └── TaskQueue
├── ShmManager   // 共享内存管理器
│   ├── BufferPool[]
│   ├── BufferMetadata
│   └── 引用计数
└── MsgBus       // 消息总线
    ├── 发布-订阅
    └── 请求-响应
```

### Block 层
```cpp
Block (基类)
├── Source Block    // 数据源
│   └── FileSourceBlock
├── Processing Block // 处理模块
│   └── AmplifierBlock
└── Sink Block      // 数据接收器
    └── FileSinkBlock
```

### 数据流
```cpp
BufferPool → Buffer → BufferPtr → Block::work()
     ↑                                  ↓
     └─────────── 引用计数归零 ←────────┘
```

---

## 🚀 使用示例

### C++ 示例
```cpp
Runtime& rt = Runtime::instance();
rt.initialize(config);

auto src = std::make_unique<FileSourceBlock>("in.dat");
auto amp = std::make_unique<AmplifierBlock>(2.0f);
auto sink = std::make_unique<FileSinkBlock>("out.dat");

BlockId src_id = rt.register_block(std::move(src));
BlockId amp_id = rt.register_block(std::move(amp));
BlockId sink_id = rt.register_block(std::move(sink));

rt.connect(src_id, "out", amp_id, "in");
rt.connect(amp_id, "out", sink_id, "in");

rt.start();
// ... 运行 ...
rt.stop();
rt.shutdown();
```

### Python 示例
```python
import multiqueue_shm as mq

rt = mq.Runtime.instance()
rt.initialize(mq.RuntimeConfig())

src = mq.FileSource("in.dat")
amp = mq.Amplifier(gain=2.0)
sink = mq.FileSink("out.dat")

src_id = rt.register_block(src)
amp_id = rt.register_block(amp)
sink_id = rt.register_block(sink)

rt.connect(src_id, "out", amp_id, "in")
rt.connect(amp_id, "out", sink_id, "in")

rt.start()
# ... 运行 ...
rt.stop()
rt.shutdown()
```

---

## 📈 性能目标

| 指标 | 目标 | 说明 |
|------|------|------|
| **延迟** | < 1ms | Block 间数据传递延迟 |
| **吞吐量** | > 1GB/s | 单个 Block 处理吞吐量 |
| **CPU 使用率** | < 80% | 满负载时的 CPU 使用率 |
| **内存开销** | < 100MB | Runtime 基础内存开销 |
| **扩展性** | 16+ 线程 | 支持的最大工作线程数 |

---

## 🛠️ 实施计划

### Phase 1: Runtime 核心（3-4天）⭐
- [ ] Runtime 单例管理器
- [ ] ShmManager + BufferPool
- [ ] Buffer + 引用计数
- [ ] MsgBus 消息总线

### Phase 2: Scheduler 调度器（2-3天）
- [ ] 线程池 + TaskQueue
- [ ] FlowGraph 连接图
- [ ] 路由表和 BufferQueue
- [ ] 工作窃取算法

### Phase 3: Block 框架（2-3天）
- [ ] Block 基类
- [ ] 内置 Block（FileSource、Amplifier、FileSink）
- [ ] Python Block 支持

### Phase 4: 测试和文档（2-3天）
- [ ] 单元测试
- [ ] 集成测试
- [ ] 性能测试
- [ ] API 文档
- [ ] 示例程序

**总计：9-13 天**

---

## ❓ 常见问题

### Q1: 为什么不支持跨进程 Buffer 共享？
**A**: 当前设计优先考虑单进程多线程的高性能场景。跨进程共享需要额外的同步开销（进程间互斥锁、引用计数同步等），会影响性能。未来版本会考虑添加此功能。

### Q2: 引用计数会不会成为性能瓶颈？
**A**: 不会。引用计数操作使用 `std::atomic`，是无锁的。且只在 Buffer 创建和销毁时操作，数据传递过程中只传递指针（零拷贝）。

### Q3: 如何处理 Block 的异常？
**A**: Block::work() 应该捕获所有异常并返回 `WorkResult::ERROR`。Scheduler 会记录错误并停止调度该 Block。关键异常会通过 MsgBus 广播。

### Q4: 支持哪些平台？
**A**: Windows 10+、Linux (Kernel 3.10+)、macOS 10.15+。使用 Boost.Interprocess 提供跨平台的共享内存支持。

### Q5: Python Block 的性能如何？
**A**: Python Block 的 work() 方法会有 GIL 开销，性能略低于 C++ Block。对于性能敏感的场景，建议使用 C++ 实现 Block。

---

## 📧 反馈和建议

如果您在审阅过程中有任何疑问、建议或发现问题，请：

1. 标注具体的章节和行号
2. 说明您的疑问或建议
3. 提供改进方案（如适用）

**准备好开始实施了吗？** 🚀

