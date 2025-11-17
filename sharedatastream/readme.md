## 文件介绍

**shared_ring_queue.cpp** 提供共享内存的创建，消费者，生产者的创建，内部缓存队列，开启线程等功能，使用pybind11 创建python模块。共享内存内部维护环形队列， 消费者连接后，创建消费指针， 生产者指针不能追越全部消费者指针，消费者指针不能追越超过生产者指针。

**processor_decorator/processor_decorator.py** 共享内存的python封装，使用装饰器。
装饰器sharedmem_consumer创建消费者函数，程序根据装饰器参数连接共享内存队列，内部接收创建线程队列，通过c++回调调用注册的python函数，使用batch控制一次返回的大小。
消费者示例 
```python
def get_params():
    # 支持命令行参数: in_shm queue_len block_size batch_size timeout_ms
    if len(sys.argv) >= 6:
        return {
            'in_shm': sys.argv[1],
            'in_queue_len': int(sys.argv[2]),
            'in_block_size': int(sys.argv[3]),
            'batch_size': int(sys.argv[4]),
            'timeout_ms': int(sys.argv[5])
        }
    else:
        return {
            'in_shm': "RingQueueSineWave",
            'in_queue_len': 1024,
            'in_block_size': 12800,
            'batch_size': 1,
            'timeout_ms': 50
        }
@sharedmem_consumer(get_params())
def plot_sine(batch):
    global buffer
    for b in batch:
        arr = np.frombuffer(b, dtype=np.float32)
        buffer.extend(arr.tolist())
    buffer = buffer[-10000:]
```

装饰器sharedmem_producer创建生产者函数，每次函数返回把返回数据放入到共享内存中，有两种模式，如无消费者，则根据参数interval控制多久调用一次装饰器下的函数，如果联合使用，则根据消费者消费的数据进行调用。
生产者示例：
```python
def get_params():
    # 支持命令行参数: out_shm queue_len block_size interval
    if len(sys.argv) >= 5:
        return {
            'out_shm': sys.argv[1],
            'out_queue_len': int(sys.argv[2]),
            'out_block_size': int(sys.argv[3]),
            'interval': float(sys.argv[4])
        }
    else:
        return {
            'out_shm': "RingQueueSineWave",
            'out_queue_len': 1024,
            'out_block_size': 12800,
            'interval': 0.05
        }
@sharedmem_producer(get_params())
def sinewave_producer():
    global t, count
    fs = 1000  # 采样率
    freq = 1   # 正弦波频率
    morph_period = 5  # 形状变化周期（秒）
    if 't' not in globals():
        globals()['t'] = 0
    if 'count' not in globals():
        globals()['count'] = 0
    t = globals()['t']
    count = globals()['count']
    now = time.time()
    alpha = 0.5 * (1 + np.sin(2 * np.pi * now / morph_period))
    x = np.arange(get_params()['out_block_size'] // 4, dtype=np.float32)
    theta = 2 * np.pi * freq * (x + t) / fs
    y = (1 - alpha) * np.sin(theta) + alpha * np.cos(theta)
    data = y.astype(np.float32).tobytes()
    t += len(x)
    count += 1
    if count % 20 == 0:
        print(f"count: {count}")
    globals()['t'] = t
    globals()['count'] = count
    return data
```
消费者+生产者示例 
```python
def get_consumer_params():
    if len(sys.argv) >= 6:
        return {
            'in_shm': sys.argv[1],
            'in_queue_len': int(sys.argv[2]),
            'in_block_size': int(sys.argv[3]),
            'batch_size': int(sys.argv[4]),
            'timeout_ms': int(sys.argv[5])
        }
    else:
        return {
            'in_shm': "RingQueueSineWave",
            'in_queue_len': 1024,
            'in_block_size': 12800,
            'batch_size': 1,
            'timeout_ms': 50
        }

def get_producer_params():
    if len(sys.argv) >= 10:
        return {
            'out_shm': sys.argv[6],
            'out_queue_len': int(sys.argv[7]),
            'out_block_size': int(sys.argv[8]),
            'interval': float(sys.argv[9])
        }
    else:
        return {
            'out_shm': "RingQueueSineWaveHalfAmp",
            'out_queue_len': 1024,
            'out_block_size': 12800,
            'interval': 0.01
        }

@sharedmem_producer(get_producer_params())
@sharedmem_consumer(get_consumer_params())
def half_amp(batch):
    # batch: list[bytes]
    for b in batch:
        arr = np.frombuffer(b, dtype=np.float32)
        arr2 = (arr * 0.5).astype(np.float32)
        yield arr2.tobytes()
```
**main.cpp** pybind11的c++函数绑定

