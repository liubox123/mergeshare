import shared_ring_queue
import time

# 参数与C++测试保持一致
def test_producer():
    shm_name = "RingQueueSharedMemory44"
    queue_len = 1024
    data_block_size = 128
    total_refs = 1
    metadata = "test meta"
    msg_count = 100000
    producer = shared_ring_queue.SharedRingQueueProducer(
        shm_name, queue_len, data_block_size, total_refs, metadata
    )
    data = b'A' * data_block_size
    for i in range(msg_count):
        while not producer.push(data):
            time.sleep(0.001)
        if (i+1) % 100 == 0:
            print(f"[Python Producer] pushed: {i+1}")

def test_consumer():
    shm_name = "RingQueueSharedMemory44"
    queue_len = 1024
    data_block_size = 128
    msg_count = 1000
    consumer = shared_ring_queue.SharedRingQueueConsumer(
        shm_name, queue_len, data_block_size
    )
    for i in range(msg_count):
        while True:
            data = consumer.pop()
            if data:
                break
            time.sleep(0.001)
        if (i+1) % 100 == 0:
            print(f"[Python Consumer] popped: {i+1}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "consumer":
        test_consumer()
    else:
        test_producer() 