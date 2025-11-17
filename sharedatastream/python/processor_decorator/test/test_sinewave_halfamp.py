import numpy as np
import sys
from processor_decorator import sharedmem_consumer, sharedmem_producer

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

if __name__ == "__main__":
    import time
    print("开始幅度减半处理...")
    while True:
        time.sleep(1) 