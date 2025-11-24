# 测试策略文档

本文档定义 multiqueue-shm 项目的测试策略，包括测试类型、测试用例、测试数据和验收标准。

## 测试目标

1. **功能正确性**: 确保所有功能按预期工作
2. **线程安全**: 确保多生产者多消费者场景下无数据竞争
3. **进程安全**: 确保多进程场景下数据一致性
4. **性能达标**: 确保性能满足目标指标
5. **平台兼容**: 确保在 Linux、macOS、Windows 上正常工作
6. **内存安全**: 确保无内存泄漏、无缓冲区溢出

## 测试分类

### 1. 单元测试 (Unit Tests)

测试单个类和函数的功能。

| 测试对象 | 测试工具 | 覆盖率目标 |
|---------|---------|-----------|
| C++ 代码 | Google Test | > 80% |
| Python 代码 | pytest | > 80% |

### 2. 集成测试 (Integration Tests)

测试多个组件协同工作。

- 多队列管理
- 时间戳同步
- 异步线程模式

### 3. 多进程测试 (Multi-Process Tests)

测试跨进程的功能。

- C++ 多进程
- Python 多进程
- 混合 C++/Python 进程

### 4. 性能测试 (Performance Tests)

测试性能指标。

- 吞吐量测试
- 延迟测试
- 负载测试

### 5. 压力测试 (Stress Tests)

测试系统在极端条件下的表现。

- 大量生产者/消费者
- 长时间运行
- 队列满/空边界

### 6. 兼容性测试 (Compatibility Tests)

测试跨平台兼容性。

- Linux (Ubuntu, CentOS)
- macOS (Intel, Apple Silicon)
- Windows (MSVC, MinGW)

## 测试用例清单

### 阶段 2: 核心队列测试

#### 2.1 基本功能测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.1.1 | 创建队列 | 1. 创建队列<br>2. 验证元数据 | 队列创建成功，元数据正确 |
| T2.1.2 | 单次写入读取 | 1. push(42)<br>2. pop() | 读取到 42 |
| T2.1.3 | 多次写入读取 | 1. push(1..100)<br>2. pop() 100次 | 顺序读取 1..100 |
| T2.1.4 | 队列容量 | 1. 查询 capacity() | 返回配置的容量 |
| T2.1.5 | 队列大小 | 1. push N 次<br>2. 查询 size() | 返回 N |

#### 2.2 边界条件测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.2.1 | 队列空读取 | 1. 空队列<br>2. try_pop() | 返回 false |
| T2.2.2 | 队列满写入（阻塞） | 1. push 直到满<br>2. push (阻塞模式) | 阻塞等待 |
| T2.2.3 | 队列满写入（非阻塞） | 1. push 直到满<br>2. push (非阻塞模式) | 覆盖旧数据 |
| T2.2.4 | 容量为 1 | 1. capacity=1<br>2. push, pop 交替 | 正常工作 |
| T2.2.5 | 容量为最大值 | 1. capacity=1M<br>2. 测试基本操作 | 正常工作 |

#### 2.3 多生产者测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.3.1 | 2个生产者 | 1. 2个线程同时 push<br>2. 单消费者 pop | 数据完整，无丢失 |
| T2.3.2 | 10个生产者 | 1. 10个线程同时 push<br>2. 单消费者 pop | 数据完整，无丢失 |
| T2.3.3 | 100个生产者 | 1. 100个线程同时 push<br>2. 多消费者 pop | 数据完整，无丢失 |
| T2.3.4 | 生产者竞争 | 1. 多生产者同时 push<br>2. 验证序列号连续 | 序列号连续递增 |

#### 2.4 多消费者测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.4.1 | 2个消费者 | 1. 单生产者 push<br>2. 2个消费者 pop | 无重复，无丢失 |
| T2.4.2 | 10个消费者 | 1. 单生产者 push<br>2. 10个消费者 pop | 无重复，无丢失 |
| T2.4.3 | 100个消费者 | 1. 多生产者 push<br>2. 100个消费者 pop | 无重复，无丢失 |
| T2.4.4 | 消费者竞争 | 1. 验证每个数据只被读取一次 | 无重复读取 |

#### 2.5 阻塞模式测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.5.1 | 生产者阻塞 | 1. 队列满<br>2. push (阻塞)<br>3. 另一线程 pop | push 成功 |
| T2.5.2 | 消费者阻塞 | 1. 队列空<br>2. pop (阻塞)<br>3. 另一线程 push | pop 成功 |
| T2.5.3 | 超时机制 | 1. 队列满<br>2. push (timeout=100ms) | 100ms 后返回 false |

#### 2.6 非阻塞模式测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.6.1 | 覆盖旧数据 | 1. 队列满<br>2. push (非阻塞) | 成功，覆盖计数+1 |
| T2.6.2 | 覆盖计数 | 1. 多次覆盖<br>2. 查询 overwrite_count | 计数正确 |

#### 2.7 时间戳测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.7.1 | 启用时间戳 | 1. has_timestamp=true<br>2. push, pop | 时间戳正确 |
| T2.7.2 | 禁用时间戳 | 1. has_timestamp=false<br>2. push, pop | 时间戳为 0 |
| T2.7.3 | 时间戳顺序 | 1. 多次 push<br>2. 验证时间戳递增 | 时间戳递增 |

#### 2.8 统计信息测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T2.8.1 | total_pushed | 1. push N 次<br>2. 查询统计 | total_pushed = N |
| T2.8.2 | total_popped | 1. pop N 次<br>2. 查询统计 | total_popped = N |
| T2.8.3 | 生产者计数 | 1. 注册生产者<br>2. 查询 producer_count | 计数正确 |
| T2.8.4 | 消费者计数 | 1. 注册消费者<br>2. 查询 consumer_count | 计数正确 |

### 阶段 3: 队列管理器测试

#### 3.1 队列管理测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T3.1.1 | 创建多个队列 | 1. 创建 10 个队列 | 全部创建成功 |
| T3.1.2 | 打开现有队列 | 1. 创建队列<br>2. 关闭<br>3. 重新打开 | 打开成功，数据保留 |
| T3.1.3 | 删除队列 | 1. 创建队列<br>2. 删除 | 删除成功 |
| T3.1.4 | 列出队列 | 1. 创建多个队列<br>2. list_queues() | 返回所有队列名称 |

#### 3.2 时间戳同步测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T3.2.1 | 两个队列同步 | 1. 创建 2 个队列<br>2. 写入带时间戳数据<br>3. 合并 | 按时间戳排序输出 |
| T3.2.2 | 多个队列同步 | 1. 创建 5 个队列<br>2. 合并 | 按时间戳排序输出 |
| T3.2.3 | 同步超时 | 1. 一个队列无数据<br>2. 合并（timeout=100ms） | 100ms 后超时 |
| T3.2.4 | 时间戳回退检测 | 1. 写入乱序时间戳<br>2. 合并 | 检测到时间戳回退 |

### 阶段 4: Python 绑定测试

#### 4.1 基本功能测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T4.1.1 | Python 创建队列 | 1. RingQueue("test", config) | 创建成功 |
| T4.1.2 | Python push/pop | 1. push(b"data")<br>2. pop() | 返回 b"data" |
| T4.1.3 | NumPy 数组 | 1. push_array(arr)<br>2. pop_array() | 数组一致 |

#### 4.2 多进程测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T4.2.1 | Python 多进程 | 1. 2 个 Python 进程<br>2. push/pop | 数据完整 |
| T4.2.2 | C++/Python 混合 | 1. C++ producer<br>2. Python consumer | 数据传输成功 |

### 阶段 5: 异步线程测试

#### 5.1 异步模式测试

| 用例ID | 用例名称 | 测试步骤 | 预期结果 |
|-------|---------|---------|---------|
| T5.1.1 | 启动异步线程 | 1. enable_async=true<br>2. start() | 线程启动成功 |
| T5.1.2 | 回调函数 | 1. 设置回调<br>2. push 数据 | 回调被调用 |
| T5.1.3 | 缓冲队列满 | 1. push 超过缓冲大小 | 丢弃旧数据 |
| T5.1.4 | 停止线程 | 1. stop() | 线程正常退出 |

### 阶段 6: 性能与压力测试

#### 6.1 吞吐量测试

| 用例ID | 用例名称 | 测试目标 | 验收标准 |
|-------|---------|---------|---------|
| T6.1.1 | 单生产者单消费者 | 测试吞吐量 | > 1M ops/sec |
| T6.1.2 | 多生产者单消费者 | 测试吞吐量 | > 800K ops/sec |
| T6.1.3 | 多生产者多消费者 | 测试吞吐量 | > 500K ops/sec |

#### 6.2 延迟测试

| 用例ID | 用例名称 | 测试目标 | 验收标准 |
|-------|---------|---------|---------|
| T6.2.1 | push 延迟 | 测试 push 延迟 | P50 < 500ns, P99 < 1us |
| T6.2.2 | pop 延迟 | 测试 pop 延迟 | P50 < 500ns, P99 < 1us |
| T6.2.3 | 端到端延迟 | 测试端到端延迟 | P99 < 5us |

#### 6.3 压力测试

| 用例ID | 用例名称 | 测试步骤 | 验收标准 |
|-------|---------|---------|---------|
| T6.3.1 | 100个生产者消费者 | 1. 100 生产者<br>2. 100 消费者<br>3. 运行 1 小时 | 无崩溃，数据完整 |
| T6.3.2 | 1000个队列 | 1. 创建 1000 个队列<br>2. 并发操作 | 正常工作 |
| T6.3.3 | 24小时运行 | 1. 持续运行 24 小时 | 无内存泄漏，无崩溃 |

#### 6.4 内存测试

| 用例ID | 用例名称 | 工具 | 验收标准 |
|-------|---------|------|---------|
| T6.4.1 | 内存泄漏检测 | Valgrind | 无内存泄漏 |
| T6.4.2 | 数据竞争检测 | ThreadSanitizer | 无数据竞争 |
| T6.4.3 | 缓冲区溢出检测 | AddressSanitizer | 无缓冲区溢出 |

## 测试数据

### 数据类型

| 类型 | 说明 | 示例 |
|-----|------|------|
| 整数 | int, uint64_t | 42, 12345 |
| 浮点数 | float, double | 3.14, 2.718 |
| 字符串 | std::string, bytes | "hello", b"world" |
| 结构体 | 自定义结构 | SensorData{temp, humidity} |
| 数组 | numpy.ndarray | [1.0, 2.0, 3.0] |

### 测试数据生成

```python
# Python 测试数据生成器
import random
import numpy as np

class TestDataGenerator:
    @staticmethod
    def generate_int_sequence(n):
        """生成整数序列 0..n-1"""
        return list(range(n))
    
    @staticmethod
    def generate_random_ints(n, min_val=0, max_val=10000):
        """生成随机整数"""
        return [random.randint(min_val, max_val) for _ in range(n)]
    
    @staticmethod
    def generate_random_floats(n):
        """生成随机浮点数"""
        return [random.random() for _ in range(n)]
    
    @staticmethod
    def generate_numpy_arrays(n, shape=(10,), dtype=np.float32):
        """生成 NumPy 数组"""
        return [np.random.rand(*shape).astype(dtype) for _ in range(n)]
    
    @staticmethod
    def generate_strings(n, length=10):
        """生成随机字符串"""
        import string
        return [''.join(random.choices(string.ascii_letters, k=length)) 
                for _ in range(n)]
```

## 测试环境

### 硬件环境

| 配置 | 最低要求 | 推荐配置 |
|-----|---------|---------|
| CPU | 2 核 | 4 核或更多 |
| 内存 | 4 GB | 8 GB或更多 |
| 磁盘 | 10 GB | 20 GB或更多 |

### 软件环境

| 平台 | 版本 |
|-----|------|
| Ubuntu | 20.04, 22.04 |
| macOS | 12.0+, 13.0+ |
| Windows | Windows 10, 11 |
| 编译器 | GCC 9+, Clang 12+, MSVC 2019+ |
| Python | 3.8, 3.9, 3.10, 3.11, 3.12 |

## 测试工具

### C++ 测试工具

| 工具 | 用途 |
|-----|------|
| Google Test | 单元测试框架 |
| Valgrind | 内存泄漏检测 |
| AddressSanitizer | 内存错误检测 |
| ThreadSanitizer | 数据竞争检测 |
| Tracy Profiler | 性能分析 |
| gprof | 性能分析 |

### Python 测试工具

| 工具 | 用途 |
|-----|------|
| pytest | 测试框架 |
| pytest-cov | 代码覆盖率 |
| pytest-xdist | 并行测试 |
| pytest-timeout | 超时控制 |
| memory_profiler | 内存分析 |

## 测试执行

### 单元测试

```bash
# C++ 单元测试
cd build
ctest --output-on-failure

# Python 单元测试
cd tests/python
pytest -v
```

### 性能测试

```bash
# C++ 性能测试
cd build/tests/cpp
./performance_benchmark

# Python 性能测试
cd tests/python
python performance_test.py
```

### 内存检测

```bash
# Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./test_ring_queue

# AddressSanitizer
cmake .. -DENABLE_ASAN=ON
cmake --build .
./test_ring_queue

# ThreadSanitizer
cmake .. -DENABLE_TSAN=ON
cmake --build .
./test_ring_queue
```

## CI/CD 集成

### GitHub Actions 工作流

```yaml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        python: [3.8, 3.9, '3.10', 3.11, 3.12]
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup Python
        uses: actions/setup-python@v4
        with:
          python-version: ${{ matrix.python }}
      
      - name: Install Dependencies
        run: |
          # 安装依赖...
      
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DBUILD_TESTS=ON
          cmake --build .
      
      - name: Run Tests
        run: |
          cd build
          ctest --output-on-failure
      
      - name: Upload Coverage
        uses: codecov/codecov-action@v3
```

## 验收标准

### 阶段 2 验收标准

- ✅ 所有单元测试通过（C++）
- ✅ 代码覆盖率 > 80%
- ✅ 无内存泄漏（Valgrind）
- ✅ 无数据竞争（ThreadSanitizer）
- ✅ 多生产者多消费者测试通过
- ✅ 阻塞/非阻塞模式测试通过

### 阶段 4 验收标准

- ✅ 所有 Python 测试通过
- ✅ Python 代码覆盖率 > 80%
- ✅ Python 多进程测试通过
- ✅ C++/Python 混合测试通过

### 阶段 6 验收标准

- ✅ 吞吐量 > 1M ops/sec
- ✅ 延迟 P99 < 1us
- ✅ 24小时压力测试通过
- ✅ 1000个队列同时运行正常
- ✅ 所有平台测试通过

## 测试报告

### 报告模板

```markdown
# 测试报告

**日期**: 2025-11-18
**版本**: 0.1.0
**测试人员**: 测试工程师

## 测试概要

- 测试用例总数: 150
- 通过: 145
- 失败: 3
- 跳过: 2
- 覆盖率: 82%

## 失败用例

1. T2.3.3: 100个生产者测试
   - 原因: 性能不达标
   - 状态: 已修复

## 性能指标

- 吞吐量: 1.2M ops/sec ✅
- 延迟 P50: 450ns ✅
- 延迟 P99: 980ns ✅

## 建议

1. 优化多生产者场景
2. 增加边界条件测试
```

---

**文档版本**: 1.0  
**最后更新**: 2025-11-18  
**维护者**: 测试工程师


