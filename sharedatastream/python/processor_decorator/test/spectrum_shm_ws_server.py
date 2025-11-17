import numpy as np
import sys
import asyncio
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from processor_decorator import sharedmem_consumer
import time

app = FastAPI()

DEFAULT_FREQ_START = 0  # 0 Hz
DEFAULT_FREQ_END = 8e9  # 8 GHz
DEFAULT_POINTS = int((DEFAULT_FREQ_END - DEFAULT_FREQ_START) / 50000)  # 默认点数


def get_params():
    if len(sys.argv) >= 6:
        return {
            "in_shm": sys.argv[1],
            "in_queue_len": int(sys.argv[2]),
            "in_block_size": int(sys.argv[3]),
            "batch_size": int(sys.argv[4]),
            "timeout_ms": int(sys.argv[5]),
        }
    else:
        return {
            "in_shm": "RingQueueSpectrum",
            "in_queue_len": 1024,
            "in_block_size": DEFAULT_POINTS * 4,
            "batch_size": 1,
            "timeout_ms": 50,
        }


buffer = []


@sharedmem_consumer(get_params())
def on_spectrum_batch(batch):
    global buffer
    for b in batch:
        arr = np.frombuffer(b, dtype=np.float32)
        buffer = arr.tolist()  # 只保留最新一帧


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        # 首包推送参数信息
        await websocket.send_json(
            {
                "freqStart": DEFAULT_FREQ_START,
                "freqEnd": DEFAULT_FREQ_END,
                "points": DEFAULT_POINTS,
            }
        )
        while True:
            if buffer:
                arr = np.array(buffer, dtype=np.float32)
                await websocket.send_bytes(arr.tobytes())
                # print(time.time())
            await asyncio.sleep(0.025)
    except WebSocketDisconnect:
        print("WebSocket 客户端断开连接")


if __name__ == "__main__":
    import uvicorn

    print("WebSocket 频谱共享内存转发服务已启动，端口 9000，路径 /ws")
    uvicorn.run("spectrum_shm_ws_server:app", host="0.0.0.0", port=9000, reload=False)
