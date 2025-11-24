# 新架构实施计划

**开始日期**: 2025-11-24  
**状态**: 实施中  
**架构版本**: v2.0 (多进程优先)

---

## 📋 实施阶段

### Phase 0: 代码清理 ✅
- [x] 创建实施计划
- [ ] 备份旧代码到 `old_impl/` 目录
- [ ] 清除旧的核心实现
- [ ] 清除旧的测试代码
- [ ] 更新 CMakeLists.txt

### Phase 1: 共享内存基础设施 (2-3天)
- [ ] GlobalRegistry 结构定义
- [ ] ProcessRegistry 实现
- [ ] BlockRegistry 实现
- [ ] ConnectionRegistry 实现
- [ ] BufferPoolRegistry 实现
- [ ] MessageBus 基础结构

### Phase 2: Buffer 管理系统 (2-3天)
- [ ] BufferMetadata 结构
- [ ] BufferMetadataTable 实现
- [ ] BufferPool 实现（多个大小）
- [ ] SharedBufferAllocator 实现
- [ ] BufferPtr 类（进程本地包装）
- [ ] 引用计数测试

### Phase 3: 端口和队列系统 (2天)
- [ ] PortQueue 实现（跨进程）
- [ ] InputPort 类
- [ ] OutputPort 类
- [ ] 端口连接机制

### Phase 4: Block 框架 (2-3天)
- [ ] Block 基类
- [ ] SourceBlock 基类
- [ ] ProcessingBlock 基类
- [ ] SinkBlock 基类
- [ ] 多输入/多输出支持

### Phase 5: 时间戳和同步 (2天)
- [ ] Timestamp 结构
- [ ] TimestampSynchronizer
- [ ] SYNC_MODE 实现
- [ ] ASYNC_MODE 实现
- [ ] 时间戳对齐策略

### Phase 6: Scheduler 调度器 (2-3天)
- [ ] 基础调度器
- [ ] 线程池
- [ ] 工作窃取算法
- [ ] Block 调度策略

### Phase 7: Runtime 核心 (2天)
- [ ] Runtime 单例
- [ ] 初始化流程
- [ ] Block 注册
- [ ] 连接管理
- [ ] 启动/停止控制

### Phase 8: 多进程支持 (2-3天)
- [ ] 心跳机制
- [ ] 僵尸进程检测
- [ ] 资源清理
- [ ] 进程间同步测试

### Phase 9: Python 绑定 (2-3天)
- [ ] pybind11 基础绑定
- [ ] Buffer Python 接口
- [ ] Block Python 基类
- [ ] Runtime Python API
- [ ] NumPy 互操作

### Phase 10: 测试和文档 (2-3天)
- [ ] 单元测试（C++）
- [ ] 多进程集成测试
- [ ] 性能基准测试
- [ ] Python 测试
- [ ] API 文档
- [ ] 示例程序

---

## 🎯 当前阶段

**Phase 0: 代码清理** ⬅️ 当前

---

## 📁 目录结构（新架构）

```
multiqueue-shm/
├── core/
│   ├── include/
│   │   └── multiqueue/
│   │       ├── types.hpp              # 基础类型定义
│   │       ├── timestamp.hpp          # 时间戳
│   │       ├── global_registry.hpp    # 全局注册表
│   │       ├── buffer_metadata.hpp    # Buffer 元数据
│   │       ├── buffer_pool.hpp        # Buffer 池
│   │       ├── buffer_allocator.hpp   # Buffer 分配器
│   │       ├── buffer_ptr.hpp         # BufferPtr 类
│   │       ├── port_queue.hpp         # 端口队列
│   │       ├── port.hpp               # 端口类
│   │       ├── block.hpp              # Block 基类
│   │       ├── scheduler.hpp          # 调度器
│   │       ├── runtime.hpp            # Runtime 核心
│   │       └── multiqueue.hpp         # 统一包含头文件
│   ├── src/
│   │   ├── global_registry.cpp
│   │   ├── buffer_pool.cpp
│   │   ├── buffer_allocator.cpp
│   │   ├── port_queue.cpp
│   │   ├── block.cpp
│   │   ├── scheduler.cpp
│   │   └── runtime.cpp
│   └── CMakeLists.txt
├── blocks/                            # 内置 Block 实现
│   ├── include/
│   │   └── multiqueue/blocks/
│   │       ├── source_block.hpp
│   │       ├── sink_block.hpp
│   │       ├── file_source.hpp
│   │       ├── file_sink.hpp
│   │       └── amplifier.hpp
│   ├── src/
│   │   ├── file_source.cpp
│   │   ├── file_sink.cpp
│   │   └── amplifier.cpp
│   └── CMakeLists.txt
├── python-binding/
│   ├── src/
│   │   ├── python_bindings.cpp
│   │   ├── buffer_bindings.cpp
│   │   ├── block_bindings.cpp
│   │   └── runtime_bindings.cpp
│   └── CMakeLists.txt
├── tests/
│   ├── cpp/
│   │   ├── test_buffer_pool.cpp
│   │   ├── test_buffer_allocator.cpp
│   │   ├── test_port_queue.cpp
│   │   ├── test_block.cpp
│   │   ├── test_scheduler.cpp
│   │   ├── test_runtime.cpp
│   │   └── test_multiprocess.cpp      # 多进程测试
│   ├── python/
│   │   ├── test_buffer.py
│   │   ├── test_block.py
│   │   ├── test_runtime.py
│   │   └── test_multiprocess.py
│   └── CMakeLists.txt
├── examples/
│   ├── cpp/
│   │   ├── simple_pipeline.cpp
│   │   ├── multiprocess_example.cpp
│   │   └── timestamp_sync_example.cpp
│   └── python/
│       ├── simple_pipeline.py
│       └── multiprocess_example.py
├── logger/                            # 保留
├── tracy-integration/                 # 保留
├── design/                            # 保留所有设计文档
├── docs/                              # 保留
├── commit/                            # 保留
├── old_impl/                          # 旧实现备份
└── CMakeLists.txt

```

---

## 🚀 开始实施

准备清除旧代码并开始 Phase 1 实施...

