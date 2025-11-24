# MultiQueue-SHM 核心库

## 概述

MultiQueue-SHM 核心库提供了基于共享内存的多进程流处理框架，支持零拷贝数据传递和跨进程引用计数。

## 目录结构

```
core/
├── include/multiqueue/        # 公共头文件
│   ├── types.hpp              # 基础类型定义
│   ├── timestamp.hpp          # 时间戳结构
│   ├── buffer_metadata.hpp    # Buffer 元数据
│   ├── buffer_pool.hpp        # Buffer Pool
│   ├── buffer_allocator.hpp   # Buffer 分配器
│   ├── buffer_ptr.hpp         # Buffer 智能指针
│   ├── global_registry.hpp    # 全局注册表
│   ├── port_queue.hpp         # 端口队列
│   └── multiqueue_shm.hpp     # 统一包含头文件
├── src/                        # 源文件（如有需要）
├── CMakeLists.txt             # CMake 构建文件
└── README.md                  # 本文件
```

## 核心组件

### 1. 类型系统 (`types.hpp`)

定义了框架使用的所有基础类型、常量和枚举：
- **ID 类型**：ProcessId, BlockId, PortId, BufferId, etc.
- **枚举类型**：BlockType, PortType, SyncMode, AlignmentPolicy, etc.
- **常量**：MAX_PROCESSES, MAX_BUFFERS, CACHE_LINE_SIZE, etc.

### 2. 时间戳 (`timestamp.hpp`)

高精度纳秒级时间戳支持，用于多流同步：
- `Timestamp`：单点时间戳
- `TimeRange`：时间范围
- 时间戳运算和比较

### 3. Buffer 元数据 (`buffer_metadata.hpp`)

Buffer 元数据存储在共享内存中，支持跨进程引用计数：
- `BufferMetadata`：Buffer 元数据结构
- `BufferMetadataTable`：元数据表，管理所有 Buffer

### 4. Buffer Pool (`buffer_pool.hpp`)

固定大小的内存池，存储在共享内存中：
- 空闲链表管理
- 原子操作分配/释放
- 跨进程访问

### 5. Buffer 分配器 (`buffer_allocator.hpp`)

负责从 Buffer Pool 分配和释放 Buffer：
- `SharedBufferAllocator`：分配器类
- 自动选择合适的池
- 维护进程本地的池映射

### 6. Buffer 智能指针 (`buffer_ptr.hpp`)

进程本地的轻量级包装器，自动管理引用计数：
- `BufferPtr`：智能指针类
- RAII 风格管理
- 支持拷贝和移动语义

### 7. 全局注册表 (`global_registry.hpp`)

存储在共享内存中，管理所有进程、Block、连接等：
- `ProcessRegistry`：进程注册表
- `BlockRegistry`：Block 注册表
- `ConnectionRegistry`：连接注册表
- `BufferPoolRegistry`：Buffer Pool 注册表
- `GlobalRegistry`：全局注册表

### 8. 端口队列 (`port_queue.hpp`)

Block 之间传递 Buffer ID 的队列，存储在共享内存中：
- `PortQueue`：端口队列类
- 阻塞/非阻塞模式
- 超时支持

## 编译

```bash
cd multiqueue-shm
mkdir -p out/build
cd out/build
cmake ../..
make
```

## 使用示例

### 基础使用

```cpp
#include <multiqueue/multiqueue_shm.hpp>
using namespace multiqueue;

// 1. 创建或打开全局注册表
GlobalRegistry* registry = ...;  // 在共享内存中

// 2. 注册进程
int32_t process_slot = registry->process_registry.register_process("MyProcess");
ProcessId process_id = registry->process_registry.processes[process_slot].process_id;

// 3. 创建 Buffer Pool
BufferPool pool;
pool.create("mqshm_pool_small", 0, 4096, 1024);

// 4. 注册 Pool
PoolId pool_id = registry->buffer_pool_registry.register_pool(
    4096, 1024, "mqshm_pool_small"
);

// 5. 创建 Buffer 分配器
SharedBufferAllocator allocator(registry, process_id);
allocator.register_pool(pool_id, "mqshm_pool_small");

// 6. 分配 Buffer
BufferId buffer_id = allocator.allocate(1024);

// 7. 创建 BufferPtr
BufferPtr buffer(buffer_id, &allocator);

// 8. 使用 Buffer
void* data = buffer.data();
memcpy(data, "Hello", 5);

// 9. BufferPtr 自动管理引用计数和释放
```

### 多进程示例

**进程 A（生产者）**：
```cpp
// 创建 Buffer Pool 和 Port Queue
BufferPool pool;
pool.create("mqshm_pool_small", 0, 4096, 1024);

PortQueue queue;
queue.create("mqshm_port_out", 1, 64);

// 分配和发送 Buffer
SharedBufferAllocator allocator(registry, process_id_a);
allocator.register_pool(0, "mqshm_pool_small");

BufferId buffer_id = allocator.allocate(1024);
void* data = allocator.get_buffer_data(buffer_id);
memcpy(data, "Hello from A", 12);

queue.push(buffer_id);
```

**进程 B（消费者）**：
```cpp
// 打开 Buffer Pool 和 Port Queue
BufferPool pool;
pool.open("mqshm_pool_small");

PortQueue queue;
queue.open("mqshm_port_out");

// 接收和使用 Buffer
SharedBufferAllocator allocator(registry, process_id_b);
allocator.register_pool(0, "mqshm_pool_small");

BufferId buffer_id;
if (queue.pop(buffer_id)) {
    BufferPtr buffer(buffer_id, &allocator);
    const char* data = buffer.as<const char>();
    printf("Received: %s\n", data);
}
// BufferPtr 析构时自动减少引用计数
```

## 设计原则

### 多进程优先

本框架**严格按多进程模式设计**，单进程多线程只是特例：

1. **所有状态在共享内存**
   - BufferMetadata、Registry、PortQueue 全部在共享内存

2. **跨进程同步原语**
   - interprocess_mutex、interprocess_condition、std::atomic（在共享内存中）

3. **不依赖进程内存**
   - 不使用进程内的指针、std::mutex、进程本地状态

4. **只传递 Buffer ID**
   - 不传递指针，使用 uint64_t Buffer ID

5. **使用相对偏移**
   - 数据地址使用相对于共享内存基地址的偏移量

### 零拷贝

数据在共享内存中分配，只传递 Buffer ID，避免数据拷贝。

### 引用计数

使用共享内存中的 atomic<uint32_t> 实现跨进程引用计数，自动回收 Buffer。

## 性能特性

- **原子操作开销**：std::atomic 是 lock-free 的，开销很小
- **查找开销**：线性查找 4096 个 BufferMetadata 条目，未来可优化为哈希表
- **进程间锁**：interprocess_mutex 比 std::mutex 慢，但对于跨进程是必须的

## 平台支持

- Linux (Kernel 3.10+)
- macOS 10.15+
- Windows 10+

## 许可证

[项目许可证]

## 参考文档


