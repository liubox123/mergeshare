# MultiQueue-SHM 详细设计文档 v1.0 (Part 2)

> 续接 DETAILED_DESIGN.md

---

## 7. 线程模型

### 7.1 整体线程架构

```
┌────────────────────────────────────────────────────────────────┐
│                        Main Thread                              │
│  - Runtime 管理                                                 │
│  - Block 注册/注销                                              │
│  - 连接管理                                                     │
└────────────────────────────────────────────────────────────────┘
                               │
                               ├─────────────┐
                               │             │
        ┌──────────────────────┴──┐    ┌─────┴──────────────┐
        │  Scheduler Worker Pool  │    │  MsgBus Dispatch   │
        │  (4-16 threads)         │    │  (1 thread)        │
        └──────────────────────────┘    └────────────────────┘
                   │
        ┌──────────┼──────────┐
        │          │          │
   ┌────▼─┐   ┌───▼──┐   ┌──▼───┐
   │Thread│   │Thread│   │Thread│
   │  0   │   │  1   │   │  N   │
   └──────┘   └──────┘   └──────┘
        │          │          │
        └──────────┼──────────┘
                   │
          Execute Block::work()
```

### 7.2 Scheduler Worker Thread 流程

```cpp
void Scheduler::worker_thread_func(size_t thread_id) {
    while (running_.load(std::memory_order_acquire)) {
        Task task;
        
        // 1. 从任务队列获取任务
        if (!task_queue_->try_pop(task)) {
            // 队列空，短暂休眠
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // 2. 获取 Block
        Block* block = Runtime::instance().get_block(task.block_id);
        if (!block) {
            continue;  // Block 已被删除
        }
        
        // 3. 执行 Block::work()
        auto start_time = std::chrono::high_resolution_clock::now();
        WorkResult result = block->work();
        auto end_time = std::chrono::high_resolution_clock::now();
        
        // 4. 记录统计信息
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time
        ).count();
        update_stats(thread_id, task.block_id, duration);
        
        // 5. 根据结果决定是否重新调度
        switch (result) {
            case WorkResult::OK:
                // 立即重新调度
                schedule_block(task.block_id);
                break;
                
            case WorkResult::NEED_MORE_INPUT:
                // 短暂延迟后重新调度
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                schedule_block(task.block_id);
                break;
                
            case WorkResult::OUTPUT_FULL:
                // 稍长延迟后重新调度
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                schedule_block(task.block_id);
                break;
                
            case WorkResult::DONE:
                // 不再调度（Block 完成）
                break;
                
            case WorkResult::ERROR:
                // 记录错误，停止调度
                log_error(task.block_id);
                break;
        }
    }
}
```

### 7.3 线程同步机制

#### 7.3.1 锁的使用原则

1. **最小化锁持有时间**
2. **避免嵌套锁**（防止死锁）
3. **优先使用读写锁**（`std::shared_mutex`）
4. **使用无锁数据结构**（如 `std::atomic`）

#### 7.3.2 关键锁

```cpp
// Runtime
std::shared_mutex blocks_mutex_;          // Block 注册表锁（读多写少）

// Scheduler
std::shared_mutex port_queues_mutex_;     // 端口队列锁（读多写少）
std::shared_mutex routing_mutex_;         // 路由表锁（读多写少）

// ShmManager
std::shared_mutex pools_mutex_;           // 池列表锁（读多写少）
std::shared_mutex metadata_mutex_;        // 元数据表锁（读多写少）

// BufferPool
std::mutex free_list_mutex_;              // 空闲列表锁（短暂持有）

// BufferQueue
std::mutex mutex_;                        // 队列锁
std::condition_variable cv_;              // 条件变量

// MsgBus
std::shared_mutex subscriptions_mutex_;   // 订阅表锁（读多写少）
std::shared_mutex handlers_mutex_;        // 处理器锁（读多写少）
```

#### 7.3.3 死锁预防

**锁顺序规则**（如果必须持有多个锁）：
1. Runtime locks
2. Scheduler locks
3. ShmManager locks
4. BufferPool locks

**示例**：
```cpp
// 正确：按顺序获取锁
std::shared_lock runtime_lock(runtime.blocks_mutex_);
std::shared_lock scheduler_lock(scheduler.routing_mutex_);

// 错误：逆序获取锁（可能死锁）
std::shared_lock scheduler_lock(scheduler.routing_mutex_);
std::shared_lock runtime_lock(runtime.blocks_mutex_);  // WRONG!
```

---

## 8. 内存布局

### 8.1 进程内存 vs 共享内存

```
┌─────────────────────────────────────────────────────────────┐
│  Process Memory (每个进程独立)                               │
├─────────────────────────────────────────────────────────────┤
│  Runtime 对象                                                │
│  ├─ Scheduler                                                │
│  ├─ ShmManager                                               │
│  ├─ MsgBus                                                   │
│  └─ FlowGraph                                                │
│                                                              │
│  Block 对象 (unique_ptr)                                     │
│  ├─ FileSourceBlock                                          │
│  ├─ AmplifierBlock                                           │
│  └─ FileSinkBlock                                            │
│                                                              │
│  BufferMetadata (unordered_map)                              │
│  ├─ BufferId → BufferMetadata                                │
│  └─ 引用计数、数据指针等                                      │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ 映射 (mmap)
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  Shared Memory (所有进程共享)                                 │
├─────────────────────────────────────────────────────────────┤
│  BufferPool "small" (mqshm_pool_small)                       │
│  ├─ managed_shared_memory header                             │
│  ├─ Buffer Block #0 (4096 bytes user data)                   │
│  ├─ Buffer Block #1 (4096 bytes user data)                   │
│  └─ ... (1024 blocks)                                        │
│                                                              │
│  BufferPool "medium" (mqshm_pool_medium)                     │
│  ├─ managed_shared_memory header                             │
│  ├─ Buffer Block #0 (65536 bytes user data)                  │
│  └─ ... (512 blocks)                                         │
│                                                              │
│  BufferPool "large" (mqshm_pool_large)                       │
│  ├─ managed_shared_memory header                             │
│  ├─ Buffer Block #0 (1048576 bytes user data)                │
│  └─ ... (128 blocks)                                         │
└─────────────────────────────────────────────────────────────┘
```

### 8.2 BufferMetadata 存储位置

**设计选择：进程内存**

理由：
1. **性能**：避免共享内存的同步开销
2. **灵活性**：可以使用 C++ 标准库容器
3. **引用计数**：每个进程独立管理引用计数
4. **简化设计**：不需要在共享内存中实现复杂的数据结构

**数据指针指向共享内存**：
```cpp
struct BufferMetadata {
    BufferId id;
    void* data;  // ← 指向共享内存中的 Buffer Block
    size_t size;
    std::atomic<uint32_t> ref_count;
    // ...
};
```

### 8.3 跨进程 Buffer 共享机制

#### 场景 1：单进程多线程
- Buffer 通过智能指针在线程间共享
- 引用计数在进程内存中维护
- **零拷贝**：指针传递

#### 场景 2：多进程
- **当前设计不支持跨进程 Buffer 共享**
- 每个进程独立运行 Runtime
- 如需跨进程通信，使用其他 IPC 机制（如共享内存队列、消息队列等）

**未来扩展**：
如需支持跨进程 Buffer 共享，需要：
1. BufferMetadata 存储在共享内存
2. 引用计数使用 `std::atomic` 在共享内存中
3. 进程间同步机制（如 `interprocess_mutex`）

---

## 9. API 设计

### 9.1 C++ API

#### 9.1.1 基础使用

```cpp
#include <multiqueue/runtime.hpp>
#include <multiqueue/blocks/file_source.hpp>
#include <multiqueue/blocks/amplifier.hpp>
#include <multiqueue/blocks/file_sink.hpp>

int main() {
    using namespace multiqueue;
    
    // 1. 初始化 Runtime
    Runtime& rt = Runtime::instance();
    
    RuntimeConfig config;
    config.scheduler_thread_count = 4;
    config.schedule_policy = SchedulePolicy::WORK_STEALING;
    rt.initialize(config);
    
    // 2. 配置共享内存池
    ShmConfig shm_config = ShmConfig::default_config();
    rt.shm_manager().initialize(shm_config);
    
    // 3. 创建 Block
    auto source = std::make_unique<FileSourceBlock>("input.dat", 4096);
    auto amp = std::make_unique<AmplifierBlock>(2.0f);
    auto sink = std::make_unique<FileSinkBlock>("output.dat");
    
    // 4. 注册 Block
    BlockId src_id = rt.register_block(std::move(source));
    BlockId amp_id = rt.register_block(std::move(amp));
    BlockId sink_id = rt.register_block(std::move(sink));
    
    // 5. 连接 Block
    rt.connect(src_id, "out", amp_id, "in");
    rt.connect(amp_id, "out", sink_id, "in");
    
    // 6. 启动
    rt.start();
    
    // 7. 运行时参数调整
    Block* amp_block = rt.get_block(amp_id);
    amp_block->set_parameter("gain", 3.0f);
    
    // 8. 监控
    auto stats = rt.get_stats();
    std::cout << "Blocks: " << stats.total_blocks << std::endl;
    std::cout << "Buffers in use: " << stats.total_buffers_in_use << std::endl;
    
    // 9. 等待完成
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 10. 停止
    rt.stop();
    rt.shutdown();
    
    return 0;
}
```

#### 9.1.2 自定义 Block

```cpp
#include <multiqueue/block.hpp>

class MyCustomBlock : public Block {
public:
    MyCustomBlock()
        : Block("MyCustomBlock", BlockType::PROCESSING)
    {
        // 定义端口
        add_input_port("in");
        add_output_port("out");
        
        // 定义参数
        set_parameter("threshold", 0.5f);
    }
    
    void initialize() override {
        // 初始化资源
        threshold_ = get_parameter<float>("threshold");
    }
    
    WorkResult work() override {
        // 获取输入
        auto input = get_input_buffer("in", 100);
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 处理数据
        auto output = allocate_output_buffer("out", input->size());
        
        const float* in_data = input->as<float>();
        float* out_data = output->as<float>();
        size_t count = input->size() / sizeof(float);
        
        for (size_t i = 0; i < count; ++i) {
            out_data[i] = in_data[i] > threshold_ ? in_data[i] : 0.0f;
        }
        
        output->set_timestamp(input->timestamp());
        
        // 发布输出
        produce_output("out", output);
        
        return WorkResult::OK;
    }
    
    void handle_message(const Message& msg) override {
        if (msg.type == MessageType::PARAMETER && 
            msg.topic == "parameter.threshold") {
            threshold_ = std::any_cast<float>(msg.payload);
        }
    }
    
private:
    float threshold_;
};

// 使用
Runtime& rt = Runtime::instance();
auto custom = std::make_unique<MyCustomBlock>();
BlockId id = rt.register_block(std::move(custom));
```

#### 9.1.3 消息总线使用

```cpp
// 订阅消息
Runtime& rt = Runtime::instance();

auto sub_id = rt.msg_bus().subscribe("status.*", [](const Message& msg) {
    std::cout << "Status update: " << msg.topic << std::endl;
});

// 发布消息
Message msg{
    MessageType::STATUS,
    "my_block",
    "status.ready",
    std::string("Block is ready")
};
rt.msg_bus().publish("status.ready", msg);

// 取消订阅
rt.msg_bus().unsubscribe(sub_id);

// 请求-响应
rt.msg_bus().register_handler("control.pause", [](const Request& req) {
    // 处理暂停请求
    Response resp;
    resp.success = true;
    resp.payload = std::string("Paused");
    return resp;
});

Request req;
req.endpoint = "control.pause";
auto resp = rt.msg_bus().request("control.pause", req, 1000);
```

### 9.2 Python API

#### 9.2.1 基础 pybind11 绑定

```cpp
// multiqueue_python.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <multiqueue/runtime.hpp>

namespace py = pybind11;
using namespace multiqueue;

PYBIND11_MODULE(multiqueue_shm, m) {
    m.doc() = "MultiQueue Shared Memory Runtime";
    
    // RuntimeConfig
    py::class_<RuntimeConfig>(m, "RuntimeConfig")
        .def(py::init<>())
        .def_readwrite("scheduler_thread_count", &RuntimeConfig::scheduler_thread_count)
        .def_readwrite("schedule_policy", &RuntimeConfig::schedule_policy)
        .def_readwrite("shm_name_prefix", &RuntimeConfig::shm_name_prefix);
    
    // SchedulePolicy
    py::enum_<SchedulePolicy>(m, "SchedulePolicy")
        .value("ROUND_ROBIN", SchedulePolicy::ROUND_ROBIN)
        .value("PRIORITY", SchedulePolicy::PRIORITY)
        .value("WORK_STEALING", SchedulePolicy::WORK_STEALING);
    
    // Runtime
    py::class_<Runtime>(m, "Runtime")
        .def_static("instance", &Runtime::instance, py::return_value_policy::reference)
        .def("initialize", &Runtime::initialize)
        .def("start", &Runtime::start)
        .def("stop", &Runtime::stop)
        .def("shutdown", &Runtime::shutdown)
        .def("is_running", &Runtime::is_running)
        .def("register_block", [](Runtime& self, std::unique_ptr<Block> block) {
            return self.register_block(std::move(block));
        })
        .def("unregister_block", &Runtime::unregister_block)
        .def("get_block", &Runtime::get_block, py::return_value_policy::reference)
        .def("connect", &Runtime::connect)
        .def("disconnect", &Runtime::disconnect)
        .def("get_stats", &Runtime::get_stats);
    
    // Block (抽象基类)
    py::class_<Block>(m, "Block")
        .def("id", &Block::id)
        .def("name", &Block::name)
        .def("type", &Block::type)
        .def("set_parameter", [](Block& self, const std::string& name, py::object value) {
            // 根据类型设置参数
            if (py::isinstance<py::int_>(value)) {
                self.set_parameter(name, value.cast<int>());
            } else if (py::isinstance<py::float_>(value)) {
                self.set_parameter(name, value.cast<float>());
            } else if (py::isinstance<py::str>(value)) {
                self.set_parameter(name, value.cast<std::string>());
            }
        })
        .def("get_parameter", [](Block& self, const std::string& name) -> py::object {
            // 尝试不同类型
            try {
                return py::cast(self.get_parameter<int>(name));
            } catch (...) {}
            try {
                return py::cast(self.get_parameter<float>(name));
            } catch (...) {}
            try {
                return py::cast(self.get_parameter<std::string>(name));
            } catch (...) {}
            return py::none();
        });
    
    // FileSourceBlock
    py::class_<FileSourceBlock, Block>(m, "FileSource")
        .def(py::init<const std::string&, size_t>(),
             py::arg("filename"),
             py::arg("chunk_size") = 4096);
    
    // AmplifierBlock
    py::class_<AmplifierBlock, Block>(m, "Amplifier")
        .def(py::init<float>(), py::arg("gain") = 1.0f);
    
    // FileSinkBlock
    py::class_<FileSinkBlock, Block>(m, "FileSink")
        .def(py::init<const std::string&>(), py::arg("filename"));
    
    // ShmManager
    py::class_<ShmManager>(m, "ShmManager")
        .def("initialize", &ShmManager::initialize)
        .def("shutdown", &ShmManager::shutdown)
        .def("get_stats", &ShmManager::get_stats);
    
    // MsgBus
    py::class_<MsgBus>(m, "MsgBus")
        .def("subscribe", &MsgBus::subscribe)
        .def("unsubscribe", &MsgBus::unsubscribe)
        .def("publish", &MsgBus::publish);
}
```

#### 9.2.2 Python 使用示例

```python
import multiqueue_shm as mq

# 1. 初始化 Runtime
rt = mq.Runtime.instance()

config = mq.RuntimeConfig()
config.scheduler_thread_count = 4
config.schedule_policy = mq.SchedulePolicy.WORK_STEALING

rt.initialize(config)

# 2. 创建 Block
source = mq.FileSource("input.dat", chunk_size=4096)
amp = mq.Amplifier(gain=2.0)
sink = mq.FileSink("output.dat")

# 3. 注册 Block
src_id = rt.register_block(source)
amp_id = rt.register_block(amp)
sink_id = rt.register_block(sink)

# 4. 连接
rt.connect(src_id, "out", amp_id, "in")
rt.connect(amp_id, "out", sink_id, "in")

# 5. 启动
rt.start()

# 6. 运行时调整参数
amp_block = rt.get_block(amp_id)
amp_block.set_parameter("gain", 3.0)

# 7. 监控
stats = rt.get_stats()
print(f"Blocks: {stats.total_blocks}")
print(f"Buffers in use: {stats.total_buffers_in_use}")

# 8. 停止
import time
time.sleep(10)
rt.stop()
rt.shutdown()
```

#### 9.2.3 Python 自定义 Block（高级）

```python
import multiqueue_shm as mq
import numpy as np

# 方式 1: 继承 Block 基类（需要额外绑定）
class MyPythonBlock(mq.Block):
    def __init__(self, threshold=0.5):
        super().__init__("MyPythonBlock", mq.BlockType.PROCESSING)
        self.threshold = threshold
        self.add_input_port("in")
        self.add_output_port("out")
    
    def work(self):
        # 获取输入
        input_buf = self.get_input_buffer("in", timeout_ms=100)
        if input_buf is None:
            return mq.WorkResult.NEED_MORE_INPUT
        
        # 处理数据
        in_data = np.frombuffer(input_buf.data(), dtype=np.float32)
        out_data = np.where(in_data > self.threshold, in_data, 0.0)
        
        # 分配输出
        output_buf = self.allocate_output_buffer("out", out_data.nbytes)
        np.copyto(np.frombuffer(output_buf.data(), dtype=np.float32), out_data)
        
        # 发布
        self.produce_output("out", output_buf)
        
        return mq.WorkResult.OK

# 使用
rt = mq.Runtime.instance()
custom = MyPythonBlock(threshold=0.5)
block_id = rt.register_block(custom)
```

---

## 10. 性能优化

### 10.1 零拷贝设计

```cpp
// 典型数据流：
// Block A → Buffer → Block B → Buffer → Block C
//
// 数据始终在共享内存中，只传递指针

// Block A 生产
auto buffer = allocate_output_buffer("out", 4096);
// 直接写入共享内存
memcpy(buffer->data(), my_data, 4096);
produce_output("out", buffer);  // 传递智能指针（零拷贝）

// Block B 消费和生产
auto input = get_input_buffer("in");
auto output = allocate_output_buffer("out", input->size());
// 处理数据（在共享内存中操作）
process(input->data(), output->data(), input->size());
produce_output("out", output);  // 传递智能指针（零拷贝）

// Block C 消费
auto input = get_input_buffer("in");
// 使用数据（直接访问共享内存）
consume(input->data(), input->size());
```

### 10.2 引用计数优化

```cpp
// 使用 std::shared_ptr 的自定义删除器
class BufferDeleter {
public:
    BufferDeleter(ShmManager* manager) : manager_(manager) {}
    
    void operator()(Buffer* buffer) {
        if (buffer) {
            // 减少引用计数
            uint32_t ref_count = manager_->remove_ref(buffer->id());
            
            if (ref_count == 0) {
                // 引用计数归零，回收到池中
                manager_->release(buffer);
            }
            
            // 注意：不 delete buffer 本身，因为 Buffer 对象
            // 是栈分配或在池中管理的
        }
    }
    
private:
    ShmManager* manager_;
};

// 创建 BufferPtr
BufferPtr make_buffer_ptr(Buffer* buffer, ShmManager* manager) {
    manager->add_ref(buffer->id());
    return BufferPtr(buffer, BufferDeleter(manager));
}
```

### 10.3 内存池预分配

```cpp
// 初始化时预分配所有 Buffer
BufferPool::BufferPool(const std::string& name,
                      size_t block_size,
                      size_t block_count,
                      bool expandable,
                      size_t max_blocks)
{
    // 计算总大小
    size_t total_size = block_count * block_size;
    
    // 创建共享内存
    shm_ = std::make_unique<managed_shared_memory>(
        create_only,
        ("mqshm_pool_" + name).c_str(),
        total_size
    );
    
    // 预分配所有 Buffer Block
    void* base = shm_->get_address();
    for (size_t i = 0; i < block_count; ++i) {
        void* block_ptr = static_cast<char*>(base) + i * block_size;
        free_list_.push_back(block_ptr);
    }
    
    // 优点：
    // 1. 避免运行时分配的开销
    // 2. 内存布局连续，缓存友好
    // 3. 分配/回收 O(1) 时间复杂度
}
```

### 10.4 工作窃取调度

```cpp
// 每个线程有自己的本地队列
class WorkStealingQueue : public TaskQueue {
public:
    // 本地线程推送（无锁）
    void push_local(const Task& task) {
        size_t t = top_.load(std::memory_order_relaxed);
        deque_[t & mask_] = task;
        top_.store(t + 1, std::memory_order_release);
    }
    
    // 本地线程弹出（无锁）
    bool try_pop_local(Task& task) {
        size_t t = top_.load(std::memory_order_acquire) - 1;
        top_.store(t, std::memory_order_release);
        
        size_t b = bottom_.load(std::memory_order_acquire);
        
        if (t < b) {
            // 队列空
            top_.store(b, std::memory_order_release);
            return false;
        }
        
        task = deque_[t & mask_];
        
        if (t == b) {
            // 最后一个元素，可能有竞争
            if (!bottom_.compare_exchange_strong(b, b + 1,
                                                 std::memory_order_acquire,
                                                 std::memory_order_release)) {
                // 被其他线程偷走了
                top_.store(b + 1, std::memory_order_release);
                return false;
            }
        }
        
        return true;
    }
    
    // 其他线程窃取（有锁）
    bool try_steal(Task& task) {
        size_t b = bottom_.load(std::memory_order_acquire);
        size_t t = top_.load(std::memory_order_acquire);
        
        if (b <= t) {
            // 队列空
            return false;
        }
        
        task = deque_[b & mask_];
        
        if (!bottom_.compare_exchange_strong(b, b + 1,
                                             std::memory_order_acquire,
                                             std::memory_order_release)) {
            // 被其他线程抢先
            return false;
        }
        
        return true;
    }
    
private:
    std::vector<Task> deque_;
    std::atomic<size_t> top_{0};
    std::atomic<size_t> bottom_{0};
    size_t mask_;
};

// Scheduler 使用工作窃取
void Scheduler::worker_thread_func(size_t thread_id) {
    WorkStealingQueue& my_queue = queues_[thread_id];
    
    while (running_) {
        Task task;
        
        // 1. 先从自己的队列取
        if (my_queue.try_pop_local(task)) {
            execute_task(task);
            continue;
        }
        
        // 2. 自己队列空，尝试从其他线程偷
        bool stolen = false;
        for (size_t i = 0; i < queues_.size(); ++i) {
            if (i == thread_id) continue;
            
            if (queues_[i].try_steal(task)) {
                execute_task(task);
                stolen = true;
                break;
            }
        }
        
        if (!stolen) {
            // 3. 所有队列都空，休眠
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}
```

### 10.5 缓存行对齐

```cpp
// 避免伪共享（False Sharing）
struct alignas(64) BufferMetadata {  // 缓存行大小通常是 64 字节
    BufferId id;
    void* data;
    size_t size;
    std::atomic<uint32_t> ref_count;
    uint64_t alloc_timestamp;
    uint64_t data_timestamp;
    std::atomic<bool> valid;
    BufferPool* pool;
    
    // 填充到 64 字节
    char padding[64 - (sizeof(BufferId) + sizeof(void*) + sizeof(size_t) +
                      sizeof(std::atomic<uint32_t>) + sizeof(uint64_t) * 2 +
                      sizeof(std::atomic<bool>) + sizeof(BufferPool*))];
};

// 确保频繁访问的原子变量不在同一缓存行
struct alignas(64) ControlBlock {
    alignas(64) std::atomic<uint32_t> producer_head;
    alignas(64) std::atomic<uint32_t> producer_tail;
    alignas(64) std::atomic<uint32_t> consumer_count;
    // ...
};
```

---

## 11. 错误处理

### 11.1 异常层次

```cpp
namespace multiqueue {

/**
 * @brief 基础异常
 */
class Exception : public std::runtime_error {
public:
    explicit Exception(const std::string& msg)
        : std::runtime_error(msg)
        , error_code_(ErrorCode::UNKNOWN)
    {}
    
    Exception(const std::string& msg, ErrorCode code)
        : std::runtime_error(msg)
        , error_code_(code)
    {}
    
    ErrorCode error_code() const { return error_code_; }
    
private:
    ErrorCode error_code_;
};

/**
 * @brief 错误码
 */
enum class ErrorCode {
    UNKNOWN = 0,
    
    // Runtime 错误
    NOT_INITIALIZED = 1000,
    ALREADY_INITIALIZED = 1001,
    ALREADY_RUNNING = 1002,
    NOT_RUNNING = 1003,
    
    // Block 错误
    BLOCK_NOT_FOUND = 2000,
    BLOCK_ALREADY_REGISTERED = 2001,
    INVALID_BLOCK = 2002,
    
    // 端口错误
    PORT_NOT_FOUND = 3000,
    PORT_ALREADY_EXISTS = 3001,
    PORT_NOT_CONNECTED = 3002,
    PORT_ALREADY_CONNECTED = 3003,
    
    // 连接错误
    CONNECTION_FAILED = 4000,
    INVALID_CONNECTION = 4001,
    CIRCULAR_DEPENDENCY = 4002,
    
    // 共享内存错误
    SHM_ALLOCATION_FAILED = 5000,
    SHM_POOL_FULL = 5001,
    SHM_INVALID_SIZE = 5002,
    
    // 调度错误
    SCHEDULER_ERROR = 6000,
    TASK_QUEUE_FULL = 6001,
    
    // 消息总线错误
    MSGBUS_ERROR = 7000,
    SUBSCRIPTION_FAILED = 7001,
    HANDLER_NOT_FOUND = 7002,
};

/**
 * @brief Runtime 异常
 */
class RuntimeException : public Exception {
    using Exception::Exception;
};

/**
 * @brief Block 异常
 */
class BlockException : public Exception {
    using Exception::Exception;
};

/**
 * @brief 端口异常
 */
class PortException : public Exception {
    using Exception::Exception;
};

/**
 * @brief 连接异常
 */
class ConnectionException : public Exception {
    using Exception::Exception;
};

/**
 * @brief 共享内存异常
 */
class ShmException : public Exception {
    using Exception::Exception;
};

} // namespace multiqueue
```

### 11.2 错误处理策略

```cpp
// 1. 关键操作使用异常
void Runtime::initialize(const RuntimeConfig& config) {
    if (initialized_.load()) {
        throw RuntimeException(
            "Runtime already initialized",
            ErrorCode::ALREADY_INITIALIZED
        );
    }
    
    try {
        init_scheduler(config);
        init_shm_manager(config);
        init_msg_bus(config);
    } catch (const std::exception& e) {
        // 回滚已初始化的部分
        cleanup();
        throw RuntimeException(
            std::string("Initialization failed: ") + e.what(),
            ErrorCode::NOT_INITIALIZED
        );
    }
    
    initialized_.store(true);
}

// 2. 非关键操作返回错误码或 std::optional
std::optional<BufferPtr> Scheduler::try_allocate_buffer(size_t size) {
    try {
        auto buffer = Runtime::instance().shm_manager().allocate(size);
        return buffer;
    } catch (const ShmException& e) {
        // 记录错误但不抛出异常
        log_error("Buffer allocation failed: {}", e.what());
        return std::nullopt;
    }
}

// 3. Block::work() 返回错误状态
WorkResult MyBlock::work() {
    try {
        auto input = get_input_buffer("in", 100);
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 处理数据...
        
        return WorkResult::OK;
    } catch (const std::exception& e) {
        log_error("Block work failed: {}", e.what());
        return WorkResult::ERROR;
    }
}
```

### 11.3 日志系统

```cpp
/**
 * @brief 日志级别
 */
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

/**
 * @brief 日志器
 */
class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }
    
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, 
             const std::string& fmt, Args&&... args) {
        if (level < level_) return;
        
        auto now = std::chrono::system_clock::now();
        auto msg = format(fmt, std::forward<Args>(args)...);
        
        std::lock_guard lock(mutex_);
        
        // 写入日志
        log_file_ << format_timestamp(now) << " "
                  << format_level(level) << " "
                  << "[" << file << ":" << line << "] "
                  << msg << std::endl;
        
        // 同时输出到控制台（ERROR 及以上）
        if (level >= LogLevel::ERROR) {
            std::cerr << msg << std::endl;
        }
    }
    
private:
    LogLevel level_ = LogLevel::INFO;
    std::ofstream log_file_;
    std::mutex mutex_;
};

// 宏定义
#define LOG_TRACE(...) Logger::instance().log(LogLevel::TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::instance().log(LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().log(LogLevel::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().log(LogLevel::WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().log(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) Logger::instance().log(LogLevel::FATAL, __FILE__, __LINE__, __VA_ARGS__)

// 使用
LOG_INFO("Runtime initialized with {} threads", thread_count);
LOG_ERROR("Failed to allocate buffer: {}", error_msg);
```

---

## 12. 跨平台支持

### 12.1 平台宏定义

```cpp
// platform.hpp
#pragma once

// 平台检测
#if defined(_WIN32) || defined(_WIN64)
    #define MULTIQUEUE_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define MULTIQUEUE_PLATFORM_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
    #define MULTIQUEUE_PLATFORM_MACOS
#else
    #error "Unsupported platform"
#endif

// 导出符号
#ifdef MULTIQUEUE_PLATFORM_WINDOWS
    #ifdef MULTIQUEUE_EXPORTS
        #define MULTIQUEUE_API __declspec(dllexport)
    #else
        #define MULTIQUEUE_API __declspec(dllimport)
    #endif
#else
    #define MULTIQUEUE_API __attribute__((visibility("default")))
#endif
```

### 12.2 共享内存实现

```cpp
// Windows
#ifdef MULTIQUEUE_PLATFORM_WINDOWS
    #include <boost/interprocess/windows_shared_memory.hpp>
    using SharedMemory = boost::interprocess::windows_shared_memory;
#else
    #include <boost/interprocess/shared_memory_object.hpp>
    using SharedMemory = boost::interprocess::shared_memory_object;
#endif

// 创建共享内存
SharedMemory create_shm(const std::string& name, size_t size) {
#ifdef MULTIQUEUE_PLATFORM_WINDOWS
    return SharedMemory(
        boost::interprocess::create_only,
        name.c_str(),
        boost::interprocess::read_write,
        size
    );
#else
    SharedMemory shm(
        boost::interprocess::create_only,
        name.c_str(),
        boost::interprocess::read_write
    );
    shm.truncate(size);
    return shm;
#endif
}
```

### 12.3 时间戳获取

```cpp
// 跨平台高精度时间戳
inline uint64_t get_timestamp_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

// 毫秒级时间戳
inline uint64_t get_timestamp_ms() {
    return get_timestamp_ns() / 1000000;
}
```

### 12.4 线程优先级设置

```cpp
void set_thread_priority(std::thread& thread, ThreadPriority priority) {
#ifdef MULTIQUEUE_PLATFORM_WINDOWS
    HANDLE handle = thread.native_handle();
    int win_priority;
    
    switch (priority) {
        case ThreadPriority::LOW:
            win_priority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::NORMAL:
            win_priority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::HIGH:
            win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::REALTIME:
            win_priority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
    }
    
    SetThreadPriority(handle, win_priority);
    
#else  // POSIX
    pthread_t handle = thread.native_handle();
    sched_param param;
    int policy;
    
    pthread_getschedparam(handle, &policy, &param);
    
    switch (priority) {
        case ThreadPriority::LOW:
            param.sched_priority = sched_get_priority_min(policy);
            break;
        case ThreadPriority::NORMAL:
            param.sched_priority = (sched_get_priority_min(policy) + 
                                   sched_get_priority_max(policy)) / 2;
            break;
        case ThreadPriority::HIGH:
            param.sched_priority = sched_get_priority_max(policy);
            break;
        case ThreadPriority::REALTIME:
            policy = SCHED_FIFO;
            param.sched_priority = sched_get_priority_max(SCHED_FIFO);
            break;
    }
    
    pthread_setschedparam(handle, policy, &param);
#endif
}
```

---

## 13. 总结

### 13.1 设计特点

✅ **中心化管理**：Runtime 统一管理资源和调度  
✅ **零拷贝**：数据在共享内存中，只传递指针  
✅ **多消费者**：引用计数自动管理，支持广播  
✅ **动态连接**：运行时修改数据流图  
✅ **模块化**：Block 独立封装，易于扩展  
✅ **高性能**：工作窃取、缓存行对齐、内存池  
✅ **跨平台**：Windows、Linux、macOS  
✅ **易用性**：C++ 和 Python API  

### 13.2 与其他框架对比

| 特性 | MultiQueue-SHM | GNU Radio | GStreamer | Apache Flink |
|------|----------------|-----------|-----------|--------------|
| 中心化管理 | ✅ | ✅ | ✅ | ✅ |
| 共享内存 | ✅ | ❌ | ❌ | ❌ |
| 零拷贝 | ✅ | ❌ | 部分 | ❌ |
| 动态连接 | ✅ | ❌ | 部分 | ✅ |
| Python 支持 | ✅ | ✅ | 部分 | ✅ (PyFlink) |
| 多进程 | ❌ (未来) | ❌ | ✅ | ✅ (分布式) |
| 实时性 | 高 | 中 | 高 | 低 (批处理为主) |

### 13.3 未来扩展

1. **跨进程 Buffer 共享**
   - BufferMetadata 存储在共享内存
   - 进程间引用计数同步

2. **分布式支持**
   - 跨节点的 Block 通信
   - 网络传输优化

3. **GPU 加速**
   - GPU Buffer 池
   - CUDA/OpenCL 集成

4. **可视化工具**
   - 流图编辑器
   - 性能监控面板
   - 实时数据可视化

5. **更多内置 Block**
   - 信号处理（FFT、滤波器等）
   - 图像处理（缩放、转换等）
   - 机器学习（推理 Block）

---

**设计文档完成！请审阅！** 📄

如有任何疑问或需要修改的地方，请告诉我！

