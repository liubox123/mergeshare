#!/usr/bin/env python3
"""
多线程测试 - 调试版本
"""

import sys
import os
import time
import threading

build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../build/python'))
sys.path.insert(0, build_dir)

try:
    import multiqueue_shm as mq
except ImportError as e:
    print(f"错误：无法导入 multiqueue_shm 模块: {e}")
    sys.exit(1)

print("=" * 50)
print("多线程测试 - 调试")
print("=" * 50)

config = mq.QueueConfig(1024)
queue = mq.RingQueueInt("test_mt_debug", config)

count = 100  # 先测试小数量
results = []
producer_done = threading.Event()

def producer():
    print(f"[Producer] 开始生产 {count} 个元素...")
    for i in range(count):
        while not queue.push(i, 0):
            time.sleep(0.0001)  # 队列满时等待
        if i % 20 == 0:
            print(f"[Producer] 已生产 {i}/{count}")
    producer_done.set()
    print(f"[Producer] 完成！")

def consumer():
    consumed = 0
    print(f"[Consumer] 开始消费...")
    while True:
        data, _ = queue.pop()
        if data is not None:
            results.append(data)
            consumed += 1
            if consumed % 20 == 0:
                print(f"[Consumer] 已消费 {consumed}/{count}")
        
        # 检查是否完成
        if producer_done.is_set() and len(results) == count:
            break
        
        # 如果队列空了但生产者还没完成，稍等一下
        if data is None and not producer_done.is_set():
            time.sleep(0.0001)
    
    print(f"[Consumer] 完成！消费了 {len(results)} 个元素")

# 启动线程
t1 = threading.Thread(target=producer)
t2 = threading.Thread(target=consumer)

print("\n启动线程...")
t1.start()
t2.start()

print("等待完成...")
t1.join()
t2.join()

# 验证
print(f"\n验证结果: 收到 {len(results)} 个元素")
results.sort()
missing = []
for i in range(count):
    if i not in results:
        missing.append(i)

if len(missing) > 0:
    print(f"  ✗ 缺失元素: {missing[:10]}...")
    sys.exit(1)
else:
    print(f"  ✓ 所有元素完整")

print("\n" + "=" * 50)
print("多线程测试通过！")
print("=" * 50)

