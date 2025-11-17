from processor_decorator import sharedmem_processor

# 假设输入输出共享内存参数如下
IN_SHM = "RingQueueInput"
OUT_SHM = "RingQueueOutput"
QUEUE_LEN = 1024
BLOCK_SIZE = 128
TOTAL_REFS = 1
META = "algo meta"
BATCH_SIZE = 4
TIMEOUT_MS = 50

@sharedmem_processor(
    in_shm=IN_SHM, in_queue_len=QUEUE_LEN, in_block_size=BLOCK_SIZE,
    out_shm=OUT_SHM, out_queue_len=QUEUE_LEN, out_block_size=BLOCK_SIZE,
    total_refs=TOTAL_REFS, metadata=META, batch_size=BATCH_SIZE, timeout_ms=TIMEOUT_MS
)
def my_algo(batch):
    print(f"[Python Algo] got batch: {[len(x) for x in batch]}")
    # 这里简单回传原数据，可自定义算法
    return [b"processed:" + x for x in batch]

if __name__ == "__main__":
    import time
    print("Python算法处理进程已启动，等待数据...")
    while True:
        time.sleep(1) 