#!/usr/bin/env python3
"""
RingQueue Python 绑定测试
"""

import sys
import os
import time
import threading

# 添加构建输出目录到路径
build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../build/python'))
sys.path.insert(0, build_dir)
print(f"添加模块路径: {build_dir}")

try:
    import multiqueue_shm as mq
except ImportError as e:
    print(f"错误：无法导入 multiqueue_shm 模块: {e}")
    print("请先编译 Python 绑定：cmake -DBUILD_PYTHON_BINDING=ON && make")
    sys.exit(1)


def test_import():
    """测试模块导入"""
    print("\n[测试 1/9] test_import - 开始")
    print("✓ 模块导入成功")
    print(f"  版本: {mq.__version__}")
    print(f"  队列版本: {mq.QUEUE_VERSION}")
    print("[测试 1/9] test_import - 完成")


def test_config():
    """测试配置"""
    print("\n[测试 2/9] test_config - 开始")
    config = mq.QueueConfig(1024)
    print(f"  创建配置: capacity={config.capacity}")
    assert config.capacity == 1024
    assert config.is_valid()
    print("✓ QueueConfig 测试通过")
    print("[测试 2/9] test_config - 完成")


def test_ringqueue_basic():
    """测试基本操作"""
    print("\n[测试 3/9] test_ringqueue_basic - 开始")
    print("  创建配置...")
    config = mq.QueueConfig(1024)
    print("  创建队列...")
    queue = mq.RingQueueInt("test_queue_py_basic", config)
    print("  队列创建成功")
    
    # Push
    print("  测试 push...")
    assert queue.push(42, 0)
    assert len(queue) == 1
    assert not queue.empty()
    print(f"  push 成功: size={len(queue)}")
    
    # Pop
    print("  测试 pop...")
    result = queue.pop()
    assert result[0] == 42  # data
    assert result[1] == 0   # timestamp
    assert len(queue) == 0
    assert queue.empty()
    print(f"  pop 成功: data={result[0]}, size={len(queue)}")
    
    print("✓ RingQueueInt 基本操作测试通过")
    print("[测试 3/9] test_ringqueue_basic - 完成")


def test_ringqueue_multiple():
    """测试多个元素"""
    print("\n[测试 4/9] test_ringqueue_multiple - 开始")
    print("  创建配置...")
    config = mq.QueueConfig(1024)
    config.has_timestamp = True  # 启用时间戳
    print("  创建队列...")
    queue = mq.RingQueueInt("test_queue_py_multi", config)
    
    # Push 多个
    count = 100
    print(f"  Push {count} 个元素...")
    for i in range(count):
        assert queue.push(i, i * 1000)
        if i % 25 == 0:
            print(f"    已 push {i}/{count}")
    
    assert len(queue) == count
    print(f"  Push 完成: size={len(queue)}")
    
    # Pop 多个
    print(f"  Pop {count} 个元素...")
    for i in range(count):
        data, timestamp = queue.pop()
        assert data == i
        assert timestamp == i * 1000
        if i % 25 == 0:
            print(f"    已 pop {i}/{count}")
    
    assert queue.empty()
    print("✓ 多元素测试通过")
    print("[测试 4/9] test_ringqueue_multiple - 完成")


def test_ringqueue_double():
    """测试 double 类型队列"""
    print("\n[测试 5/9] test_ringqueue_double - 开始")
    print("  创建配置...")
    config = mq.QueueConfig(1024)
    config.has_timestamp = True  # 启用时间戳
    print("  创建队列...")
    queue = mq.RingQueueDouble("test_queue_py_double", config)
    
    # Push
    print("  测试 push...")
    assert queue.push(3.14, 12345)
    assert len(queue) == 1
    print("  push 成功")
    
    # Pop
    print("  测试 pop...")
    data, timestamp = queue.pop()
    assert abs(data - 3.14) < 0.001
    assert timestamp == 12345
    print(f"  pop 成功: data={data}, ts={timestamp}")
    
    print("✓ RingQueueDouble 测试通过")
    print("[测试 5/9] test_ringqueue_double - 完成")


def test_timestamp():
    """测试时间戳"""
    print("\n[测试 6/9] test_timestamp - 开始")
    print("  获取时间戳 1...")
    ts1 = mq.timestamp_now()
    print(f"  ts1={ts1}")
    time.sleep(0.01)  # 10ms
    print("  获取时间戳 2...")
    ts2 = mq.timestamp_now()
    print(f"  ts2={ts2}, diff={ts2-ts1}ns")
    
    assert ts2 > ts1
    assert ts2 - ts1 > 10000000  # > 10ms in nanoseconds
    
    # 测试不同格式
    print("  测试不同格式...")
    ts_micros = mq.timestamp_now_micros()
    ts_millis = mq.timestamp_now_millis()
    
    assert ts_micros > 0
    assert ts_millis > 0
    print(f"  micros={ts_micros}, millis={ts_millis}")
    
    print("✓ 时间戳测试通过")
    print("[测试 6/9] test_timestamp - 完成")


def test_stats():
    """测试统计信息"""
    print("\n[测试 7/9] test_stats - 开始")
    print("  创建队列...")
    config = mq.QueueConfig(1024)
    queue = mq.RingQueueInt("test_queue_py_stats", config)
    
    # Push 一些数据
    print("  Push 10 个元素...")
    for i in range(10):
        queue.push(i, 0)
    
    print("  获取统计信息...")
    stats = queue.get_stats()
    print(f"  pushed={stats.total_pushed}, popped={stats.total_popped}, size={stats.current_size}")
    assert stats.total_pushed == 10
    assert stats.total_popped == 0
    assert stats.current_size == 10
    assert stats.capacity == 1024
    assert not stats.is_closed
    
    # Pop 一些数据
    print("  Pop 5 个元素...")
    for i in range(5):
        queue.pop()
    
    print("  获取统计信息...")
    stats = queue.get_stats()
    print(f"  pushed={stats.total_pushed}, popped={stats.total_popped}, size={stats.current_size}")
    assert stats.total_pushed == 10
    assert stats.total_popped == 5
    assert stats.current_size == 5
    
    print("✓ 统计信息测试通过")
    print("[测试 7/9] test_stats - 完成")


def test_blocking_mode():
    """测试阻塞模式"""
    print("\n[测试 8/9] test_blocking_mode - 开始")
    print("  创建配置...")
    config = mq.QueueConfig(1024)
    config.blocking_mode = mq.BlockingMode.BLOCKING
    config.timeout_ms = 100
    
    print("  创建队列...")
    queue = mq.RingQueueInt("test_queue_py_blocking", config)
    
    # 空队列 pop_blocking 应该超时
    print("  测试空队列 pop_blocking (100ms 超时)...")
    start = time.time()
    result = queue.pop_blocking(100)  # 使用 pop_blocking，100ms 超时
    elapsed = time.time() - start
    print(f"  pop_blocking 返回: {result}, 耗时: {elapsed*1000:.1f}ms")
    
    assert result[0] is None  # 超时返回 None
    assert elapsed >= 0.09  # 至少 90ms
    
    print("✓ 阻塞模式测试通过")
    print("[测试 8/9] test_blocking_mode - 完成")


def test_multithreading():
    """测试多线程"""
    print("\n[测试 9/9] test_multithreading - 开始")
    print("  创建队列...")
    config = mq.QueueConfig(1024)
    queue = mq.RingQueueInt("test_queue_py_mt", config)
    
    count = 1000
    results = []
    producer_done = threading.Event()
    
    def producer():
        print("  [Producer] 开始...")
        for i in range(count):
            while not queue.push(i, 0):
                time.sleep(0.0001)  # 队列满时等待
            if i % 200 == 0:
                print(f"  [Producer] 进度: {i}/{count}")
        producer_done.set()
        print("  [Producer] 完成")
    
    def consumer():
        print("  [Consumer] 开始...")
        consumed = 0
        while True:
            data, _ = queue.pop()
            if data is not None:
                results.append(data)
                consumed += 1
                if consumed % 200 == 0:
                    print(f"  [Consumer] 进度: {consumed}/{count}")
            
            # 生产者完成且收集到足够数据则退出
            if producer_done.is_set() and len(results) == count:
                break
            
            # 队列空且生产者未完成，稍等
            if data is None and not producer_done.is_set():
                time.sleep(0.0001)
        print("  [Consumer] 完成")
    
    # 启动线程
    print("  启动生产者和消费者线程...")
    t1 = threading.Thread(target=producer)
    t2 = threading.Thread(target=consumer)
    
    t1.start()
    t2.start()
    
    print("  等待线程完成...")
    t1.join()
    print("  生产者线程已结束")
    t2.join()
    print("  消费者线程已结束")
    
    # 验证
    print(f"  验证结果: 收集到 {len(results)} 个元素")
    assert len(results) == count
    results.sort()
    assert results == list(range(count))
    
    print("✓ 多线程测试通过")
    print("[测试 9/9] test_multithreading - 完成")


def main():
    """运行所有测试"""
    print("=" * 50)
    print("MultiQueue-SHM Python 绑定测试")
    print("=" * 50)
    print()
    
    tests = [
        test_import,
        test_config,
        test_ringqueue_basic,
        test_ringqueue_multiple,
        test_ringqueue_double,
        test_timestamp,
        test_stats,
        test_blocking_mode,
        test_multithreading,
    ]
    
    passed = 0
    failed = 0
    
    for test in tests:
        try:
            test()
            passed += 1
        except Exception as e:
            print(f"✗ {test.__name__} 失败: {e}")
            import traceback
            traceback.print_exc()
            failed += 1
    
    print()
    print("=" * 50)
    print(f"测试完成: {passed} 通过, {failed} 失败")
    print("=" * 50)
    
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

