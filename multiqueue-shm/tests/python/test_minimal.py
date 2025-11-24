#!/usr/bin/env python3
"""
最小化测试 - 只测试创建队列
"""

import sys
import os

build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../build/python'))
sys.path.insert(0, build_dir)

import multiqueue_shm as mq

print("Step 1: 导入模块 OK")

print("Step 2: 创建配置...")
config = mq.QueueConfig(1024)
print("  配置创建 OK")

print("Step 3: 准备创建队列 'test_minimal'...")
print("  即将调用 RingQueueInt()...")
sys.stdout.flush()

try:
    queue = mq.RingQueueInt("test_minimal", config)
    print("  队列创建成功!")
except Exception as e:
    print(f"  错误: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("Step 4: 测试 push...")
result = queue.push(123, 0)
print(f"  push 结果: {result}")

print("Step 5: 测试 pop...")
data, ts = queue.pop()
print(f"  pop 结果: data={data}, ts={ts}")

print("\n所有测试通过!")

