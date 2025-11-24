# Tracy Profiler 集成

## 概述

Tracy Profiler 是一个实时性能分析工具，可以帮助你分析和优化代码性能。

本项目集成了 Tracy Profiler，通过简单的宏定义即可在代码中添加性能监控点。

## 安装 Tracy

### 方式 1: 从源码构建

```bash
# 克隆 Tracy 仓库
git clone https://github.com/wolfpld/tracy.git
cd tracy

# 构建 Tracy Server (GUI 工具)
cd profiler/build/unix
make release

# Tracy Client 是 header-only，只需要包含头文件
```

### 方式 2: 使用包管理器

**macOS (Homebrew)**:
```bash
brew install tracy
```

**Ubuntu/Debian**:
```bash
# Tracy 通常需要从源码编译
```

### 方式 3: 集成到项目

将 Tracy 头文件复制到项目的 `third_party/tracy/` 目录：

```bash
cd multiqueue-shm
mkdir -p third_party/tracy
cp -r /path/to/tracy/public third_party/tracy/
```

## 启用 Tracy

### 编译时启用

```bash
mkdir build && cd build
cmake .. -DENABLE_TRACY=ON
cmake --build .
```

### CMake 选项

- `-DENABLE_TRACY=ON`: 启用 Tracy Profiler
- `-DENABLE_TRACY=OFF`: 禁用 Tracy Profiler (默认)

## 使用示例

### 1. 函数性能监控

```cpp
#include <tracy_wrapper.hpp>

void my_function() {
    MQ_TRACE_FUNC;  // 自动使用函数名作为监控区域名称
    
    // 函数代码...
}
```

### 2. 自定义监控区域

```cpp
void complex_operation() {
    MQ_TRACE_FUNC_N("ComplexOperation");
    
    // 操作代码...
}
```

### 3. 作用域监控

```cpp
void process_data() {
    // 数据加载
    {
        MQ_TRACE_SCOPE("LoadData");
        load_data();
    }
    
    // 数据处理
    {
        MQ_TRACE_SCOPE("ProcessData");
        process();
    }
    
    // 数据保存
    {
        MQ_TRACE_SCOPE("SaveData");
        save_data();
    }
}
```

### 4. 性能图表

```cpp
void update_metrics() {
    size_t queue_size = get_queue_size();
    double throughput = calculate_throughput();
    
    // 绘制队列大小图表
    MQ_TRACE_PLOT("QueueSize", queue_size);
    
    // 绘制吞吐量图表
    MQ_TRACE_PLOT("Throughput", throughput);
}
```

### 5. 使用 C++ 包装器

```cpp
#include <tracy_wrapper.hpp>

void my_function() {
    using namespace multiqueue::profiler;
    
    // RAII 作用域监控
    ScopedProfiler profiler("MyFunction");
    
    // 绘制图表
    Profiler::plot("Latency", 0.001);
    
    // 发送消息
    Profiler::message("Processing started");
    
    // 处理代码...
    
    Profiler::message("Processing completed");
}
```

### 6. 队列性能监控示例

```cpp
#include <multiqueue/ring_queue.hpp>
#include <tracy_wrapper.hpp>

template<typename T>
class MonitoredRingQueue : public RingQueue<T> {
public:
    bool push(const T& data, uint64_t timestamp = 0) override {
        MQ_TRACE_FUNC;
        
        bool result = RingQueue<T>::push(data, timestamp);
        
        // 监控队列大小
        MQ_TRACE_PLOT("QueueSize", this->size());
        
        return result;
    }
    
    bool pop(T& data, uint64_t* timestamp = nullptr) override {
        MQ_TRACE_FUNC;
        
        bool result = RingQueue<T>::pop(data, timestamp);
        
        // 监控队列大小
        MQ_TRACE_PLOT("QueueSize", this->size());
        
        return result;
    }
};
```

## 运行 Tracy Server

### 启动 Tracy GUI

```bash
# Linux/macOS
./tracy-profiler

# Windows
tracy.exe
```

### 连接到应用程序

1. 启动 Tracy Server (GUI 工具)
2. 运行你的应用程序（已启用 Tracy）
3. Tracy 会自动检测并连接到你的程序
4. 开始查看实时性能数据

## Tracy Server 界面

Tracy Server 提供以下功能：

- **时间线视图**: 查看函数调用的时间线
- **火焰图**: 查看函数调用关系和耗时
- **统计信息**: 查看函数调用次数、总耗时、平均耗时等
- **图表**: 查看自定义的性能图表
- **内存分析**: 查看内存分配和释放
- **帧率分析**: 查看帧率和帧时间

## 性能开销

Tracy 的性能开销非常低：

- 单次函数调用监控：~10-30 纳秒
- 图表绘制：~20-40 纳秒
- 消息发送：~50-100 纳秒

总体开销通常 < 1%，可以在生产环境中启用。

## 禁用 Tracy

如果不需要 Tracy，可以完全禁用：

```bash
cmake .. -DENABLE_TRACY=OFF
cmake --build .
```

或者不使用 `-DENABLE_TRACY` 选项（默认禁用）。

禁用后，所有 Tracy 宏会被定义为空，不会有任何性能开销。

## 高级功能

### 1. 锁性能监控

```cpp
#include <tracy_wrapper.hpp>
#include <mutex>

// 监控锁性能
MQ_TRACE_LOCKABLE(std::mutex, my_mutex);

void thread_safe_operation() {
    std::lock_guard<decltype(my_mutex)> lock(my_mutex);
    MQ_TRACE_LOCK_MARK(my_mutex);
    
    // 临界区代码...
}
```

### 2. 内存分配监控

```cpp
void allocate_memory() {
    void* ptr = malloc(1024);
    MQ_TRACE_ALLOC(ptr, 1024);
    
    // 使用内存...
    
    MQ_TRACE_FREE(ptr);
    free(ptr);
}
```

### 3. 帧标记（用于游戏或实时系统）

```cpp
void main_loop() {
    while (running) {
        MQ_TRACE_FRAME_MARK;  // 标记新的一帧
        
        // 帧逻辑...
    }
}
```

## 故障排除

### Tracy Server 无法连接

1. 检查防火墙设置
2. 确保应用程序已启用 Tracy (`-DENABLE_TRACY=ON`)
3. 检查 Tracy Server 是否正在运行

### 编译错误

1. 确保 Tracy 头文件已安装
2. 检查 CMake 配置输出，查看 Tracy 是否找到
3. 手动设置 `TRACY_INCLUDE_DIR`

### 性能数据不准确

1. 使用 Release 构建获取准确的性能数据
2. 确保编译器优化已启用 (`-O3`)
3. 避免在 Debug 构建中分析性能

## 参考资源

- Tracy 官方网站: https://github.com/wolfpld/tracy
- Tracy 文档: https://github.com/wolfpld/tracy/releases
- Tracy 教程: https://www.youtube.com/watch?v=fB5B46lbapc

---

**维护者**: 开发者  
**最后更新**: 2025-11-18


