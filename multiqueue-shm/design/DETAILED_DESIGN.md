# MultiQueue-SHM 详细设计文档 v1.0

> 基于中心化 Runtime 的流处理框架
> 
> 设计日期：2025-11-24
> 
> 状态：设计审阅中

---

## 目录

1. [架构概览](#1-架构概览)
2. [Runtime 核心](#2-runtime-核心)
3. [共享内存管理](#3-共享内存管理)
4. [调度器设计](#4-调度器设计)
5. [消息总线](#5-消息总线)
6. [Block 框架](#6-block-框架)
7. [线程模型](#7-线程模型)
8. [内存布局](#8-内存布局)
9. [API 设计](#9-api-设计)
10. [性能优化](#10-性能优化)
11. [错误处理](#11-错误处理)
12. [跨平台支持](#12-跨平台支持)

---

## 1. 架构概览

### 1.1 设计目标

1. **中心化管理**：统一的资源管理和调度
2. **零拷贝**：数据在共享内存中传递，避免拷贝
3. **多消费者**：支持一对多的广播模式
4. **动态连接**：运行时动态创建/修改数据流图
5. **高性能**：低延迟、高吞吐量
6. **易用性**：简洁的 API，支持 Python 装饰器

### 1.2 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application Layer                        │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                  │
│  │ Block A  │───▶│ Block B  │───▶│ Block C  │                  │
│  │ (Source) │    │(Process) │    │  (Sink)  │                  │
│  └──────────┘    └──────────┘    └──────────┘                  │
└────────┬──────────────┬──────────────┬──────────────────────────┘
         │              │              │
         ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Runtime Core Layer                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │  Scheduler   │  │  ShmManager  │  │   MsgBus     │          │
│  │              │  │              │  │              │          │
│  │ - ThreadPool │  │ - BufferPool │  │ - Pub/Sub    │          │
│  │ - FlowGraph  │  │ - RefCount   │  │ - Req/Resp   │          │
│  │ - Routing    │  │ - Allocator  │  │ - Broadcast  │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
└────────┬──────────────┬──────────────┬──────────────────────────┘
         │              │              │
         ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Shared Memory Layer                             │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Shared Memory Region                                   │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │    │
│  │  │ BufferPool  │  │ BufferPool  │  │ BufferPool  │    │    │
│  │  │  (4KB)      │  │  (64KB)     │  │  (1MB)      │    │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘    │    │
│  │                                                         │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │    │
│  │  │   Buffer    │  │   Buffer    │  │   Buffer    │    │    │
│  │  │  Metadata   │  │  Metadata   │  │  Metadata   │    │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘    │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 核心概念

#### Block（处理模块）
独立的处理单元，有输入端口和输出端口，执行特定的数据处理任务。

#### Port（端口）
Block 之间的连接点：
- **Input Port**：接收数据
- **Output Port**：发送数据

#### Buffer（数据缓冲区）
共享内存中的数据块，通过引用计数管理生命周期。

#### Connection（连接）
两个 Port 之间的数据通路。

#### FlowGraph（流图）
所有 Block 和 Connection 组成的有向图。

---

## 2. Runtime 核心

### 2.1 Runtime 类设计

Runtime 是整个系统的核心管理器，采用单例模式。

```cpp
namespace multiqueue {

/**
 * @brief Runtime 配置
 */
struct RuntimeConfig {
    // 调度器配置
    size_t scheduler_thread_count = 4;
    SchedulePolicy schedule_policy = SchedulePolicy::WORK_STEALING;
    
    // 共享内存配置
    std::string shm_name_prefix = "mqshm_";
    bool shm_auto_cleanup = true;
    
    // 消息总线配置
    size_t msgbus_queue_size = 1024;
    bool msgbus_async = true;
    
    // 性能配置
    bool enable_profiling = false;
    bool enable_tracing = false;
    
    // 日志配置
    LogLevel log_level = LogLevel::INFO;
    std::string log_file = "runtime.log";
};

/**
 * @brief Runtime 核心管理器（单例）
 */
class Runtime {
public:
    // 获取单例
    static Runtime& instance();
    
    // 删除拷贝和移动
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    
    // ===== 生命周期管理 =====
    
    /**
     * @brief 初始化 Runtime
     * @param config 配置参数
     */
    void initialize(const RuntimeConfig& config);
    
    /**
     * @brief 启动 Runtime（启动调度器）
     */
    void start();
    
    /**
     * @brief 停止 Runtime（停止所有 Block）
     */
    void stop();
    
    /**
     * @brief 关闭 Runtime（清理所有资源）
     */
    void shutdown();
    
    /**
     * @brief 检查 Runtime 是否正在运行
     */
    bool is_running() const { return running_.load(); }
    
    // ===== Block 管理 =====
    
    /**
     * @brief 注册一个 Block
     * @param block Block 实例（unique_ptr）
     * @return Block ID
     */
    BlockId register_block(std::unique_ptr<Block> block);
    
    /**
     * @brief 注销一个 Block
     * @param id Block ID
     */
    void unregister_block(BlockId id);
    
    /**
     * @brief 获取 Block
     * @param id Block ID
     * @return Block 指针（可能为空）
     */
    Block* get_block(BlockId id);
    
    /**
     * @brief 列出所有 Block
     * @return Block ID 列表
     */
    std::vector<BlockId> list_blocks() const;
    
    // ===== 连接管理 =====
    
    /**
     * @brief 连接两个 Block
     * @param src_block 源 Block ID
     * @param src_port 源端口名
     * @param dst_block 目标 Block ID
     * @param dst_port 目标端口名
     */
    void connect(BlockId src_block, const std::string& src_port,
                 BlockId dst_block, const std::string& dst_port);
    
    /**
     * @brief 断开连接
     */
    void disconnect(BlockId src_block, const std::string& src_port,
                    BlockId dst_block, const std::string& dst_port);
    
    /**
     * @brief 断开 Block 的所有连接
     */
    void disconnect_all(BlockId block_id);
    
    /**
     * @brief 获取流图（用于可视化）
     */
    const FlowGraph& flow_graph() const { return flow_graph_; }
    
    // ===== 子系统访问 =====
    
    Scheduler& scheduler() { return *scheduler_; }
    ShmManager& shm_manager() { return *shm_manager_; }
    MsgBus& msg_bus() { return *msg_bus_; }
    
    const Scheduler& scheduler() const { return *scheduler_; }
    const ShmManager& shm_manager() const { return *shm_manager_; }
    const MsgBus& msg_bus() const { return *msg_bus_; }
    
    // ===== 统计信息 =====
    
    struct RuntimeStats {
        size_t total_blocks;
        size_t total_connections;
        size_t total_buffers_allocated;
        size_t total_buffers_in_use;
        uint64_t total_bytes_processed;
        double average_latency_ms;
        double cpu_usage_percent;
    };
    
    RuntimeStats get_stats() const;
    
private:
    Runtime();
    ~Runtime();
    
    // 初始化子系统
    void init_scheduler(const RuntimeConfig& config);
    void init_shm_manager(const RuntimeConfig& config);
    void init_msg_bus(const RuntimeConfig& config);
    
    // 配置
    RuntimeConfig config_;
    
    // 运行状态
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // 子系统
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<ShmManager> shm_manager_;
    std::unique_ptr<MsgBus> msg_bus_;
    
    // Block 注册表
    mutable std::shared_mutex blocks_mutex_;
    std::unordered_map<BlockId, std::unique_ptr<Block>> blocks_;
    std::atomic<uint64_t> next_block_id_{1};
    
    // 流图
    FlowGraph flow_graph_;
    
    // 日志
    std::unique_ptr<Logger> logger_;
};

} // namespace multiqueue
```

### 2.2 Runtime 生命周期

```cpp
// 状态转换图
//
//  [Created] ---initialize()---> [Initialized]
//      ^                              |
//      |                         start()
//      |                              ↓
//  shutdown()                    [Running]
//      ^                              |
//      |                          stop()
//      |                              ↓
//  [Stopped] <----------------------+
```

### 2.3 错误处理策略

```cpp
/**
 * @brief Runtime 异常基类
 */
class RuntimeException : public std::runtime_error {
public:
    explicit RuntimeException(const std::string& msg) 
        : std::runtime_error(msg) {}
};

class InitializationException : public RuntimeException {
    using RuntimeException::RuntimeException;
};

class BlockNotFoundException : public RuntimeException {
    using RuntimeException::RuntimeException;
};

class ConnectionException : public RuntimeException {
    using RuntimeException::RuntimeException;
};
```

---

## 3. 共享内存管理

### 3.1 ShmManager 类设计

```cpp
/**
 * @brief 共享内存管理器配置
 */
struct ShmConfig {
    std::string name_prefix = "mqshm_";
    
    // 内存池配置
    struct PoolConfig {
        std::string name;
        size_t block_size;      // 单个 Buffer 大小
        size_t block_count;     // Buffer 数量
        bool expandable;        // 是否可扩展
        size_t max_blocks;      // 最大 Buffer 数量（可扩展时）
    };
    
    std::vector<PoolConfig> pools;
    
    // 默认池配置
    static ShmConfig default_config() {
        ShmConfig config;
        config.pools = {
            {"small",  4096,    1024, true, 4096},     // 4KB
            {"medium", 65536,   512,  true, 2048},     // 64KB
            {"large",  1048576, 128,  true, 512}       // 1MB
        };
        return config;
    }
};

/**
 * @brief 共享内存管理器
 */
class ShmManager {
public:
    explicit ShmManager(const ShmConfig& config);
    ~ShmManager();
    
    // ===== 初始化 =====
    
    void initialize();
    void shutdown();
    
    // ===== Buffer 分配 =====
    
    /**
     * @brief 分配指定大小的 Buffer
     * @param size 所需大小（字节）
     * @return Buffer 智能指针
     * @throws std::bad_alloc 如果无法分配
     */
    BufferPtr allocate(size_t size);
    
    /**
     * @brief 从指定池分配 Buffer
     * @param pool_name 池名称
     * @return Buffer 智能指针
     */
    BufferPtr allocate_from_pool(const std::string& pool_name);
    
    /**
     * @brief 释放 Buffer（通常由智能指针自动调用）
     * @param buffer Buffer 指针
     */
    void release(Buffer* buffer);
    
    // ===== 引用计数 =====
    
    /**
     * @brief 增加引用计数
     */
    void add_ref(BufferId id);
    
    /**
     * @brief 减少引用计数
     * @return 当前引用计数
     */
    uint32_t remove_ref(BufferId id);
    
    /**
     * @brief 获取引用计数
     */
    uint32_t get_ref_count(BufferId id) const;
    
    // ===== 池管理 =====
    
    /**
     * @brief 添加新的内存池
     */
    void add_pool(const ShmConfig::PoolConfig& config);
    
    /**
     * @brief 移除内存池
     */
    void remove_pool(const std::string& name);
    
    /**
     * @brief 获取池信息
     */
    BufferPool* get_pool(const std::string& name);
    
    /**
     * @brief 列出所有池
     */
    std::vector<std::string> list_pools() const;
    
    // ===== 统计信息 =====
    
    struct ShmStats {
        size_t total_pools;
        size_t total_capacity;      // 总容量（字节）
        size_t total_allocated;     // 已分配（字节）
        size_t total_free;          // 剩余（字节）
        size_t allocation_count;    // 分配次数
        size_t deallocation_count;  // 释放次数
        
        // 每个池的统计
        struct PoolStats {
            std::string name;
            size_t block_size;
            size_t block_count;
            size_t blocks_used;
            size_t blocks_free;
            double utilization;     // 使用率
        };
        std::vector<PoolStats> pool_stats;
    };
    
    ShmStats get_stats() const;
    
private:
    ShmConfig config_;
    
    // 内存池集合
    mutable std::shared_mutex pools_mutex_;
    std::unordered_map<std::string, std::unique_ptr<BufferPool>> pools_;
    
    // Buffer 元数据表
    mutable std::shared_mutex metadata_mutex_;
    std::unordered_map<BufferId, BufferMetadata> buffer_metadata_;
    
    // Buffer ID 生成器
    std::atomic<uint64_t> next_buffer_id_{1};
    
    // 选择合适的池
    BufferPool* select_pool(size_t size);
};
```

### 3.2 BufferPool 类设计

```cpp
/**
 * @brief 固定大小的 Buffer 池
 */
class BufferPool {
public:
    BufferPool(const std::string& name,
               size_t block_size,
               size_t block_count,
               bool expandable = false,
               size_t max_blocks = 0);
    
    ~BufferPool();
    
    // ===== 分配/释放 =====
    
    /**
     * @brief 分配一个 Buffer
     * @return Buffer 数据指针，nullptr 如果池已满
     */
    void* allocate();
    
    /**
     * @brief 释放一个 Buffer
     * @param ptr Buffer 数据指针
     */
    void deallocate(void* ptr);
    
    // ===== 扩展 =====
    
    /**
     * @brief 扩展池（如果允许）
     * @param additional_count 增加的 Buffer 数量
     * @return 是否成功扩展
     */
    bool expand(size_t additional_count);
    
    // ===== 信息查询 =====
    
    const std::string& name() const { return name_; }
    size_t block_size() const { return block_size_; }
    size_t capacity() const { return capacity_; }
    size_t available() const;
    size_t used() const { return capacity_ - available(); }
    double utilization() const { return (double)used() / capacity(); }
    
private:
    std::string name_;
    size_t block_size_;
    size_t capacity_;
    bool expandable_;
    size_t max_blocks_;
    
    // 共享内存对象
    std::unique_ptr<boost::interprocess::managed_shared_memory> shm_;
    
    // Arena 内存管理
    struct Arena {
        void* base;
        size_t size;
        size_t block_count;
    };
    std::vector<Arena> arenas_;
    
    // 空闲列表（使用锁保护）
    mutable std::mutex free_list_mutex_;
    std::vector<void*> free_list_;
    
    // 创建新的 Arena
    bool create_arena(size_t block_count);
};
```

### 3.3 Buffer 和 BufferMetadata

```cpp
/**
 * @brief Buffer 唯一标识
 */
using BufferId = uint64_t;

/**
 * @brief Buffer 元数据（存储在 ShmManager 中）
 */
struct BufferMetadata {
    BufferId id;
    
    // 内存信息
    void* data;                         // 数据指针
    size_t size;                        // Buffer 大小
    BufferPool* pool;                   // 所属池
    
    // 引用计数
    std::atomic<uint32_t> ref_count;
    
    // 时间戳
    uint64_t alloc_timestamp;           // 分配时间
    uint64_t data_timestamp;            // 数据时间戳
    
    // 状态
    std::atomic<bool> valid;            // 是否有效
    
    BufferMetadata()
        : id(0)
        , data(nullptr)
        , size(0)
        , pool(nullptr)
        , ref_count(0)
        , alloc_timestamp(0)
        , data_timestamp(0)
        , valid(false)
    {}
};

/**
 * @brief Buffer 智能指针包装
 */
class Buffer {
public:
    Buffer(BufferId id, void* data, size_t size, ShmManager* manager);
    ~Buffer();
    
    // 禁止拷贝，允许移动
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    
    // ===== 数据访问 =====
    
    void* data() { return data_; }
    const void* data() const { return data_; }
    size_t size() const { return size_; }
    
    // 类型化访问
    template<typename T>
    T* as() { return static_cast<T*>(data_); }
    
    template<typename T>
    const T* as() const { return static_cast<const T*>(data_); }
    
    // ===== 元数据 =====
    
    BufferId id() const { return id_; }
    
    uint64_t timestamp() const { return timestamp_; }
    void set_timestamp(uint64_t ts) { timestamp_ = ts; }
    
    uint32_t ref_count() const;
    
    // ===== 引用计数（通常由智能指针管理，手动使用需谨慎）=====
    
    void add_ref();
    void remove_ref();
    
private:
    BufferId id_;
    void* data_;
    size_t size_;
    uint64_t timestamp_;
    ShmManager* manager_;
};

/**
 * @brief Buffer 智能指针
 */
using BufferPtr = std::shared_ptr<Buffer>;
```

### 3.4 共享内存布局

```
┌─────────────────────────────────────────────────────────────┐
│  Shared Memory Segment: "mqshm_pool_small"                  │
├─────────────────────────────────────────────────────────────┤
│  Header (managed_shared_memory 内部管理)                     │
├─────────────────────────────────────────────────────────────┤
│  Buffer Block #0 (4096 bytes)                               │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  User Data Area (4096 bytes)                          │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  Buffer Block #1 (4096 bytes)                               │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  User Data Area (4096 bytes)                          │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  ...                                                        │
├─────────────────────────────────────────────────────────────┤
│  Buffer Block #1023 (4096 bytes)                            │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  User Data Area (4096 bytes)                          │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

注：元数据（BufferMetadata）不存储在共享内存中，
    而是存储在 ShmManager 的进程内存中。
```

---

## 4. 调度器设计

### 4.1 Scheduler 类设计

```cpp
/**
 * @brief 调度策略
 */
enum class SchedulePolicy {
    ROUND_ROBIN,      // 轮询调度
    PRIORITY,         // 优先级调度
    WORK_STEALING     // 工作窃取
};

/**
 * @brief 调度器
 */
class Scheduler {
public:
    Scheduler(size_t thread_count, SchedulePolicy policy);
    ~Scheduler();
    
    // ===== 生命周期 =====
    
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // ===== Block 调度 =====
    
    /**
     * @brief 调度一个 Block（将其加入调度队列）
     * @param block_id Block ID
     */
    void schedule_block(BlockId block_id);
    
    /**
     * @brief 取消调度一个 Block
     * @param block_id Block ID
     */
    void unschedule_block(BlockId block_id);
    
    /**
     * @brief 设置 Block 优先级
     * @param block_id Block ID
     * @param priority 优先级（数值越大优先级越高）
     */
    void set_block_priority(BlockId block_id, int priority);
    
    // ===== Buffer 路由接口（给 Block 调用）=====
    
    /**
     * @brief 为输出端口分配 Buffer
     * @param block_id Block ID
     * @param port_name 端口名
     * @param size Buffer 大小
     * @return Buffer 智能指针
     */
    BufferPtr allocate_output_buffer(BlockId block_id, 
                                     const std::string& port_name,
                                     size_t size);
    
    /**
     * @brief 发布 Buffer 到输出端口（路由到下游）
     * @param block_id Block ID
     * @param port_name 端口名
     * @param buffer Buffer 智能指针
     */
    void publish_buffer(BlockId block_id,
                       const std::string& port_name,
                       BufferPtr buffer);
    
    /**
     * @brief 从输入端口消费 Buffer
     * @param block_id Block ID
     * @param port_name 端口名
     * @param timeout_ms 超时时间（毫秒），0 表示非阻塞
     * @return Buffer 智能指针，nullptr 如果超时或队列空
     */
    BufferPtr consume_input_buffer(BlockId block_id,
                                   const std::string& port_name,
                                   uint32_t timeout_ms = 0);
    
    // ===== 连接管理 =====
    
    /**
     * @brief 添加连接
     */
    void add_connection(const Connection& conn);
    
    /**
     * @brief 移除连接
     */
    void remove_connection(const Connection& conn);
    
    /**
     * @brief 获取输出端口的下游端口列表
     */
    std::vector<InputPort> get_downstream_ports(const OutputPort& port) const;
    
    // ===== 统计信息 =====
    
    struct SchedulerStats {
        size_t thread_count;
        size_t active_blocks;
        size_t pending_tasks;
        uint64_t total_tasks_executed;
        double average_task_duration_us;
        
        // 每个线程的统计
        struct ThreadStats {
            size_t thread_id;
            uint64_t tasks_executed;
            double cpu_usage_percent;
            uint64_t idle_time_us;
        };
        std::vector<ThreadStats> thread_stats;
    };
    
    SchedulerStats get_stats() const;
    
private:
    // ===== 工作线程 =====
    
    void worker_thread_func(size_t thread_id);
    
    // ===== Buffer 路由 =====
    
    void route_buffer(const OutputPort& src, BufferPtr buffer);
    
    // ===== 任务管理 =====
    
    struct Task {
        BlockId block_id;
        int priority;
        uint64_t enqueue_time;
    };
    
    // 任务队列（支持不同调度策略）
    std::unique_ptr<TaskQueue> task_queue_;
    
    // ===== 配置 =====
    
    size_t thread_count_;
    SchedulePolicy policy_;
    
    // ===== 线程池 =====
    
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_{false};
    
    // ===== 端口队列 =====
    
    // 每个输入端口对应一个 Buffer 队列
    mutable std::shared_mutex port_queues_mutex_;
    std::unordered_map<InputPort, std::shared_ptr<BufferQueue>> input_queues_;
    
    // ===== 路由表 =====
    
    mutable std::shared_mutex routing_mutex_;
    std::unordered_map<OutputPort, std::vector<InputPort>> routing_table_;
    
    // ===== Block 信息 =====
    
    mutable std::shared_mutex block_info_mutex_;
    struct BlockInfo {
        int priority;
        bool scheduled;
        uint64_t last_execution_time;
    };
    std::unordered_map<BlockId, BlockInfo> block_info_;
};
```

### 4.2 Port 和 Connection 定义

```cpp
/**
 * @brief Block ID
 */
using BlockId = uint64_t;

/**
 * @brief 输出端口标识
 */
struct OutputPort {
    BlockId block_id;
    std::string port_name;
    
    bool operator==(const OutputPort& other) const {
        return block_id == other.block_id && port_name == other.port_name;
    }
};

/**
 * @brief 输入端口标识
 */
struct InputPort {
    BlockId block_id;
    std::string port_name;
    
    bool operator==(const InputPort& other) const {
        return block_id == other.block_id && port_name == other.port_name;
    }
};

// Hash 函数（用于 unordered_map）
namespace std {
    template<>
    struct hash<OutputPort> {
        size_t operator()(const OutputPort& p) const {
            return hash<uint64_t>()(p.block_id) ^ hash<string>()(p.port_name);
        }
    };
    
    template<>
    struct hash<InputPort> {
        size_t operator()(const InputPort& p) const {
            return hash<uint64_t>()(p.block_id) ^ hash<string>()(p.port_name);
        }
    };
}

/**
 * @brief 连接
 */
struct Connection {
    OutputPort src;
    InputPort dst;
    
    bool operator==(const Connection& other) const {
        return src == other.src && dst == other.dst;
    }
};
```

### 4.3 TaskQueue 设计

```cpp
/**
 * @brief 任务队列接口
 */
class TaskQueue {
public:
    virtual ~TaskQueue() = default;
    
    virtual void push(const Scheduler::Task& task) = 0;
    virtual bool try_pop(Scheduler::Task& task) = 0;
    virtual bool empty() const = 0;
    virtual size_t size() const = 0;
};

/**
 * @brief 轮询队列（FIFO）
 */
class RoundRobinQueue : public TaskQueue {
    // 使用 std::queue + std::mutex + std::condition_variable
};

/**
 * @brief 优先级队列
 */
class PriorityQueue : public TaskQueue {
    // 使用 std::priority_queue
};

/**
 * @brief 工作窃取队列
 */
class WorkStealingQueue : public TaskQueue {
    // 每个线程一个本地队列 + 窃取机制
};
```

### 4.4 BufferQueue 设计

```cpp
/**
 * @brief Buffer 队列（输入端口的 Buffer 缓冲区）
 */
class BufferQueue {
public:
    explicit BufferQueue(size_t capacity = 0);  // 0 表示无限
    
    /**
     * @brief 推送 Buffer
     * @param buffer Buffer 智能指针
     * @return 是否成功（队列满时失败）
     */
    bool push(BufferPtr buffer);
    
    /**
     * @brief 弹出 Buffer
     * @param timeout_ms 超时时间（毫秒）
     * @return Buffer 智能指针，nullptr 如果超时
     */
    BufferPtr pop(uint32_t timeout_ms = 0);
    
    /**
     * @brief 尝试弹出（非阻塞）
     */
    BufferPtr try_pop();
    
    bool empty() const;
    size_t size() const;
    size_t capacity() const { return capacity_; }
    
private:
    size_t capacity_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<BufferPtr> queue_;
};
```

### 4.5 FlowGraph 设计

```cpp
/**
 * @brief 流图 - 维护 Block 和 Connection 的拓扑关系
 */
class FlowGraph {
public:
    FlowGraph() = default;
    
    // ===== 连接管理 =====
    
    void add_edge(const Connection& conn);
    void remove_edge(const Connection& conn);
    
    // ===== 查询 =====
    
    /**
     * @brief 获取输出端口的所有下游端口
     */
    std::vector<InputPort> get_downstream_ports(const OutputPort& src) const;
    
    /**
     * @brief 获取输入端口的所有上游端口
     */
    std::vector<OutputPort> get_upstream_ports(const InputPort& dst) const;
    
    /**
     * @brief 检查是否存在连接
     */
    bool has_connection(const Connection& conn) const;
    
    /**
     * @brief 列出所有连接
     */
    std::vector<Connection> list_connections() const;
    
    // ===== 拓扑分析 =====
    
    /**
     * @brief 拓扑排序（用于优化调度顺序）
     * @return Block ID 列表（按依赖顺序）
     */
    std::vector<BlockId> topological_sort() const;
    
    /**
     * @brief 检测环
     * @return 是否有环
     */
    bool has_cycle() const;
    
    /**
     * @brief 查找 Block 的所有依赖
     */
    std::unordered_set<BlockId> get_dependencies(BlockId block_id) const;
    
private:
    // 邻接表：OutputPort -> [InputPort]
    mutable std::shared_mutex mutex_;
    std::unordered_map<OutputPort, std::vector<InputPort>> adjacency_list_;
    
    // 反向邻接表：InputPort -> [OutputPort]
    std::unordered_map<InputPort, std::vector<OutputPort>> reverse_adjacency_list_;
};
```

---

## 5. 消息总线

### 5.1 MsgBus 类设计

```cpp
/**
 * @brief 消息总线
 */
class MsgBus {
public:
    MsgBus();
    ~MsgBus();
    
    void start();
    void stop();
    
    // ===== 发布-订阅 =====
    
    /**
     * @brief 发布消息
     * @param topic 主题
     * @param msg 消息
     */
    void publish(const std::string& topic, const Message& msg);
    
    /**
     * @brief 订阅主题
     * @param topic 主题（支持通配符 * ）
     * @param callback 回调函数
     * @return 订阅 ID
     */
    SubscriptionId subscribe(const std::string& topic, MessageCallback callback);
    
    /**
     * @brief 取消订阅
     * @param id 订阅 ID
     */
    void unsubscribe(SubscriptionId id);
    
    // ===== 请求-响应 =====
    
    /**
     * @brief 发送请求并等待响应
     * @param endpoint 端点
     * @param req 请求
     * @param timeout_ms 超时时间
     * @return 响应
     */
    Response request(const std::string& endpoint, 
                    const Request& req,
                    uint32_t timeout_ms = 1000);
    
    /**
     * @brief 注册请求处理器
     * @param endpoint 端点
     * @param handler 处理器
     */
    void register_handler(const std::string& endpoint, RequestHandler handler);
    
    /**
     * @brief 注销请求处理器
     */
    void unregister_handler(const std::string& endpoint);
    
    // ===== 广播 =====
    
    /**
     * @brief 广播消息（所有订阅者接收）
     * @param msg 消息
     */
    void broadcast(const Message& msg);
    
private:
    // 分发线程
    void dispatch_thread_func();
    
    // 消息队列
    ThreadSafeQueue<std::pair<std::string, Message>> msg_queue_;
    
    // 订阅表
    mutable std::shared_mutex subscriptions_mutex_;
    std::unordered_map<std::string, std::vector<Subscription>> subscriptions_;
    std::atomic<uint64_t> next_subscription_id_{1};
    
    // 请求处理器
    mutable std::shared_mutex handlers_mutex_;
    std::unordered_map<std::string, RequestHandler> handlers_;
    
    // 待处理的请求
    mutable std::mutex pending_requests_mutex_;
    std::unordered_map<RequestId, std::promise<Response>> pending_requests_;
    std::atomic<uint64_t> next_request_id_{1};
    
    // 分发线程
    std::thread dispatch_thread_;
    std::atomic<bool> running_{false};
};
```

### 5.2 Message 定义

```cpp
/**
 * @brief 消息类型
 */
enum class MessageType {
    CONTROL,      // 控制消息（start/stop/pause/resume）
    PARAMETER,    // 参数更改
    STATUS,       // 状态更新
    EVENT,        // 事件通知
    LOG           // 日志消息
};

/**
 * @brief 消息
 */
struct Message {
    MessageType type;
    std::string sender;         // 发送者 ID
    std::string topic;          // 主题
    std::any payload;           // 负载（可以是任意类型）
    uint64_t timestamp;         // 时间戳（纳秒）
    
    // 构造函数
    Message() : type(MessageType::EVENT), timestamp(0) {}
    
    Message(MessageType t, const std::string& s, const std::string& tp, std::any p)
        : type(t), sender(s), topic(tp), payload(std::move(p))
        , timestamp(get_timestamp_ns())
    {}
};

/**
 * @brief 消息回调
 */
using MessageCallback = std::function<void(const Message&)>;

/**
 * @brief 订阅 ID
 */
using SubscriptionId = uint64_t;

/**
 * @brief 订阅信息
 */
struct Subscription {
    SubscriptionId id;
    std::string topic;
    MessageCallback callback;
};
```

### 5.3 Request/Response 定义

```cpp
/**
 * @brief 请求 ID
 */
using RequestId = uint64_t;

/**
 * @brief 请求
 */
struct Request {
    RequestId id;
    std::string sender;
    std::string endpoint;
    std::any payload;
    uint64_t timestamp;
};

/**
 * @brief 响应
 */
struct Response {
    RequestId request_id;
    bool success;
    std::any payload;
    std::string error_message;
    uint64_t timestamp;
};

/**
 * @brief 请求处理器
 */
using RequestHandler = std::function<Response(const Request&)>;
```

---

## 6. Block 框架

### 6.1 Block 基类

```cpp
/**
 * @brief Block 类型
 */
enum class BlockType {
    SOURCE,       // 数据源（只有输出端口）
    SINK,         // 数据接收器（只有输入端口）
    PROCESSING    // 处理模块（有输入和输出端口）
};

/**
 * @brief 工作结果
 */
enum class WorkResult {
    OK,                 // 正常完成
    NEED_MORE_INPUT,    // 需要更多输入数据
    OUTPUT_FULL,        // 输出队列满
    DONE,               // 完成（用于有限数据源）
    ERROR               // 错误
};

/**
 * @brief 端口配置
 */
struct PortConfig {
    size_t buffer_size = 0;         // Buffer 大小（0 表示使用默认）
    size_t queue_capacity = 0;      // 队列容量（0 表示无限）
    bool optional = false;          // 是否可选（可选端口可以不连接）
};

/**
 * @brief Block 基类
 */
class Block {
public:
    Block(const std::string& name, BlockType type);
    virtual ~Block() = default;
    
    // 禁止拷贝和移动
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;
    
    // ===== 生命周期 =====
    
    /**
     * @brief 初始化（在 register_block 后调用）
     */
    virtual void initialize() {}
    
    /**
     * @brief 启动（在 Runtime::start 时调用）
     */
    virtual void start() {}
    
    /**
     * @brief 停止（在 Runtime::stop 时调用）
     */
    virtual void stop() {}
    
    /**
     * @brief 清理（在 unregister_block 时调用）
     */
    virtual void cleanup() {}
    
    // ===== 工作函数（由调度器调用）=====
    
    /**
     * @brief 执行一次处理
     * @return 工作结果
     * 
     * 这个函数会被调度器反复调用，每次应该：
     * 1. 从输入端口获取数据
     * 2. 处理数据
     * 3. 发布到输出端口
     * 4. 返回结果状态
     */
    virtual WorkResult work() = 0;
    
    // ===== 端口管理 =====
    
    void add_input_port(const std::string& name, PortConfig config = {});
    void add_output_port(const std::string& name, PortConfig config = {});
    
    bool has_input_port(const std::string& name) const;
    bool has_output_port(const std::string& name) const;
    
    std::vector<std::string> list_input_ports() const;
    std::vector<std::string> list_output_ports() const;
    
    // ===== Block 信息 =====
    
    BlockId id() const { return id_; }
    const std::string& name() const { return name_; }
    BlockType type() const { return type_; }
    
    void set_id(BlockId id) { id_ = id; }
    
    // ===== 消息处理 =====
    
    /**
     * @brief 处理消息总线消息
     * @param msg 消息
     */
    virtual void handle_message(const Message& msg) {}
    
    // ===== 参数管理 =====
    
    template<typename T>
    T get_parameter(const std::string& name) const {
        std::shared_lock lock(params_mutex_);
        auto it = parameters_.find(name);
        if (it == parameters_.end()) {
            throw std::runtime_error("Parameter not found: " + name);
        }
        return std::any_cast<T>(it->second);
    }
    
    template<typename T>
    void set_parameter(const std::string& name, const T& value) {
        std::unique_lock lock(params_mutex_);
        parameters_[name] = value;
        
        // 发布参数更改消息
        Message msg{
            MessageType::PARAMETER,
            std::to_string(id_),
            "parameter." + name,
            value
        };
        runtime().msg_bus().publish("parameter." + name, msg);
    }
    
    bool has_parameter(const std::string& name) const;
    std::vector<std::string> list_parameters() const;
    
protected:
    // ===== 数据操作接口（子类使用）=====
    
    /**
     * @brief 从输入端口获取 Buffer
     * @param port_name 端口名
     * @param timeout_ms 超时时间（毫秒）
     * @return Buffer 智能指针，nullptr 如果超时或队列空
     */
    BufferPtr get_input_buffer(const std::string& port_name, 
                              uint32_t timeout_ms = 0);
    
    /**
     * @brief 发布 Buffer 到输出端口
     * @param port_name 端口名
     * @param buffer Buffer 智能指针
     */
    void produce_output(const std::string& port_name, BufferPtr buffer);
    
    /**
     * @brief 为输出分配 Buffer
     * @param port_name 端口名
     * @param size Buffer 大小
     * @return Buffer 智能指针
     */
    BufferPtr allocate_output_buffer(const std::string& port_name, size_t size);
    
    // ===== Runtime 访问 =====
    
    Runtime& runtime() { return Runtime::instance(); }
    const Runtime& runtime() const { return Runtime::instance(); }
    
private:
    BlockId id_{0};
    std::string name_;
    BlockType type_;
    
    // 端口
    mutable std::shared_mutex ports_mutex_;
    std::unordered_map<std::string, PortConfig> input_ports_;
    std::unordered_map<std::string, PortConfig> output_ports_;
    
    // 参数
    mutable std::shared_mutex params_mutex_;
    std::unordered_map<std::string, std::any> parameters_;
};
```

### 6.2 内置 Block 示例

#### FileSource Block

```cpp
/**
 * @brief 文件数据源
 */
class FileSourceBlock : public Block {
public:
    explicit FileSourceBlock(const std::string& filename,
                            size_t chunk_size = 4096)
        : Block("FileSource", BlockType::SOURCE)
        , filename_(filename)
        , chunk_size_(chunk_size)
    {
        add_output_port("out", PortConfig{.buffer_size = chunk_size});
        set_parameter("filename", filename);
        set_parameter("chunk_size", chunk_size);
    }
    
    void initialize() override {
        file_.open(filename_, std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename_);
        }
    }
    
    WorkResult work() override {
        // 分配输出 Buffer
        auto buffer = allocate_output_buffer("out", chunk_size_);
        
        // 读取数据
        file_.read(static_cast<char*>(buffer->data()), chunk_size_);
        size_t bytes_read = file_.gcount();
        
        if (bytes_read == 0) {
            return WorkResult::DONE;  // 文件读完
        }
        
        // 调整 Buffer 大小（如果读取不足）
        // 注：这里假设 Buffer 支持 resize，实际实现可能需要重新分配
        
        // 设置时间戳
        buffer->set_timestamp(get_timestamp_ns());
        
        // 发布
        produce_output("out", buffer);
        
        return WorkResult::OK;
    }
    
    void cleanup() override {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
private:
    std::string filename_;
    size_t chunk_size_;
    std::ifstream file_;
};
```

#### Amplifier Block

```cpp
/**
 * @brief 信号放大器
 */
class AmplifierBlock : public Block {
public:
    explicit AmplifierBlock(float gain = 1.0f)
        : Block("Amplifier", BlockType::PROCESSING)
        , gain_(gain)
    {
        add_input_port("in");
        add_output_port("out");
        set_parameter("gain", gain);
    }
    
    WorkResult work() override {
        // 获取输入
        auto input = get_input_buffer("in", 100);  // 100ms 超时
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 分配输出
        auto output = allocate_output_buffer("out", input->size());
        
        // 处理（假设数据是 float 数组）
        const float* in_data = input->as<float>();
        float* out_data = output->as<float>();
        size_t count = input->size() / sizeof(float);
        
        for (size_t i = 0; i < count; ++i) {
            out_data[i] = in_data[i] * gain_;
        }
        
        // 保留时间戳
        output->set_timestamp(input->timestamp());
        
        // 发布
        produce_output("out", output);
        
        return WorkResult::OK;
    }
    
    void handle_message(const Message& msg) override {
        if (msg.type == MessageType::PARAMETER && msg.topic == "parameter.gain") {
            try {
                gain_ = std::any_cast<float>(msg.payload);
            } catch (const std::bad_any_cast&) {
                // 忽略类型错误
            }
        }
    }
    
private:
    float gain_;
};
```

#### FileSink Block

```cpp
/**
 * @brief 文件接收器
 */
class FileSinkBlock : public Block {
public:
    explicit FileSinkBlock(const std::string& filename)
        : Block("FileSink", BlockType::SINK)
        , filename_(filename)
    {
        add_input_port("in");
        set_parameter("filename", filename);
    }
    
    void initialize() override {
        file_.open(filename_, std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename_);
        }
    }
    
    WorkResult work() override {
        auto input = get_input_buffer("in", 100);
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 写入文件
        file_.write(static_cast<const char*>(input->data()), input->size());
        bytes_written_ += input->size();
        
        return WorkResult::OK;
    }
    
    void cleanup() override {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
private:
    std::string filename_;
    std::ofstream file_;
    uint64_t bytes_written_{0};
};
```

---

（由于篇幅限制，我将继续在下一个文件中编写剩余部分）

## 7-12 节内容将在后续文件中补充...

请先审阅这部分内容！

