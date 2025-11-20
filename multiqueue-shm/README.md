# MultiQueue-SHM: 高性能多对多共享内存队列库

## 📖 项目简介

MultiQueue-SHM 是一个跨平台的高性能共享内存队列库，支持多生产者-多消费者（MPMC）模式。该库提供 C++ 核心实现和 Python 绑定，可独立或混合使用。

### 核心特性

- ✅ **多对多模式**：支持多个生产者和多个消费者同时操作
- ✅ **统一内存池**：基于数组的高效环形队列实现
- ✅ **类型安全**：C++ 模板支持任意数据类型
- ✅ **时间戳同步**：多队列合并时的精确时间同步机制
- ✅ **灵活配置**：支持阻塞/非阻塞模式、独立线程等
- ✅ **跨平台**：支持 Linux、macOS、Windows
- ✅ **双语言支持**：C++ 和 Python 可独立或协同使用
- ✅ **性能监控**：集成 Tracy Profiler 实时性能分析

## 🏗️ 项目结构

```
multiqueue-shm/
├── core/                      # C++ 核心库
│   ├── include/              # 公共头文件
│   │   ├── ring_queue.hpp   # 环形队列模板类
│   │   ├── queue_manager.hpp # 队列管理器
│   │   ├── metadata.hpp     # 元数据结构
│   │   └── config.hpp       # 配置选项
│   ├── src/                 # 实现文件
│   └── CMakeLists.txt       # 核心库构建配置
│
├── logger/                   # 独立日志组件
│   ├── include/
│   │   └── mp_logger.hpp    # 多进程安全日志
│   ├── src/
│   └── CMakeLists.txt
│
├── python-binding/           # Python 绑定
│   ├── src/
│   │   └── bindings.cpp    # pybind11 绑定代码
│   ├── multiqueue_shm/     # Python 包
│   │   └── __init__.py
│   ├── setup.py
│   └── CMakeLists.txt
│
├── tracy-integration/        # Tracy 性能监控
│   ├── tracy_wrapper.hpp
│   └── CMakeLists.txt
│
├── tests/                    # 测试程序
│   ├── cpp/                 # C++ 单元测试
│   ├── python/              # Python 测试
│   └── integration/         # 多进程集成测试
│
├── examples/                 # 使用示例
│   ├── cpp/
│   └── python/
│
├── docs/                     # 文档
│   ├── architecture.md      # 架构设计
│   ├── api_reference.md     # API 参考
│   └── performance.md       # 性能测试报告
│
├── cmake/                    # CMake 辅助模块
│   ├── FindBoost.cmake
│   └── FindTracy.cmake
│
├── commit/                   # 变更记录
│
├── CMakeLists.txt           # 根 CMake 配置
└── README.md                # 本文件
```

## 🚀 快速开始

### 前置依赖

- C++17 或更高版本
- CMake 3.15+
- Boost 1.70+ (Boost.Interprocess)
- Python 3.8+ (可选，用于 Python 绑定)
- pybind11 (可选，用于 Python 绑定)
- Tracy Profiler (可选，用于性能分析)

### 编译 C++ 库

```bash
mkdir build && cd build
cmake .. -DBUILD_PYTHON_BINDING=OFF -DENABLE_TRACY=ON
cmake --build .
```

### 编译 Python 绑定

```bash
cd python-binding
pip install .
```

### 运行测试

```bash
# C++ 测试
cd build
ctest

# Python 测试
cd tests/python
python test_basic_queue.py
```

## 📚 技术栈

| 组件 | 技术 | 版本要求 |
|-----|------|---------|
| 核心语言 | C++ | C++17+ |
| 构建系统 | CMake | 3.15+ |
| 共享内存 | Boost.Interprocess | 1.70+ |
| Python 绑定 | pybind11 | 2.10+ |
| 性能分析 | Tracy Profiler | Latest |
| 日志格式化 | fmt | 9.0+ |
| 单元测试 | Google Test | 1.12+ |

## 📊 开发状态

| 模块 | 状态 | 进度 |
|-----|------|------|
| 核心库架构 | 🟡 规划中 | 0% |
| 日志组件 | 🟡 规划中 | 0% |
| Python 绑定 | 🟡 规划中 | 0% |
| Tracy 集成 | 🟡 规划中 | 0% |
| 多进程测试 | 🟡 规划中 | 0% |
| 文档 | 🟡 规划中 | 10% |

## 🔑 核心概念

### 1. 环形队列（Ring Queue）
基于统一内存池和数组实现的高效循环队列，支持多生产者和多消费者。

### 2. 元数据头部（Metadata Header）
每个队列维护一个固定大小的头部空间，存储：
- 队列配置信息
- 生产者/消费者计数器
- 时间戳信息
- 用户自定义元数据

### 3. 阻塞模式
- **阻塞模式**：生产者不能越过消费者写入，需要等待
- **非阻塞模式**：生产者可以覆盖未读数据，但记录覆盖事件

### 4. 时间戳同步
多个队列合并时，根据时间戳对齐数据。若队列无时间戳，则阻塞直到超时。

## 📈 性能目标

- 单队列吞吐量：> 1M ops/sec
- 延迟：< 1us (P99)
- 支持队列数量：> 1000
- 单队列大小：最大 4GB

## 🤝 贡献指南

详见 `docs/CONTRIBUTING.md`

## 📄 许可证

MIT License

## 📮 联系方式

- 问题反馈：GitHub Issues
- 文档：`docs/` 目录

---

**最后更新**: 2025-11-18
**版本**: 0.1.0-alpha


