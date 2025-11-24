# 多进程 Buffer 管理详细设计

> **核心原则**：严格多进程支持，单进程多线程只是特例

---

## 1. 设计原则

### 1.1 多进程优先 ⚠️

**错误的思路**：
```
设计单进程多线程 → 扩展到多进程
```

**正确的思路**：
```
严格按多进程设计 → 单进程多线程自然支持
```

### 1.2 核心原则

1. **所有状态在共享内存** ✅
   - BufferMetadata 在共享内存
   - 引用计数在共享内存
   - 所有同步原语使用 Boost.Interprocess

2. **进程本地对象只是包装** ✅
   - BufferPtr 是进程本地的轻量级包装
   - 不存储任何状态，只操作共享内存

3. **绝对的进程安全** ✅
   - 所有操作使用原子操作或进程间锁
   - 不依赖进程内存

---

## 2. 共享内存布局

### 2.1 完整的共享内存结构

```
┌─────────────────────────────────────────────────────────────────┐
│  Shared Memory Segment: "mqshm_global"                          │
├─────────────────────────────────────────────────────────────────┤
│  [Global Header]                                                 │
│  - magic_number                                                  │
│  - version                                                       │
│  - total_size                                                    │
├─────────────────────────────────────────────────────────────────┤
│  [Global Registry]                                               │
│  ├─ ProcessRegistry                                             │
│  ├─ BlockRegistry                                               │
│  ├─ ConnectionRegistry                                          │
│  ├─ BufferPoolRegistry                                          │
│  └─ MessageBus                                                  │
├─────────────────────────────────────────────────────────────────┤
│  [BufferMetadata Table]  ← 核心！                               │
│  ├─ interprocess_mutex table_mutex                              │
│  ├─ uint32_t buffer_count                                       │
│  ├─ BufferMetadata entries[MAX_BUFFERS]                        │
│  │   └─ 每个 BufferMetadata:                                   │
│  │       - buffer_id                                            │
│  │       - pool_id                                              │
│  │       - offset_in_pool                                       │
│  │       - size                                                 │
│  │       - std::atomic<uint32_t> ref_count  ← 跨进程原子操作   │
│  │       - data_offset (相对偏移)                              │
│  │       - timestamp                                            │
│  └─ FreeList free_slots                                         │
├─────────────────────────────────────────────────────────────────┤
│  [Buffer Pools]                                                  │
│  └─ 独立的共享内存段（"mqshm_pool_small" 等）                   │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  Shared Memory Segment: "mqshm_pool_small"                      │
├─────────────────────────────────────────────────────────────────┤
│  [Pool Header]                                                   │
│  - pool_id                                                       │
│  - block_size (4096)                                            │
│  - block_count (1024)                                           │
│  - interprocess_mutex pool_mutex                                │
├─────────────────────────────────────────────────────────────────┤
│  [Free List]                                                     │
│  - std::atomic<uint32_t> free_count                             │
│  - std::atomic<uint32_t> free_head                              │
│  - uint32_t next_free[block_count]  (链表结构)                  │
├─────────────────────────────────────────────────────────────────┤
│  [Data Blocks]                                                   │
│  ├─ Block #0 [4096 bytes]                                       │
│  ├─ Block #1 [4096 bytes]                                       │
│  ├─ ...                                                          │
│  └─ Block #1023 [4096 bytes]                                    │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 BufferMetadata 详细结构

```cpp
/**
 * @brief Buffer 元数据（存储在共享内存）
 * 
 * 注意：此结构体的所有成员都必须是 POD 类型或支持跨进程的类型
 */
struct alignas(64) BufferMetadata {
    // ===== 标识信息 =====
    uint64_t buffer_id;          // 全局唯一 ID
    
    // ===== 内存位置信息 =====
    uint32_t pool_id;            // 所属池 ID
    uint32_t block_index;        // 在池中的块索引
    size_t size;                 // 实际数据大小
    
    // ===== 引用计数（跨进程原子操作）=====
    std::atomic<uint32_t> ref_count;
    
    // 注意：std::atomic 在共享内存中是安全的，前提是：
    // 1. 共享内存是以 read/write 模式映射的
    // 2. CPU 支持原子操作指令
    // 3. 内存对齐正确（已使用 alignas(64)）
    
    // ===== 数据位置 =====
    // 不存储指针！只存储相对偏移量
    uint64_t data_shm_offset;    // 相对于池共享内存基地址的偏移
    
    // ===== 时间戳信息 =====
    Timestamp timestamp;
    Timestamp start_time;
    Timestamp end_time;
    bool has_time_range;
    
    // ===== 状态 =====
    std::atomic<bool> valid;     // 是否有效
    
    // ===== 所有者信息（用于清理）=====
    uint32_t creator_process_slot;  // 创建此 Buffer 的进程槽位
    uint64_t alloc_time_ns;         // 分配时间
    
    // ===== 填充到缓存行边界 =====
    char padding[/* 计算得出 */];
    
    /**
     * @brief 原子地增加引用计数
     * @return 增加后的引用计数
     */
    uint32_t add_ref() {
        return ref_count.fetch_add(1, std::memory_order_acq_rel);
    }
    
    /**
     * @brief 原子地减少引用计数
     * @return 减少后的引用计数
     */
    uint32_t remove_ref() {
        return ref_count.fetch_sub(1, std::memory_order_acq_rel);
    }
    
    /**
     * @brief 获取当前引用计数
     */
    uint32_t get_ref_count() const {
        return ref_count.load(std::memory_order_acquire);
    }
} __attribute__((packed));  // 确保内存布局紧凑

// 静态断言：确保 std::atomic 可以在共享内存中使用
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "std::atomic<uint32_t> must be lock-free for cross-process usage");
```

### 2.3 BufferMetadata Table

```cpp
/**
 * @brief Buffer 元数据表（存储在共享内存）
 */
struct BufferMetadataTable {
    static constexpr size_t MAX_BUFFERS = 4096;
    
    // ===== 同步原语（跨进程）=====
    interprocess_mutex table_mutex;  // 表级锁（用于分配/释放槽位）
    
    // ===== 计数器 =====
    std::atomic<uint32_t> allocated_count;  // 已分配数量
    std::atomic<uint64_t> next_buffer_id;   // 下一个 Buffer ID
    
    // ===== 元数据数组 =====
    BufferMetadata entries[MAX_BUFFERS];
    
    // ===== 空闲链表 =====
    std::atomic<int32_t> free_head;  // 空闲链表头（-1 表示空）
    int32_t next_free[MAX_BUFFERS];  // 下一个空闲槽位索引
    
    /**
     * @brief 初始化表（只由第一个进程调用）
     */
    void initialize() {
        // 初始化锁
        new (&table_mutex) interprocess_mutex();
        
        // 初始化计数器
        allocated_count.store(0, std::memory_order_relaxed);
        next_buffer_id.store(1, std::memory_order_relaxed);
        
        // 初始化所有条目
        for (size_t i = 0; i < MAX_BUFFERS; ++i) {
            entries[i].valid.store(false, std::memory_order_relaxed);
            entries[i].ref_count.store(0, std::memory_order_relaxed);
            next_free[i] = (i + 1 < MAX_BUFFERS) ? (i + 1) : -1;
        }
        
        // 初始化空闲链表
        free_head.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief 分配一个 BufferMetadata 槽位
     * @return 槽位索引，-1 表示失败
     */
    int32_t allocate_slot() {
        scoped_lock<interprocess_mutex> lock(table_mutex);
        
        // 从空闲链表获取
        int32_t slot = free_head.load(std::memory_order_acquire);
        if (slot < 0) {
            return -1;  // 无可用槽位
        }
        
        // 更新链表头
        free_head.store(next_free[slot], std::memory_order_release);
        
        // 分配 Buffer ID
        uint64_t buffer_id = next_buffer_id.fetch_add(1, std::memory_order_acq_rel);
        
        // 初始化槽位
        BufferMetadata& meta = entries[slot];
        meta.buffer_id = buffer_id;
        meta.ref_count.store(0, std::memory_order_relaxed);
        meta.valid.store(false, std::memory_order_relaxed);  // 稍后设为 true
        
        allocated_count.fetch_add(1, std::memory_order_relaxed);
        
        return slot;
    }
    
    /**
     * @brief 释放一个 BufferMetadata 槽位
     * @param slot 槽位索引
     */
    void free_slot(int32_t slot) {
        if (slot < 0 || slot >= MAX_BUFFERS) {
            return;
        }
        
        scoped_lock<interprocess_mutex> lock(table_mutex);
        
        // 标记为无效
        entries[slot].valid.store(false, std::memory_order_release);
        
        // 加入空闲链表
        int32_t old_head = free_head.load(std::memory_order_acquire);
        next_free[slot] = old_head;
        free_head.store(slot, std::memory_order_release);
        
        allocated_count.fetch_sub(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 根据 Buffer ID 查找槽位
     * @return 槽位索引，-1 表示未找到
     */
    int32_t find_slot_by_id(uint64_t buffer_id) const {
        // 线性搜索（对于 4096 个条目，性能可接受）
        // 未来可以优化为哈希表
        for (size_t i = 0; i < MAX_BUFFERS; ++i) {
            if (entries[i].valid.load(std::memory_order_acquire) &&
                entries[i].buffer_id == buffer_id) {
                return i;
            }
        }
        return -1;
    }
};
```

---

## 3. Buffer 分配流程（多进程视角）

### 3.1 分配流程

```cpp
/**
 * @brief 全局 Buffer 分配器（操作共享内存）
 */
class SharedBufferAllocator {
public:
    /**
     * @brief 构造函数（每个进程调用）
     * @param registry 全局注册表（共享内存中）
     */
    SharedBufferAllocator(GlobalRegistry* registry)
        : registry_(registry)
        , process_slot_(-1)
    {
        // 映射所有 Buffer 池到进程地址空间
        for (size_t i = 0; i < registry_->buffer_pool_registry.pool_count; ++i) {
            auto& pool_info = registry_->buffer_pool_registry.pools[i];
            
            // 打开共享内存
            auto shm = boost::interprocess::shared_memory_object(
                boost::interprocess::open_only,
                pool_info.shm_name,
                boost::interprocess::read_write
            );
            
            // 映射到进程地址空间
            auto region = boost::interprocess::mapped_region(
                shm,
                boost::interprocess::read_write
            );
            
            // 保存映射信息（进程本地）
            pool_mappings_[i] = {
                region.get_address(),  // 基地址（进程本地指针）
                region.get_size()
            };
        }
    }
    
    /**
     * @brief 分配 Buffer
     * @param size 所需大小
     * @return Buffer ID，0 表示失败
     */
    uint64_t allocate(size_t size) {
        // 1. 选择合适的池
        uint32_t pool_id = select_pool(size);
        if (pool_id == INVALID_POOL_ID) {
            return 0;
        }
        
        auto& pool_info = registry_->buffer_pool_registry.pools[pool_id];
        
        // 2. 从池中分配一个块
        int32_t block_index = allocate_from_pool(pool_id);
        if (block_index < 0) {
            return 0;
        }
        
        // 3. 在 BufferMetadata 表中分配槽位
        int32_t meta_slot = registry_->buffer_metadata_table.allocate_slot();
        if (meta_slot < 0) {
            // 回收池中的块
            free_to_pool(pool_id, block_index);
            return 0;
        }
        
        // 4. 初始化 BufferMetadata
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        meta.pool_id = pool_id;
        meta.block_index = block_index;
        meta.size = size;
        meta.ref_count.store(1, std::memory_order_release);  // 初始引用计数为 1
        meta.data_shm_offset = calculate_data_offset(pool_id, block_index);
        meta.creator_process_slot = process_slot_;
        meta.alloc_time_ns = get_timestamp_ns();
        meta.valid.store(true, std::memory_order_release);
        
        return meta.buffer_id;
    }
    
    /**
     * @brief 释放 Buffer（引用计数归零时调用）
     * @param buffer_id Buffer ID
     */
    void deallocate(uint64_t buffer_id) {
        // 1. 查找 BufferMetadata
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return;
        }
        
        BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        
        // 2. 检查引用计数（双重检查）
        uint32_t ref = meta.ref_count.load(std::memory_order_acquire);
        if (ref > 0) {
            // 引用计数不为 0，不应释放
            return;
        }
        
        // 3. 回收池中的块
        free_to_pool(meta.pool_id, meta.block_index);
        
        // 4. 释放 BufferMetadata 槽位
        registry_->buffer_metadata_table.free_slot(meta_slot);
    }
    
    /**
     * @brief 获取 Buffer 数据指针（进程本地）
     * @param buffer_id Buffer ID
     * @return 数据指针，nullptr 表示失败
     */
    void* get_buffer_data(uint64_t buffer_id) {
        // 1. 查找 BufferMetadata
        int32_t meta_slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id);
        if (meta_slot < 0) {
            return nullptr;
        }
        
        const BufferMetadata& meta = registry_->buffer_metadata_table.entries[meta_slot];
        
        if (!meta.valid.load(std::memory_order_acquire)) {
            return nullptr;
        }
        
        // 2. 获取池的映射基地址（进程本地）
        void* pool_base = pool_mappings_[meta.pool_id].base_addr;
        
        // 3. 计算数据指针
        void* data_ptr = static_cast<char*>(pool_base) + meta.data_shm_offset;
        
        return data_ptr;
    }
    
private:
    GlobalRegistry* registry_;  // 共享内存中的全局注册表
    int32_t process_slot_;      // 当前进程的槽位
    
    // 进程本地的池映射信息
    struct PoolMapping {
        void* base_addr;  // 进程本地基地址
        size_t size;
    };
    std::unordered_map<uint32_t, PoolMapping> pool_mappings_;
    
    // ... 辅助方法 ...
};
```

### 3.2 引用计数操作（多进程安全）

```cpp
/**
 * @brief 增加 Buffer 引用计数
 * 
 * 此函数可以被任意进程调用，是多进程安全的
 */
void add_buffer_ref(GlobalRegistry* registry, uint64_t buffer_id) {
    int32_t meta_slot = registry->buffer_metadata_table.find_slot_by_id(buffer_id);
    if (meta_slot < 0) {
        return;
    }
    
    BufferMetadata& meta = registry->buffer_metadata_table.entries[meta_slot];
    
    // 原子操作，多进程安全
    uint32_t old_ref = meta.add_ref();
    
    // 日志（可选）
    LOG_TRACE("Buffer {} ref_count: {} -> {}", buffer_id, old_ref, old_ref + 1);
}

/**
 * @brief 减少 Buffer 引用计数
 * 
 * 此函数可以被任意进程调用，是多进程安全的
 * 
 * @return 如果引用计数归零，返回 true
 */
bool remove_buffer_ref(GlobalRegistry* registry, uint64_t buffer_id) {
    int32_t meta_slot = registry->buffer_metadata_table.find_slot_by_id(buffer_id);
    if (meta_slot < 0) {
        return false;
    }
    
    BufferMetadata& meta = registry->buffer_metadata_table.entries[meta_slot];
    
    // 原子操作，多进程安全
    uint32_t old_ref = meta.remove_ref();
    
    LOG_TRACE("Buffer {} ref_count: {} -> {}", buffer_id, old_ref, old_ref - 1);
    
    if (old_ref == 1) {
        // 引用计数归零，需要回收
        return true;
    }
    
    return false;
}
```

---

## 4. BufferPtr 设计（进程本地包装）

### 4.1 BufferPtr 类

```cpp
/**
 * @brief Buffer 指针（进程本地的轻量级包装）
 * 
 * 注意：
 * 1. BufferPtr 是进程本地的对象，不存储在共享内存中
 * 2. 它只是共享内存中 BufferMetadata 的包装器
 * 3. 可以在进程内自由拷贝和移动
 * 4. 析构时自动减少引用计数
 */
class BufferPtr {
public:
    /**
     * @brief 默认构造函数（空 Buffer）
     */
    BufferPtr()
        : buffer_id_(0)
        , registry_(nullptr)
        , allocator_(nullptr)
        , data_(nullptr)
    {}
    
    /**
     * @brief 构造函数
     * @param buffer_id Buffer ID
     * @param registry 全局注册表
     * @param allocator Buffer 分配器
     */
    BufferPtr(uint64_t buffer_id,
              GlobalRegistry* registry,
              SharedBufferAllocator* allocator)
        : buffer_id_(buffer_id)
        , registry_(registry)
        , allocator_(allocator)
    {
        if (buffer_id_ > 0) {
            // 增加引用计数
            add_buffer_ref(registry_, buffer_id_);
            
            // 获取数据指针（进程本地）
            data_ = allocator_->get_buffer_data(buffer_id_);
        } else {
            data_ = nullptr;
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~BufferPtr() {
        release();
    }
    
    /**
     * @brief 拷贝构造函数（增加引用计数）
     */
    BufferPtr(const BufferPtr& other)
        : buffer_id_(other.buffer_id_)
        , registry_(other.registry_)
        , allocator_(other.allocator_)
        , data_(other.data_)
    {
        if (buffer_id_ > 0) {
            add_buffer_ref(registry_, buffer_id_);
        }
    }
    
    /**
     * @brief 拷贝赋值运算符
     */
    BufferPtr& operator=(const BufferPtr& other) {
        if (this != &other) {
            release();
            
            buffer_id_ = other.buffer_id_;
            registry_ = other.registry_;
            allocator_ = other.allocator_;
            data_ = other.data_;
            
            if (buffer_id_ > 0) {
                add_buffer_ref(registry_, buffer_id_);
            }
        }
        return *this;
    }
    
    /**
     * @brief 移动构造函数（不改变引用计数）
     */
    BufferPtr(BufferPtr&& other) noexcept
        : buffer_id_(other.buffer_id_)
        , registry_(other.registry_)
        , allocator_(other.allocator_)
        , data_(other.data_)
    {
        other.buffer_id_ = 0;
        other.registry_ = nullptr;
        other.allocator_ = nullptr;
        other.data_ = nullptr;
    }
    
    /**
     * @brief 移动赋值运算符
     */
    BufferPtr& operator=(BufferPtr&& other) noexcept {
        if (this != &other) {
            release();
            
            buffer_id_ = other.buffer_id_;
            registry_ = other.registry_;
            allocator_ = other.allocator_;
            data_ = other.data_;
            
            other.buffer_id_ = 0;
            other.registry_ = nullptr;
            other.allocator_ = nullptr;
            other.data_ = nullptr;
        }
        return *this;
    }
    
    // ===== 数据访问 =====
    
    void* data() { return data_; }
    const void* data() const { return data_; }
    
    template<typename T>
    T* as() { return static_cast<T*>(data_); }
    
    template<typename T>
    const T* as() const { return static_cast<const T*>(data_); }
    
    size_t size() const {
        if (buffer_id_ == 0) return 0;
        
        int32_t slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id_);
        if (slot < 0) return 0;
        
        return registry_->buffer_metadata_table.entries[slot].size;
    }
    
    uint64_t id() const { return buffer_id_; }
    
    bool valid() const { return buffer_id_ > 0 && data_ != nullptr; }
    
    // ===== 时间戳访问 =====
    
    Timestamp timestamp() const {
        if (buffer_id_ == 0) return Timestamp{};
        
        int32_t slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id_);
        if (slot < 0) return Timestamp{};
        
        return registry_->buffer_metadata_table.entries[slot].timestamp;
    }
    
    void set_timestamp(const Timestamp& ts) {
        if (buffer_id_ == 0) return;
        
        int32_t slot = registry_->buffer_metadata_table.find_slot_by_id(buffer_id_);
        if (slot < 0) return;
        
        registry_->buffer_metadata_table.entries[slot].timestamp = ts;
    }
    
private:
    void release() {
        if (buffer_id_ > 0 && registry_) {
            bool should_free = remove_buffer_ref(registry_, buffer_id_);
            
            if (should_free) {
                // 引用计数归零，回收 Buffer
                allocator_->deallocate(buffer_id_);
            }
        }
        
        buffer_id_ = 0;
        registry_ = nullptr;
        allocator_ = nullptr;
        data_ = nullptr;
    }
    
    uint64_t buffer_id_;                  // Buffer ID（共享的）
    GlobalRegistry* registry_;            // 全局注册表（共享内存）
    SharedBufferAllocator* allocator_;    // 分配器（进程本地）
    void* data_;                          // 数据指针（进程本地）
};
```

---

## 5. 跨进程 Buffer 传递

### 5.1 传递机制

**关键点**：
- **不传递指针**：只传递 Buffer ID（uint64_t）
- **接收进程重建 BufferPtr**：根据 Buffer ID 查找共享内存中的 BufferMetadata

```cpp
// ===== 发送进程（Process A）=====

void send_buffer_to_port(const std::string& port_name, BufferPtr buffer) {
    // 1. 获取端口队列（在共享内存中）
    PortQueue* queue = get_port_queue(port_name);
    
    // 2. 只传递 Buffer ID（不是指针！）
    uint64_t buffer_id = buffer.id();
    
    // 3. 推送到队列
    queue->push(buffer_id, timeout_ms);
    
    // 注意：buffer 超出作用域时，引用计数不会归零
    // 因为接收进程会增加引用计数
}

// ===== 接收进程（Process B）=====

BufferPtr receive_buffer_from_port(const std::string& port_name, uint32_t timeout_ms) {
    // 1. 获取端口队列（在共享内存中）
    PortQueue* queue = get_port_queue(port_name);
    
    // 2. 从队列弹出 Buffer ID
    uint64_t buffer_id;
    if (!queue->pop(buffer_id, timeout_ms)) {
        return BufferPtr();  // 超时或失败
    }
    
    // 3. 根据 Buffer ID 创建 BufferPtr
    // 注意：构造 BufferPtr 时会自动增加引用计数
    BufferPtr buffer(buffer_id, registry_, allocator_);
    
    return buffer;
}
```

### 5.2 完整的跨进程数据流

```
Process 1 (Producer)                   Shared Memory                   Process 2 (Consumer)
─────────────────────                 ──────────────                  ─────────────────────

1. 分配 Buffer
   buffer_id = allocate(4096)
   
                                → BufferMetadata Table
                                  entries[123]:
                                    buffer_id = 1001
                                    ref_count = 1  ← 初始

2. 写入数据
   memcpy(buffer->data(), ...)
   
                                → Buffer Pool
                                  Block #45: [data...]

3. 发送到端口
   produce_output("out", buffer)
   
                                → PortQueue
                                  push(buffer_id = 1001)

4. buffer 超出作用域
   ~BufferPtr()
   ref_count-- (1 → 0?)
   
                                → BufferMetadata Table
                                  entries[123]:
                                    ref_count = 1  ← 队列持有引用
                                    
                                                                       5. 从端口接收
                                                                          buffer_id = pop()
                                                                          buffer_id = 1001
                                                                       
                                → BufferMetadata Table                 6. 创建 BufferPtr
                                  entries[123]:                           BufferPtr(1001, ...)
                                    ref_count = 1                         ref_count++
                                    
                                → BufferMetadata Table
                                  entries[123]:
                                    ref_count = 2  ← 进程2 持有
                                    
                                                                       7. 读取数据
                                                                          data = buffer->data()
                                                                          
                                                                       8. 处理完成
                                                                          ~BufferPtr()
                                                                          ref_count--
                                                                       
                                → BufferMetadata Table
                                  entries[123]:
                                    ref_count = 1
                                    
                                                                       9. 队列清理
                                                                          (从队列移除)
                                                                          ref_count--
                                                                       
                                → BufferMetadata Table
                                  entries[123]:
                                    ref_count = 0  ← 归零！
                                    
10. 后台回收线程
    scan_zero_ref_buffers()
    deallocate(1001)
    
                                → BufferMetadata Table
                                  entries[123]:
                                    valid = false
                                    
                                → Buffer Pool
                                  Block #45: freed
```

---

## 6. 单进程多线程作为特例

### 6.1 为什么单进程自动支持？

**因为**：
1. 共享内存在同一进程内也是"共享"的
2. 原子操作在线程间和进程间都是安全的
3. interprocess_mutex 在进程内也可以用（只是开销略大）

### 6.2 单进程优化（可选）

```cpp
/**
 * @brief 检测是否是单进程模式
 */
bool is_single_process_mode(GlobalRegistry* registry) {
    return registry->process_registry.process_count <= 1;
}

/**
 * @brief 单进程模式下的优化
 */
class SingleProcessOptimization {
    // 可选：使用 std::mutex 替代 interprocess_mutex（更快）
    // 可选：使用进程内的 Buffer 指针缓存
    // 可选：跳过某些跨进程检查
};
```

---

## 7. 进程退出清理

### 7.1 正常退出

```cpp
/**
 * @brief 进程退出时的清理
 */
void cleanup_on_exit(GlobalRegistry* registry, int32_t process_slot) {
    // 1. 遍历所有 BufferMetadata
    for (size_t i = 0; i < BufferMetadataTable::MAX_BUFFERS; ++i) {
        BufferMetadata& meta = registry->buffer_metadata_table.entries[i];
        
        if (!meta.valid.load(std::memory_order_acquire)) {
            continue;
        }
        
        // 2. 检查是否是本进程创建的
        if (meta.creator_process_slot == process_slot) {
            // 本进程创建的 Buffer
            
            // 假设本进程持有 1 个引用
            uint32_t old_ref = meta.remove_ref();
            
            if (old_ref == 1) {
                // 引用计数归零，回收
                deallocate_buffer(registry, meta.buffer_id);
            }
        }
    }
    
    // 3. 注销进程
    registry->process_registry.unregister_process(process_slot);
}
```

### 7.2 异常退出（僵尸进程清理）

```cpp
/**
 * @brief 清理僵尸进程的 Buffer
 */
void cleanup_dead_process_buffers(GlobalRegistry* registry, int32_t dead_process_slot) {
    LOG_WARN("Cleaning up buffers from dead process {}", dead_process_slot);
    
    // 1. 遍历所有 BufferMetadata
    for (size_t i = 0; i < BufferMetadataTable::MAX_BUFFERS; ++i) {
        BufferMetadata& meta = registry->buffer_metadata_table.entries[i];
        
        if (!meta.valid.load(std::memory_order_acquire)) {
            continue;
        }
        
        // 2. 检查是否是死进程创建的
        if (meta.creator_process_slot == dead_process_slot) {
            // 强制减少引用计数
            uint32_t old_ref = meta.remove_ref();
            
            LOG_DEBUG("Buffer {} ref_count: {} -> {}", 
                     meta.buffer_id, old_ref, old_ref - 1);
            
            if (old_ref == 1) {
                // 引用计数归零，回收
                deallocate_buffer(registry, meta.buffer_id);
            }
        }
    }
}
```

---

## 8. 总结

### 8.1 关键设计点

| 设计点 | 说明 | 多进程安全 |
|--------|------|-----------|
| **BufferMetadata 在共享内存** | 所有状态在共享内存 | ✅ |
| **引用计数使用 std::atomic** | 跨进程原子操作 | ✅ |
| **只传递 Buffer ID** | 不传递指针 | ✅ |
| **数据使用相对偏移** | 不使用绝对指针 | ✅ |
| **interprocess_mutex** | 进程间互斥锁 | ✅ |
| **BufferPtr 是进程本地包装** | 轻量级包装器 | ✅ |
| **心跳 + 清理机制** | 处理进程崩溃 | ✅ |

### 8.2 单进程多线程作为特例

```
多进程设计（核心） ──→ 自动支持 ──→ 单进程多线程（特例）
                                      ↓
                                   （可选优化）
```

### 8.3 性能考虑

1. **原子操作开销**：std::atomic 是无锁的，开销很小
2. **查找开销**：线性查找 4096 个条目，可优化为哈希表
3. **进程间锁**：interprocess_mutex 比 std::mutex 慢，但必须用于跨进程
4. **单进程优化**：检测单进程模式，使用更快的 std::mutex

---

**这个设计严格以多进程为核心，单进程多线程自然支持！** ✅

