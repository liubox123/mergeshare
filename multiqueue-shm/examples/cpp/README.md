# C++ 使用示例

本目录包含 multiqueue-shm 库的 C++ 使用示例。

## 示例列表

### 1. basic_usage.cpp
最基本的单进程使用示例，演示如何创建队列、写入和读取数据。

**运行**:
```bash
./basic_usage
```

### 2. multi_process.cpp
多进程示例，演示生产者和消费者在不同进程中的使用。

**运行**:
```bash
# 终端 1: 启动消费者
./multi_process consumer

# 终端 2: 启动生产者
./multi_process producer
```

### 3. multi_producer_consumer.cpp
多生产者多消费者示例，演示并发场景下的使用。

**运行**:
```bash
./multi_producer_consumer
```

### 4. timestamp_sync.cpp
多队列时间戳同步示例，演示如何合并多个队列。

**运行**:
```bash
./timestamp_sync
```

### 5. async_wrapper.cpp
异步线程模式示例，演示如何使用独立线程消费数据。

**运行**:
```bash
./async_wrapper
```

### 6. performance_benchmark.cpp
性能测试示例，测试吞吐量和延迟。

**运行**:
```bash
./performance_benchmark
```

## 编译示例

所有示例程序在构建项目时自动编译（如果启用了 `BUILD_EXAMPLES` 选项）。

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON
cmake --build .
```

编译后的示例程序位于 `build/examples/cpp/` 目录。

## 注意事项

1. 多进程示例需要在不同的终端窗口中运行
2. 某些示例可能需要 root 权限（用于创建共享内存）
3. 运行前确保清理旧的共享内存段（使用 `ipcs` 和 `ipcrm` 命令）

## 清理共享内存

Linux/macOS:
```bash
# 查看共享内存
ipcs -m

# 删除指定的共享内存段
ipcrm -m <shmid>

# 或使用库提供的清理工具
./cleanup_shm
```

Windows:
共享内存在所有进程关闭后自动清理。

---

**维护者**: 开发者


