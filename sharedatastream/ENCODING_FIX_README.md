# UTF-8 编码修复说明

## 问题描述
`processor_decorator.py` 中的 `shared_ring_queue.SharedMemProcessor` 在处理输入字符串时没有强制使用 UTF-8 编码，可能导致编码问题。

## 修复内容

### 1. C++ 层面的修复

#### main.cpp
- 添加了 `is_valid_utf8()` 函数：检测字符串是否为有效的 UTF-8 编码
- 添加了 `convert_to_utf8()` 函数：尝试从常见编码（如 Latin-1）转换为 UTF-8
- 添加了 `ensure_utf8_string()` 函数：综合处理，确保输入字符串使用 UTF-8 编码

#### shared_ring_queue.cpp
- 添加了相同的编码检测和转换函数
- 更新了所有字符串处理点，确保使用 UTF-8 编码

### 2. Python 层面的修复

#### processor_decorator.py
- 添加了 `ensure_utf8_bytes()` 函数：检测并转换字节序列为 UTF-8
- 添加了 `validate_and_convert_string()` 函数：支持 str 和 bytes 输入的统一处理
- 更新了 `SharedMemConsumerWrapper.register()` 方法：在回调前验证编码
- 更新了 `SharedMemProducerWrapper.push_to_output()` 方法：在输出前验证编码

## 功能特性

### 编码检测
- 自动检测输入数据是否为有效的 UTF-8 编码
- 支持检测 Latin-1 编码并转换为 UTF-8
- 使用 `chardet` 库进行更精确的编码检测（Python 层面）

### 错误处理
- 当无法检测或转换编码时，使用错误处理策略（替换无效字符）
- 提供警告信息，便于调试和监控
- 确保程序不会因编码问题而崩溃

### 兼容性
- 支持多种输入类型：str、bytes、其他可转换类型
- 向后兼容现有代码
- 在 C++ 和 Python 层面都提供一致的编码处理

## 依赖要求

### Python 依赖
```bash
pip install chardet
```

### C++ 依赖
- Boost.Interprocess
- pybind11

## 使用方法

### 基本使用
```python
from processor_decorator import sharedmem_consumer, sharedmem_producer

@sharedmem_consumer(in_shm="input", in_queue_len=100, in_block_size=1024)
@sharedmem_producer(out_shm="output", out_queue_len=100, out_block_size=1024)
def process_data(batch):
    for data in batch:
        # data 现在保证是有效的 UTF-8 字节序列
        print(data.decode('utf-8'))
        yield data  # 输出也会自动验证编码
```

### 编码转换示例
```python
# 输入可能是不同编码的字符串
input_data = "你好世界".encode('gbk')  # GBK 编码
# 系统会自动检测并转换为 UTF-8

# 或者直接输入字符串
input_data = "Hello 世界"  # 字符串
# 系统会自动编码为 UTF-8
```

## 测试建议

1. 测试不同编码的输入数据
2. 测试无效编码的处理
3. 测试大量数据的性能影响
4. 验证输出数据的编码正确性

## 注意事项

1. 编码检测和转换会带来一定的性能开销
2. 对于大量数据，建议在输入前就确保使用 UTF-8 编码
3. 警告信息会帮助识别编码问题，建议在生产环境中监控这些警告
4. 如果 `chardet` 库不可用，系统会回退到基础的编码检测方法
