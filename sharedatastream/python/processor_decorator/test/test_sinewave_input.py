import numpy as np
import sys
from processor_decorator import sharedmem_consumer

buffer = []

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
    # 不返回

if __name__ == "__main__":
    import time
    import matplotlib.pyplot as plt
    plt.ion()
    fig, ax = plt.subplots()
    line, = ax.plot([])
    ax.set_ylim(-1.2, 1.2)
    ax.set_xlim(0, 10000)
    print("开始从共享内存读取并绘制正弦/余弦变形波...")
    while True:
        if buffer:
            line.set_ydata(buffer)
            line.set_xdata(np.arange(len(buffer)))
            ax.set_xlim(0, max(10000, len(buffer)))
            fig.canvas.draw()
            fig.canvas.flush_events()
        time.sleep(0.05) 