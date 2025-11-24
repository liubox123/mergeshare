# MultiQueue-SHM 多进程/多线程架构设计 v2.0

> **核心需求**：
> - ✅ 支持多进程 + 多线程
> - ✅ 支持多入多出
> - ✅ 共享内存零拷贝
> - ✅ 引用计数跨进程管理

---

## 目录

1. [架构概览](#1-架构概览)
2. [进程模型](#2-进程模型)
3. [共享内存设计](#3-共享内存设计)
4. [引用计数机制](#4-引用计数机制)
5. [Block 多入多出](#5-block-多入多出)
6. [进程间同步](#6-进程间同步)
7. [流图管理](#7-流图管理)
8. [API 设计](#8-api-设计)

---

## 1. 架构概览

### 1.1 多进程架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     Shared Memory Region                         │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Global Registry (全局注册表)                              │ │
│  │  - Process Registry                                        │ │
│  │  - Block Registry                                          │ │
│  │  - Connection Registry                                     │ │
│  │  - Buffer Pool Registry                                    │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Buffer Pools (多个 BufferPool)                            │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                │ │
│  │  │ Pool 4KB │  │Pool 64KB │  │Pool 1MB  │                │ │
│  │  └──────────┘  └──────────┘  └──────────┘                │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Port Queues (端口队列)                                    │ │
│  │  - 每个输入端口一个队列                                     │ │
│  │  - 使用 interprocess_condition 通知                        │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                          ↑           ↑           ↑
                          │           │           │
              ┌───────────┴─┐    ┌───┴────────┐  │
              │  Process 1  │    │ Process 2  │  │
              │             │    │            │  │
              │  ┌────────┐ │    │ ┌────────┐│  │
              │  │Block A │ │    │ │Block B ││  │
              │  │(Source)│ │    │ │(Proc)  ││  │
              │  └────────┘ │    │ └────────┘│  │
              │             │    │            │  │
              │  ┌────────┐ │    │ ┌────────┐│  │
              │  │Thread  │ │    │ │Thread  ││  │
              │  │Pool    │ │    │ │Pool    ││  │
              │  └────────┘ │    │ └────────┘│  │
              └─────────────┘    └────────────┘  │
                                                  │
                                    ┌─────────────┴──┐
                                    │   Process 3    │
                                    │                │
                                    │  ┌────────┐    │
                                    │  │Block C │    │
                                    │  │(Sink)  │    │
                                    │  └────────┘    │
                                    └────────────────┘
```

### 1.2 关键设计点

#### ✅ 共享内存中心化
- **全局注册表**：所有进程共享，记录进程、Block、连接信息
- **Buffer 池**：跨进程共享的内存池
- **端口队列**：使用共享内存队列 + 进程间条件变量

#### ✅ 进程独立运行
- 每个进程运行一个或多个 Block
- 进程内可以有多个线程（工作线程池）
- 进程崩溃不影响其他进程

#### ✅ 引用计数跨进程
- BufferMetadata 存储在共享内存
- 引用计数使用 `std::atomic` 在共享内存中
- 进程退出时自动清理引用

---

## 2. 进程模型

### 2.1 进程类型

```cpp
/**
 * @brief 进程类型
 */
enum class ProcessRole {
    STANDALONE,     // 独立进程（运行单个 Block）
    RUNTIME_SERVER, // Runtime 服务器（管理全局状态）
    WORKER          // 工作进程（运行多个 Block）
};

/**
 * @brief 进程信息（存储在共享内存）
 */
struct alignas(64) ProcessInfo {
    uint64_t pid;                           // 进程 ID
    ProcessRole role;                       // 进程角色
    
    char process_name[64];                  // 进程名称
    
    std::atomic<bool> alive;                // 是否存活
    std::atomic<uint64_t> heartbeat_time;   // 心跳时间戳
    
    uint64_t start_time;                    // 启动时间
    
    // 该进程运行的 Block ID 列表
    uint32_t block_count;
    uint64_t block_ids[16];                 // 最多 16 个 Block
    
    char padding[64];
};

/**
 * @brief 进程注册表（存储在共享内存）
 */
struct ProcessRegistry {
    static constexpr size_t MAX_PROCESSES = 64;
    
    interprocess_mutex mutex;
    
    uint32_t process_count;
    ProcessInfo processes[MAX_PROCESSES];
    
    /**
     * @brief 注册进程
     */
    int register_process(uint64_t pid, ProcessRole role, const char* name);
    
    /**
     * @brief 注销进程
     */
    void unregister_process(int process_slot);
    
    /**
     * @brief 更新心跳
     */
    void update_heartbeat(int process_slot);
    
    /**
     * @brief 检查僵尸进程并清理
     */
    void cleanup_dead_processes();
};
```

### 2.2 进程启动流程

```cpp
int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    std::string shm_name = argv[1];  // 共享内存名
    std::string block_type = argv[2]; // Block 类型
    // ...
    
    // 2. 连接到共享内存
    GlobalRegistry* registry = open_global_registry(shm_name);
    
    // 3. 注册进程
    int process_slot = registry->process_registry.register_process(
        getpid(),
        ProcessRole::WORKER,
        "my_process"
    );
    
    // 4. 创建 Block
    std::unique_ptr<Block> block = create_block(block_type, /* params */);
    
    // 5. 注册 Block
    uint64_t block_id = registry->block_registry.register_block(
        process_slot,
        block->name(),
        block->type()
    );
    
    // 6. 初始化 Block
    block->set_id(block_id);
    block->set_registry(registry);
    block->initialize();
    
    // 7. 启动心跳线程
    std::thread heartbeat_thread([&]() {
        while (running) {
            registry->process_registry.update_heartbeat(process_slot);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    
    // 8. 运行 Block
    block->run();  // 阻塞运行
    
    // 9. 清理
    heartbeat_thread.join();
    registry->block_registry.unregister_block(block_id);
    registry->process_registry.unregister_process(process_slot);
    
    return 0;
}
```

### 2.3 进程间通信

```cpp
// 方式 1: 共享内存队列（数据流）
// Block A (Process 1) → PortQueue → Block B (Process 2)

// 方式 2: 消息总线（控制流）
// Process 1 → MsgBus → Process 2

// 方式 3: 全局注册表（状态查询）
// Process N 读取 Registry → 获取其他 Block 的状态
```

---

## 3. 共享内存设计

### 3.1 全局注册表

```cpp
/**
 * @brief 全局注册表（共享内存的核心）
 */
struct GlobalRegistry {
    // ===== 魔数和版本 =====
    uint64_t magic_number;  // 0x4D5147524547 "MQGREG"
    uint32_t version;
    
    // ===== 注册表 =====
    ProcessRegistry process_registry;
    BlockRegistry block_registry;
    ConnectionRegistry connection_registry;
    BufferPoolRegistry buffer_pool_registry;
    
    // ===== 消息总线 =====
    MessageBus message_bus;
    
    // ===== 统计信息 =====
    std::atomic<uint64_t> total_buffers_allocated;
    std::atomic<uint64_t> total_buffers_freed;
    std::atomic<uint64_t> total_bytes_transferred;
    
    /**
     * @brief 初始化（由第一个进程调用）
     */
    void initialize();
    
    /**
     * @brief 清理僵尸资源
     */
    void cleanup();
};

/**
 * @brief 打开或创建全局注册表
 */
GlobalRegistry* open_or_create_global_registry(const std::string& shm_name) {
    try {
        // 1. 尝试打开现有的共享内存
        auto shm = boost::interprocess::shared_memory_object(
            boost::interprocess::open_only,
            shm_name.c_str(),
            boost::interprocess::read_write
        );
        
        auto region = boost::interprocess::mapped_region(
            shm,
            boost::interprocess::read_write
        );
        
        GlobalRegistry* registry = static_cast<GlobalRegistry*>(
            region.get_address()
        );
        
        // 验证魔数
        if (registry->magic_number != 0x4D5147524547ULL) {
            throw std::runtime_error("Invalid shared memory");
        }
        
        return registry;
        
    } catch (const boost::interprocess::interprocess_exception&) {
        // 2. 共享内存不存在，创建新的
        
        // 删除可能存在的旧共享内存
        boost::interprocess::shared_memory_object::remove(shm_name.c_str());
        
        // 计算大小
        size_t total_size = sizeof(GlobalRegistry) +
                           16 * 1024 * 1024;  // 额外 16MB 用于 Buffer 池
        
        // 创建
        auto shm = boost::interprocess::shared_memory_object(
            boost::interprocess::create_only,
            shm_name.c_str(),
            boost::interprocess::read_write
        );
        shm.truncate(total_size);
        
        auto region = boost::interprocess::mapped_region(
            shm,
            boost::interprocess::read_write
        );
        
        GlobalRegistry* registry = static_cast<GlobalRegistry*>(
            region.get_address()
        );
        
        // 初始化
        new (registry) GlobalRegistry();
        registry->initialize();
        
        return registry;
    }
}
```

### 3.2 Block 注册表

```cpp
/**
 * @brief Block 信息（存储在共享内存）
 */
struct alignas(64) BlockInfo {
    uint64_t block_id;
    uint32_t process_slot;       // 所属进程
    
    char block_name[64];
    BlockType block_type;
    
    std::atomic<bool> active;
    std::atomic<uint64_t> last_work_time;
    
    // 端口信息
    struct PortInfo {
        char port_name[32];
        bool is_input;
        uint32_t queue_offset;   // 在共享内存中的队列偏移
    };
    
    uint32_t port_count;
    PortInfo ports[16];          // 最多 16 个端口
    
    // 统计信息
    std::atomic<uint64_t> work_count;
    std::atomic<uint64_t> total_work_time_us;
};

/**
 * @brief Block 注册表（存储在共享内存）
 */
struct BlockRegistry {
    static constexpr size_t MAX_BLOCKS = 256;
    
    interprocess_mutex mutex;
    
    uint32_t block_count;
    BlockInfo blocks[MAX_BLOCKS];
    
    /**
     * @brief 注册 Block
     */
    uint64_t register_block(uint32_t process_slot,
                           const char* name,
                           BlockType type);
    
    /**
     * @brief 注销 Block
     */
    void unregister_block(uint64_t block_id);
    
    /**
     * @brief 注册端口
     */
    void register_port(uint64_t block_id,
                      const char* port_name,
                      bool is_input,
                      uint32_t queue_offset);
    
    /**
     * @brief 查找 Block
     */
    BlockInfo* find_block(uint64_t block_id);
    
    /**
     * @brief 查找端口
     */
    BlockInfo::PortInfo* find_port(uint64_t block_id, const char* port_name);
};
```

### 3.3 连接注册表

```cpp
/**
 * @brief 连接信息（存储在共享内存）
 */
struct alignas(64) ConnectionInfo {
    uint64_t connection_id;
    
    // 源端口
    uint64_t src_block_id;
    char src_port_name[32];
    
    // 目标端口
    uint64_t dst_block_id;
    char dst_port_name[32];
    
    std::atomic<bool> active;
    
    // 统计信息
    std::atomic<uint64_t> buffer_count;
    std::atomic<uint64_t> total_bytes;
};

/**
 * @brief 连接注册表（存储在共享内存）
 */
struct ConnectionRegistry {
    static constexpr size_t MAX_CONNECTIONS = 512;
    
    interprocess_mutex mutex;
    
    uint32_t connection_count;
    ConnectionInfo connections[MAX_CONNECTIONS];
    
    /**
     * @brief 添加连接
     */
    uint64_t add_connection(uint64_t src_block, const char* src_port,
                           uint64_t dst_block, const char* dst_port);
    
    /**
     * @brief 移除连接
     */
    void remove_connection(uint64_t connection_id);
    
    /**
     * @brief 获取输出端口的所有下游连接
     */
    std::vector<ConnectionInfo*> get_downstream_connections(
        uint64_t block_id,
        const char* port_name
    );
};
```

### 3.4 Buffer 池注册表

```cpp
/**
 * @brief Buffer 池信息（存储在共享内存）
 */
struct alignas(64) BufferPoolInfo {
    char pool_name[32];
    size_t block_size;
    size_t block_count;
    
    // 共享内存对象名称
    char shm_name[64];
    
    std::atomic<uint32_t> allocated_count;
    std::atomic<uint32_t> free_count;
    
    // 空闲列表头（使用偏移量）
    std::atomic<uint32_t> free_list_head;
};

/**
 * @brief Buffer 池注册表（存储在共享内存）
 */
struct BufferPoolRegistry {
    static constexpr size_t MAX_POOLS = 16;
    
    interprocess_mutex mutex;
    
    uint32_t pool_count;
    BufferPoolInfo pools[MAX_POOLS];
    
    /**
     * @brief 添加池
     */
    void add_pool(const char* name, size_t block_size, size_t block_count);
    
    /**
     * @brief 查找池
     */
    BufferPoolInfo* find_pool(const char* name);
    BufferPoolInfo* find_pool_by_size(size_t size);
};
```

---

## 4. 引用计数机制

### 4.1 Buffer 元数据（在共享内存中）

```cpp
/**
 * @brief Buffer 元数据（存储在共享内存）
 */
struct alignas(64) BufferMetadata {
    uint64_t buffer_id;
    
    // 内存信息
    uint32_t pool_id;               // 所属池 ID
    uint32_t offset_in_pool;        // 在池中的偏移
    size_t size;                    // Buffer 大小
    
    // 引用计数（跨进程原子操作）
    std::atomic<uint32_t> ref_count;
    
    // 时间戳
    uint64_t alloc_timestamp;
    uint64_t data_timestamp;
    
    // 状态
    std::atomic<bool> valid;
    
    // 数据指针（相对偏移量，不是绝对指针）
    uint64_t data_offset;           // 相对于共享内存基地址的偏移
    
    char padding[64 - sizeof(uint64_t) - sizeof(uint32_t) * 2 - sizeof(size_t) -
                 sizeof(std::atomic<uint32_t>) - sizeof(uint64_t) * 3 -
                 sizeof(std::atomic<bool>)];
};

/**
 * @brief Buffer 元数据表（存储在共享内存）
 */
struct BufferMetadataTable {
    static constexpr size_t MAX_BUFFERS = 4096;
    
    interprocess_mutex mutex;
    
    uint32_t buffer_count;
    BufferMetadata buffers[MAX_BUFFERS];
    
    std::atomic<uint64_t> next_buffer_id;
    
    /**
     * @brief 分配 BufferMetadata 槽位
     */
    BufferMetadata* allocate_metadata();
    
    /**
     * @brief 释放 BufferMetadata 槽位
     */
    void free_metadata(uint64_t buffer_id);
    
    /**
     * @brief 查找 BufferMetadata
     */
    BufferMetadata* find_metadata(uint64_t buffer_id);
};
```

### 4.2 引用计数操作

```cpp
/**
 * @brief Buffer 智能指针（进程内）
 */
class BufferPtr {
public:
    BufferPtr() : metadata_(nullptr), data_(nullptr) {}
    
    BufferPtr(BufferMetadata* metadata, void* shm_base)
        : metadata_(metadata)
        , shm_base_(shm_base)
    {
        if (metadata_) {
            // 增加引用计数
            metadata_->ref_count.fetch_add(1, std::memory_order_acq_rel);
            
            // 计算数据指针
            data_ = static_cast<char*>(shm_base_) + metadata_->data_offset;
        }
    }
    
    ~BufferPtr() {
        if (metadata_) {
            // 减少引用计数
            uint32_t old_ref = metadata_->ref_count.fetch_sub(1, std::memory_order_acq_rel);
            
            if (old_ref == 1) {
                // 引用计数归零，回收到池中
                release_to_pool();
            }
        }
    }
    
    // 拷贝构造（增加引用计数）
    BufferPtr(const BufferPtr& other)
        : metadata_(other.metadata_)
        , data_(other.data_)
        , shm_base_(other.shm_base_)
    {
        if (metadata_) {
            metadata_->ref_count.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    
    // 移动构造（不改变引用计数）
    BufferPtr(BufferPtr&& other) noexcept
        : metadata_(other.metadata_)
        , data_(other.data_)
        , shm_base_(other.shm_base_)
    {
        other.metadata_ = nullptr;
        other.data_ = nullptr;
    }
    
    void* data() { return data_; }
    size_t size() const { return metadata_ ? metadata_->size : 0; }
    uint64_t id() const { return metadata_ ? metadata_->buffer_id : 0; }
    
private:
    void release_to_pool();
    
    BufferMetadata* metadata_;
    void* data_;
    void* shm_base_;
};
```

### 4.3 进程退出清理

```cpp
/**
 * @brief 进程退出时清理引用计数
 */
void cleanup_process_buffers(GlobalRegistry* registry, uint32_t process_slot) {
    // 遍历所有 Buffer
    for (size_t i = 0; i < BufferMetadataTable::MAX_BUFFERS; ++i) {
        BufferMetadata& meta = registry->buffer_metadata_table.buffers[i];
        
        if (!meta.valid.load(std::memory_order_acquire)) {
            continue;
        }
        
        // 检查是否是该进程分配的 Buffer
        // （可以通过 buffer_id 的高位存储 process_slot）
        uint32_t owner_process = (meta.buffer_id >> 32) & 0xFF;
        
        if (owner_process == process_slot) {
            // 强制减少引用计数（该进程的引用）
            uint32_t old_ref = meta.ref_count.fetch_sub(1, std::memory_order_acq_rel);
            
            if (old_ref == 1) {
                // 引用计数归零，回收
                release_buffer_to_pool(registry, &meta);
            }
        }
    }
}
```

---

## 5. Block 多入多出

### 5.1 端口队列（在共享内存中）

```cpp
/**
 * @brief 端口队列（存储在共享内存）
 */
struct alignas(64) PortQueue {
    static constexpr size_t MAX_QUEUE_SIZE = 256;
    
    // 同步原语
    interprocess_mutex mutex;
    interprocess_condition not_empty;
    interprocess_condition not_full;
    
    // 队列数据
    uint32_t capacity;
    std::atomic<uint32_t> size;
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    
    // Buffer ID 数组（不存储指针，存储 ID）
    uint64_t buffer_ids[MAX_QUEUE_SIZE];
    
    /**
     * @brief 推送 Buffer ID
     */
    bool push(uint64_t buffer_id, uint32_t timeout_ms = 0) {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 等待队列不满
        auto timeout = boost::posix_time::milliseconds(timeout_ms);
        while (size.load() >= capacity) {
            if (timeout_ms == 0) {
                return false;  // 非阻塞模式
            }
            if (!not_full.timed_wait(lock, timeout)) {
                return false;  // 超时
            }
        }
        
        // 推送
        buffer_ids[tail.load()] = buffer_id;
        tail.store((tail.load() + 1) % MAX_QUEUE_SIZE, std::memory_order_release);
        size.fetch_add(1, std::memory_order_release);
        
        // 通知消费者
        not_empty.notify_one();
        
        return true;
    }
    
    /**
     * @brief 弹出 Buffer ID
     */
    bool pop(uint64_t& buffer_id, uint32_t timeout_ms = 0) {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 等待队列非空
        auto timeout = boost::posix_time::milliseconds(timeout_ms);
        while (size.load() == 0) {
            if (timeout_ms == 0) {
                return false;  // 非阻塞模式
            }
            if (!not_empty.timed_wait(lock, timeout)) {
                return false;  // 超时
            }
        }
        
        // 弹出
        buffer_id = buffer_ids[head.load()];
        head.store((head.load() + 1) % MAX_QUEUE_SIZE, std::memory_order_release);
        size.fetch_sub(1, std::memory_order_release);
        
        // 通知生产者
        not_full.notify_one();
        
        return true;
    }
};
```

### 5.2 Block 多端口支持

```cpp
/**
 * @brief Block 基类（支持多入多出）
 */
class Block {
public:
    Block(const std::string& name, BlockType type)
        : name_(name), type_(type), block_id_(0), registry_(nullptr)
    {}
    
    virtual ~Block() = default;
    
    // ===== 生命周期 =====
    virtual void initialize() {}
    virtual void start() {}
    virtual void stop() {}
    virtual void cleanup() {}
    
    // ===== 工作函数 =====
    /**
     * @brief 执行一次处理
     * 
     * 多入多出场景：
     * 1. 从多个输入端口读取数据
     * 2. 处理数据
     * 3. 发布到多个输出端口
     */
    virtual WorkResult work() = 0;
    
    // ===== 端口管理 =====
    void add_input_port(const std::string& name, size_t queue_size = 256);
    void add_output_port(const std::string& name);
    
    // ===== 数据操作 =====
    
    /**
     * @brief 从输入端口读取（支持多个输入）
     */
    BufferPtr get_input(const std::string& port_name, uint32_t timeout_ms = 0);
    
    /**
     * @brief 发布到输出端口（支持多个输出）
     */
    void produce_output(const std::string& port_name, BufferPtr buffer);
    
    /**
     * @brief 分配输出 Buffer
     */
    BufferPtr allocate_buffer(size_t size);
    
    // ===== 辅助方法 =====
    
    /**
     * @brief 检查输入端口是否有数据
     */
    bool has_input(const std::string& port_name);
    
    /**
     * @brief 获取输入端口队列大小
     */
    size_t input_size(const std::string& port_name);
    
    // Setters
    void set_id(uint64_t id) { block_id_ = id; }
    void set_registry(GlobalRegistry* registry) { registry_ = registry; }
    
    // Getters
    uint64_t id() const { return block_id_; }
    const std::string& name() const { return name_; }
    BlockType type() const { return type_; }
    
protected:
    std::string name_;
    BlockType type_;
    uint64_t block_id_;
    GlobalRegistry* registry_;
    
    // 端口队列映射（进程内）
    std::unordered_map<std::string, PortQueue*> input_queues_;
    std::unordered_map<std::string, std::vector<uint64_t>> output_connections_;
};
```

### 5.3 多入多出示例

```cpp
/**
 * @brief 多入单出：合并器
 */
class MergerBlock : public Block {
public:
    MergerBlock()
        : Block("Merger", BlockType::PROCESSING)
    {
        // 添加多个输入端口
        add_input_port("in1");
        add_input_port("in2");
        add_input_port("in3");
        
        // 添加单个输出端口
        add_output_port("out");
    }
    
    WorkResult work() override {
        // 从任意一个输入端口读取
        std::vector<std::string> ports = {"in1", "in2", "in3"};
        
        for (const auto& port : ports) {
            if (has_input(port)) {
                auto input = get_input(port, 10);  // 10ms 超时
                
                if (input) {
                    // 直接转发到输出
                    produce_output("out", input);
                    return WorkResult::OK;
                }
            }
        }
        
        return WorkResult::NEED_MORE_INPUT;
    }
};

/**
 * @brief 单入多出：分离器
 */
class SplitterBlock : public Block {
public:
    SplitterBlock()
        : Block("Splitter", BlockType::PROCESSING)
    {
        // 添加单个输入端口
        add_input_port("in");
        
        // 添加多个输出端口
        add_output_port("out1");
        add_output_port("out2");
        add_output_port("out3");
    }
    
    WorkResult work() override {
        // 从输入端口读取
        auto input = get_input("in", 100);
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 复制到多个输出端口
        // 注意：引用计数会自动增加
        produce_output("out1", input);
        produce_output("out2", input);
        produce_output("out3", input);
        
        return WorkResult::OK;
    }
};

/**
 * @brief 多入多出：路由器
 */
class RouterBlock : public Block {
public:
    RouterBlock()
        : Block("Router", BlockType::PROCESSING)
    {
        add_input_port("in1");
        add_input_port("in2");
        
        add_output_port("out1");
        add_output_port("out2");
    }
    
    WorkResult work() override {
        // 从 in1 读取，发送到 out1
        if (has_input("in1")) {
            auto input = get_input("in1", 10);
            if (input) {
                produce_output("out1", input);
                return WorkResult::OK;
            }
        }
        
        // 从 in2 读取，发送到 out2
        if (has_input("in2")) {
            auto input = get_input("in2", 10);
            if (input) {
                produce_output("out2", input);
                return WorkResult::OK;
            }
        }
        
        return WorkResult::NEED_MORE_INPUT;
    }
};
```

---

## 6. 进程间同步

### 6.1 心跳机制

```cpp
/**
 * @brief 心跳线程（每个进程）
 */
void heartbeat_thread_func(GlobalRegistry* registry, uint32_t process_slot) {
    while (running.load()) {
        // 更新心跳时间戳
        registry->process_registry.update_heartbeat(process_slot);
        
        // 每秒更新一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/**
 * @brief 僵尸进程检测和清理（由 Runtime Server 执行）
 */
void cleanup_thread_func(GlobalRegistry* registry) {
    while (running.load()) {
        // 检查所有进程的心跳
        auto now = get_timestamp_ns();
        
        for (size_t i = 0; i < ProcessRegistry::MAX_PROCESSES; ++i) {
            ProcessInfo& proc = registry->process_registry.processes[i];
            
            if (!proc.alive.load(std::memory_order_acquire)) {
                continue;
            }
            
            // 检查心跳超时（5 秒）
            uint64_t last_heartbeat = proc.heartbeat_time.load(std::memory_order_acquire);
            if (now - last_heartbeat > 5000000000ULL) {  // 5 秒
                // 心跳超时，认为进程已死
                std::cerr << "Process " << proc.pid << " is dead (heartbeat timeout)" << std::endl;
                
                // 清理该进程的资源
                cleanup_process_resources(registry, i);
                
                // 标记为不活跃
                proc.alive.store(false, std::memory_order_release);
            }
        }
        
        // 每 2 秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
```

### 6.2 进程间消息总线

```cpp
/**
 * @brief 消息总线（存储在共享内存）
 */
struct MessageBus {
    static constexpr size_t MAX_MESSAGES = 1024;
    
    interprocess_mutex mutex;
    interprocess_condition not_empty;
    
    // 环形缓冲区
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    
    struct Message {
        uint64_t sender_pid;
        uint64_t target_pid;  // 0 表示广播
        
        MessageType type;
        char topic[64];
        char payload[256];
        
        uint64_t timestamp;
    };
    
    Message messages[MAX_MESSAGES];
    
    /**
     * @brief 发布消息
     */
    bool publish(const Message& msg);
    
    /**
     * @brief 接收消息（阻塞）
     */
    bool receive(Message& msg, uint32_t timeout_ms = 0);
};
```

---

## 7. 流图管理

### 7.1 管理工具

```cpp
/**
 * @brief Runtime 管理器（独立进程或工具）
 */
class RuntimeManager {
public:
    RuntimeManager(const std::string& shm_name);
    
    // ===== 进程管理 =====
    
    /**
     * @brief 启动一个 Block 进程
     */
    void start_block(const std::string& block_type,
                    const std::string& block_name,
                    const std::map<std::string, std::string>& params);
    
    /**
     * @brief 停止一个进程
     */
    void stop_process(uint64_t pid);
    
    // ===== 连接管理 =====
    
    /**
     * @brief 连接两个 Block
     */
    void connect(uint64_t src_block, const std::string& src_port,
                uint64_t dst_block, const std::string& dst_port);
    
    /**
     * @brief 断开连接
     */
    void disconnect(uint64_t connection_id);
    
    // ===== 监控 =====
    
    /**
     * @brief 列出所有进程
     */
    std::vector<ProcessInfo> list_processes();
    
    /**
     * @brief 列出所有 Block
     */
    std::vector<BlockInfo> list_blocks();
    
    /**
     * @brief 列出所有连接
     */
    std::vector<ConnectionInfo> list_connections();
    
    /**
     * @brief 获取统计信息
     */
    RuntimeStats get_stats();
    
private:
    std::string shm_name_;
    GlobalRegistry* registry_;
};
```

### 7.2 命令行工具

```bash
# 初始化 Runtime
$ mqruntime init --name my_runtime

# 启动 Block 进程
$ mqruntime start --runtime my_runtime --block FileSource --name src1 \
    --param filename=input.dat

$ mqruntime start --runtime my_runtime --block Amplifier --name amp1 \
    --param gain=2.0

$ mqruntime start --runtime my_runtime --block FileSink --name sink1 \
    --param filename=output.dat

# 连接 Block
$ mqruntime connect --runtime my_runtime \
    --from src1:out --to amp1:in

$ mqruntime connect --runtime my_runtime \
    --from amp1:out --to sink1:in

# 监控
$ mqruntime list --runtime my_runtime
Processes:
  PID      Name         Blocks      Status
  12345    src1_proc    src1        Running
  12346    amp1_proc    amp1        Running
  12347    sink1_proc   sink1       Running

Connections:
  From        To          Buffers    Bytes
  src1:out    amp1:in     12345      50.2MB
  amp1:out    sink1:in    12345      50.2MB

# 停止
$ mqruntime stop --runtime my_runtime --block src1
```

---

## 8. API 设计

### 8.1 C++ API

```cpp
// main.cpp - 单个 Block 进程
#include <multiqueue/block.hpp>
#include <multiqueue/registry.hpp>

int main(int argc, char* argv[]) {
    // 解析参数
    std::string shm_name = argv[1];
    std::string block_type = argv[2];
    
    // 连接到共享内存
    GlobalRegistry* registry = open_global_registry(shm_name);
    
    // 注册进程
    int process_slot = registry->process_registry.register_process(
        getpid(),
        ProcessRole::WORKER,
        "my_block_process"
    );
    
    // 创建 Block
    std::unique_ptr<Block> block;
    if (block_type == "FileSource") {
        block = std::make_unique<FileSourceBlock>("input.dat");
    } else if (block_type == "Amplifier") {
        block = std::make_unique<AmplifierBlock>(2.0f);
    }
    // ...
    
    // 注册 Block
    uint64_t block_id = registry->block_registry.register_block(
        process_slot,
        block->name().c_str(),
        block->type()
    );
    
    block->set_id(block_id);
    block->set_registry(registry);
    
    // 初始化
    block->initialize();
    
    // 启动心跳
    std::atomic<bool> running{true};
    std::thread heartbeat([&]() {
        while (running) {
            registry->process_registry.update_heartbeat(process_slot);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    
    // 运行 Block
    while (running) {
        WorkResult result = block->work();
        
        if (result == WorkResult::DONE) {
            break;
        } else if (result == WorkResult::ERROR) {
            std::cerr << "Block work error" << std::endl;
            break;
        }
        // 继续工作
    }
    
    // 清理
    running = false;
    heartbeat.join();
    
    block->cleanup();
    registry->block_registry.unregister_block(block_id);
    registry->process_registry.unregister_process(process_slot);
    
    return 0;
}
```

### 8.2 Python API

```python
# python_block.py
import multiqueue_shm as mq
import sys

def main():
    # 解析参数
    shm_name = sys.argv[1]
    block_type = sys.argv[2]
    
    # 连接到共享内存
    registry = mq.GlobalRegistry.open(shm_name)
    
    # 注册进程
    process_slot = registry.register_process(
        os.getpid(),
        mq.ProcessRole.WORKER,
        "python_block"
    )
    
    # 创建 Block
    if block_type == "FileSource":
        block = mq.FileSource("input.dat")
    elif block_type == "Amplifier":
        block = mq.Amplifier(gain=2.0)
    # ...
    
    # 注册 Block
    block_id = registry.register_block(
        process_slot,
        block.name(),
        block.type()
    )
    
    block.set_id(block_id)
    block.set_registry(registry)
    
    # 初始化
    block.initialize()
    
    # 启动心跳
    heartbeat_thread = threading.Thread(
        target=heartbeat_func,
        args=(registry, process_slot)
    )
    heartbeat_thread.start()
    
    # 运行
    try:
        while True:
            result = block.work()
            if result == mq.WorkResult.DONE:
                break
            elif result == mq.WorkResult.ERROR:
                print("Error")
                break
    finally:
        # 清理
        block.cleanup()
        registry.unregister_block(block_id)
        registry.unregister_process(process_slot)

if __name__ == "__main__":
    main()
```

---

## 总结

### 核心设计特点

✅ **真正的多进程支持**
- 每个进程独立运行
- 进程间通过共享内存通信
- 进程崩溃不影响其他进程

✅ **跨进程引用计数**
- BufferMetadata 在共享内存中
- 原子操作保证线程安全和进程安全
- 自动回收机制

✅ **多入多出**
- Block 支持任意数量的输入/输出端口
- 灵活的数据流图
- 支持合并、分离、路由等模式

✅ **零拷贝**
- 数据存储在共享内存
- 只传递 Buffer ID
- 引用计数管理生命周期

✅ **健壮性**
- 心跳机制检测僵尸进程
- 自动清理死进程的资源
- 进程间互斥和条件变量

---

**请审阅这个多进程/多线程架构设计！** 🚀

需要我继续补充哪些细节？

