#!/usr/bin/env python3
"""
简化测试 - 逐步排查问题
"""

import sys
import os

# 添加构建输出目录到路径
build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../build/python'))
sys.path.insert(0, build_dir)

try:
    import multiqueue_shm as mq
except ImportError as e:
    print(f"错误：无法导入 multiqueue_shm 模块: {e}")
    sys.exit(1)

print("=" * 50)
print("简化测试 - 逐步排查")
print("=" * 50)

# 测试1: 导入
print("\n[1/7] 测试模块导入...")
print(f"  ✓ 模块版本: {mq.__version__}")

# 测试2: 配置
print("\n[2/7] 测试配置...")
config = mq.QueueConfig(1024)
print(f"  ✓ 配置创建成功, capacity={config.capacity}")

# 测试3: 创建队列
print("\n[3/7] 测试创建队列...")
queue = mq.RingQueueInt("test_simple", config)
print(f"  ✓ 队列创建成功")

# 测试4: Push
print("\n[4/7] 测试 push...")
result = queue.push(42, 0)
print(f"  ✓ Push 成功: {result}, size={len(queue)}")

# 测试5: Pop
print("\n[5/7] 测试 pop...")
data, ts = queue.pop()
print(f"  ✓ Pop 成功: data={data}, ts={ts}, size={len(queue)}")

# 测试6: 空队列 Pop (非阻塞)
print("\n[6/7] 测试空队列 pop...")
data, ts = queue.pop()
print(f"  ✓ 空队列 pop: data={data}, ts={ts}")

# 测试7: 统计
print("\n[7/7] 测试统计...")
stats = queue.get_stats()
print(f"  ✓ 统计: pushed={stats.total_pushed}, popped={stats.total_popped}")

print("\n" + "=" * 50)
print("所有基础测试通过！")
print("=" * 50)

