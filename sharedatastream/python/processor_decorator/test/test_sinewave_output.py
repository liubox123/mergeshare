import numpy as np
import time
import sys
from processor_decorator import sharedmem_producer

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

if __name__ == "__main__":
    print("开始写入正弦/余弦变形波到共享内存...")
    while True:
        time.sleep(1) 