import numpy as np
import cv2
import time
import sys
from processor_decorator import sharedmem_consumer
import threading

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

new_data = []
data_lock = threading.Lock()

@sharedmem_consumer(get_params())
def on_data(batch):
    arrs = []
    for b in batch:
        arr = np.frombuffer(b, dtype=np.float32)
        arrs.extend(arr.tolist())
    with data_lock:
        new_data.extend(arrs)

def draw_wave(y, winname="SineWave", fps=None, fixed_max=1.0):
    h, w = 400, 1000
    img = np.ones((h, w, 3), dtype=np.uint8) * 255
    if len(y) > 1:
        y = np.array(y)
        # 固定幅度归一化
        y_clip = np.clip(y, -fixed_max, fixed_max)
        y_norm = ((y_clip + fixed_max) / (2 * fixed_max) * (h * 0.8) + h * 0.1).astype(np.int32)
        x = np.linspace(0, w-1, len(y_norm)).astype(np.int32)
        for i in range(1, len(y_norm)):
            cv2.line(img, (x[i-1], h - y_norm[i-1]), (x[i], h - y_norm[i]), (0, 0, 255), 2)
    if fps is not None:
        cv2.putText(img, f"FPS: {fps:.1f}", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0,0,0), 2)
    cv2.imshow(winname, img)
    cv2.waitKey(1)

if __name__ == "__main__":
    print("开始从共享内存读取并用OpenCV绘制正弦/余弦变形波...")
    last_time = time.time()
    frame_count = 0
    fps = 0
    while True:
        y = []
        with data_lock:
            if new_data:
                y = new_data.copy()
                new_data.clear()
        if y:
            draw_wave(y, fps=fps, fixed_max=1.0)
            frame_count += 1
        now = time.time()
        if now - last_time >= 1.0:
            fps = frame_count / (now - last_time)
            frame_count = 0
            last_time = now
        time.sleep(0.01) 