# MultiQueue-SHM

> **多进程共享内存流处理框架 v2.0**

一个高性能、多进程/多线程的共享内存流处理框架，支持零拷贝数据传递、跨进程引用计数和时间戳同步。

---

## 🎯 核心特性

✅ **严格多进程支持**
- 所有状态存储在共享内存中
- 跨进程原子引用计数
- 进程崩溃自动清理

✅ **零拷贝数据传递**
- 数据在共享内存中分配
- 只传递 Buffer ID
- 避免进程间数据拷贝

✅ **多输入/多输出**
- Block 支持多个输入和输出端口
- 灵活的数据流图

✅ **时间戳同步**
- 纳秒精度时间戳
- 同步模式（时间戳对齐）
- 异步模式（自由流）

✅ **高性能**
- Lock-free 原子操作
- 缓存行对齐避免 false sharing
- 工作窃取调度（待实现）

---

## 📊 开发状态

| Phase | 状态 | 描述 |
|-------|------|------|
| **Phase 1** | ✅ 完成 | 核心组件（Buffer、Registry、Queue）**- 所有测试通过** |
| **Phase 2** | ✅ 完成 | Block 框架、Scheduler、Runtime **- 所有测试通过** |
| **Phase 3** | ⏳ 待开始 | Python 绑定 |
| **Phase 4** | ⏳ 待开始 | 性能优化和测试 |

**当前版本**: v2.0.0-phase2  
**测试状态**: ✅ **100% 通过**（8/8 测试模块，44 个测试用例）  
**最新测试**: 2025-11-24

---

## 🏗️ 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                    Shared Memory Region                          │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Global Registry (全局注册表)                              │ │
│  │  ├─ Process Registry     (进程注册表)                      │ │
│  │  ├─ Block Registry       (Block 注册表)                    │ │
│  │  ├─ Connection Registry  (连接注册表)                      │ │
│  │  ├─ BufferPool Registry  (Buffer 池注册表)                │ │
│  │  └─ BufferMetadata Table (元数据表 - 包含时间戳)          │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Buffer Pools (多个内存池)                                 │ │
│  │  ├─ Pool 4KB  (1024 blocks)                               │ │
│  │  ├─ Pool 64KB (512 blocks)                                │ │
│  │  └─ Pool 1MB  (128 blocks)                                │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Port Queues (端口队列)                                     │ │
│  │  - 每个输入端口一个队列                                     │ │
│  │  - interprocess_mutex + interprocess_condition             │ │
│  │  - 存储 Buffer ID（不是指针）                              │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

Process A           Process B           Process C
   │                   │                   │
   ├─ Block 1 ─────────┼─ Block 2 ─────────┼─ Block 3
   │  (Source)         │  (Processing)     │  (Sink)
   │                   │                   │
   └───► BufferPtr ────┴───► BufferPtr ────┴───► 零拷贝！
```

---

## 🚀 快速开始

### 依赖

- **编译器**: GCC 7+ / Clang 6+ / MSVC 2019+
- **C++ 标准**: C++17
- **CMake**: 3.15+
- **Boost**: 1.65+ (Boost.Interprocess)
- **Google Test**: 1.10+ (仅测试)

### 编译

```bash
# 克隆仓库
cd multiqueue-shm

# 创建构建目录
mkdir -p out/build
cd out/build

# 配置和编译
cmake ../..
make -j$(nproc)

# 运行测试
ctest --verbose
```

### 使用示例

#### 完整的流处理示例（使用 Runtime）

```cpp
#include <multiqueue/multiqueue_shm.hpp>
using namespace multiqueue;
using namespace multiqueue::blocks;

// 创建 Runtime
Runtime runtime;
runtime.initialize(true);  // 第一个进程创建全局注册表

// 创建 Block
auto source = std::make_unique<NullSource>(runtime.allocator(), 1024, 100);
auto amp = std::make_unique<Amplifier>(runtime.allocator(), 2.0f);
auto sink = std::make_unique<NullSink>(runtime.allocator());

// 注册 Block
BlockId src_id = runtime.register_block(std::move(source));
BlockId amp_id = runtime.register_block(std::move(amp));
BlockId sink_id = runtime.register_block(std::move(sink));

// 连接 Block
runtime.connect(src_id, "out", amp_id, "in");
runtime.connect(amp_id, "out", sink_id, "in");

// 启动
runtime.start();

// 运行一段时间
std::this_thread::sleep_for(std::chrono::seconds(5));

// 停止并清理
runtime.stop();
runtime.shutdown();
```

#### 多进程生产者-消费者（底层 API）

**进程 A（生产者）**：
```cpp
#include <multiqueue/multiqueue_shm.hpp>
using namespace multiqueue;

// 创建全局注册表和 Buffer Pool
GlobalRegistry* registry = ...;
BufferPool pool;
pool.create("my_pool", 0, 4096, 1024);

// 创建 Port Queue
PortQueue queue;
queue.create("my_queue", 1, 64);

// 创建分配器
SharedBufferAllocator allocator(registry, process_id);
allocator.register_pool(0, "my_pool");

// 分配和发送 Buffer
BufferId buffer_id = allocator.allocate(1024);
void* data = allocator.get_buffer_data(buffer_id);
memcpy(data, "Hello, World!", 13);

queue.push(buffer_id);
allocator.remove_ref(buffer_id);  // 减少本地引用
```

**进程 B（消费者）**：
```cpp
#include <multiqueue/multiqueue_shm.hpp>
using namespace multiqueue;

// 打开全局注册表和 Buffer Pool
GlobalRegistry* registry = ...;
BufferPool pool;
pool.open("my_pool");

// 打开 Port Queue
PortQueue queue;
queue.open("my_queue");

// 创建分配器
SharedBufferAllocator allocator(registry, process_id);
allocator.register_pool(0, "my_pool");

// 接收和使用 Buffer
BufferId buffer_id;
if (queue.pop(buffer_id)) {
    BufferPtr buffer(buffer_id, &allocator);
    const char* data = buffer.as<const char>();
    printf("Received: %s\n", data);
}
// BufferPtr 析构时自动减少引用计数
```

---

## 📚 文档

- [核心库 README](core/README.md)
- [设计文档](design/)
  - [架构总结](design/ARCHITECTURE_SUMMARY.md)
  - [多进程 Buffer 管理](design/MULTIPROCESS_BUFFER_MANAGEMENT.md)
  - [多进程架构设计](design/MULTIPROCESS_DESIGN_V2.md)
  - [多流同步设计](design/MULTISTREAM_SYNC_DESIGN.md)
- [变更记录](commit/)
  - [Phase 2 完成报告](commit/2025-11-24_phase2_complete.md) ⭐ 最新
  - [Phase 1 完成报告](commit/2025-11-24_phase1_complete.md)
  - [多进程优先设计更新](commit/2025-11-24_multiprocess_priority_design.md)

---

## 🧪 测试

### 运行所有测试

```bash
cd out/build
ctest --verbose
```

### 单独运行测试

```bash
# 基础类型测试
./tests/cpp/test_types

# 时间戳测试
./tests/cpp/test_timestamp

# Buffer 元数据测试
./tests/cpp/test_buffer_metadata

# Buffer Pool 测试
./tests/cpp/test_buffer_pool

# Buffer 分配器测试
./tests/cpp/test_buffer_allocator

# 端口队列测试
./tests/cpp/test_port_queue

# Block 框架测试
./tests/cpp/test_block

# 多进程集成测试
./tests/cpp/test_multiprocess
```

---

## 🔧 性能特性

| 特性 | 实现方式 | 性能影响 |
|------|---------|---------|
| **引用计数** | `std::atomic<uint32_t>` (lock-free) | 极小 |
| **Buffer 分配** | 空闲链表 O(1) | 极小 |
| **数据传递** | 零拷贝（只传递 ID） | 无拷贝开销 |
| **进程间锁** | `interprocess_mutex` | 比 std::mutex 慢，但必须 |
| **查找 Buffer** | 线性搜索（4096 条目） | 可优化为哈希表 |
| **缓存行对齐** | 64 字节对齐 | 避免 false sharing |

---

## 🌍 平台支持

- ✅ **Linux** (Kernel 3.10+)
- ✅ **macOS** (10.15+)
- ✅ **Windows** (10+)

---

## 📊 项目统计

| 指标 | 数值 |
|------|------|
| **总代码行数** | ~9900+ |
| **头文件** | 19 |
| **测试文件** | 8 |
| **设计文档** | 8+ |
| **Phase 1 核心组件** | 9 个 |
| **Phase 2 Runtime 组件** | 10 个 |

---

## 🤝 贡献

欢迎贡献！请查看 [CONTRIBUTING.md](CONTRIBUTING.md)（待创建）。

---

## 📜 许可证

[项目许可证]（待定）

---

## 📧 联系

如有问题或建议，请创建 Issue。

---

**MultiQueue-SHM - 高性能多进程共享内存流处理框架** 🚀
