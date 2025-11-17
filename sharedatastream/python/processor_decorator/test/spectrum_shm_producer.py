import numpy as np
import time
import sys
from processor_decorator import sharedmem_producer
import random

DEFAULT_FREQ_START = 0  # 0 Hz
DEFAULT_FREQ_END = 8e9  # 8 GHz
DEFAULT_POINTS = int((DEFAULT_FREQ_END - DEFAULT_FREQ_START) / 50000)  # 默认点数
NOISE_FLOOR = -120


def get_params():
    if len(sys.argv) >= 5:
        return {
            "out_shm": sys.argv[1],
            "out_queue_len": int(sys.argv[2]),
            "out_block_size": int(sys.argv[3]),
            "interval": float(sys.argv[4]),
        }
    else:
        return {
            "out_shm": "RingQueueSpectrum",
            "out_queue_len": 1024,
            "out_block_size": DEFAULT_POINTS * 4,  # float32
            "interval": 0.025,
        }


# 信号池，管理信号生命周期
global_active_signals = []


@sharedmem_producer(get_params())
def spectrum_producer():
    global global_active_signals
    points = DEFAULT_POINTS
    # 1. 更新信号池生命周期
    for sig in global_active_signals:
        sig["remain"] -= 1
    global_active_signals = [sig for sig in global_active_signals if sig["remain"] > 0]
    # 2. 随机补充新信号
    if len(global_active_signals) < 3 and random.random() < 0.1:
        modulation = random.choice(["am", "fm", "qam", "ofdm", "pulse", "carrier"])
        center_idx = random.randint(int(points * 0.1), int(points * 0.9))
        bw = random.randint(int(points * 0.002), int(points * 0.02))
        amp = random.uniform(-60, -20)
        remain = random.randint(40 * 4, 80 * 4)  # 1~2秒
        global_active_signals.append(
            {
                "center_idx": center_idx,
                "bw": bw,
                "amp": amp,
                "modulation": modulation,
                "remain": remain,
            }
        )
    # 3. 生成底噪
    spectrum = [NOISE_FLOOR for _ in range(points)]
    # 4. 合成信号并加抖动
    for sig in global_active_signals:
        mod = sig.get("modulation", "carrier")
        if mod == "am":
            for i in range(
                max(0, sig["center_idx"] - sig["bw"]),
                min(points, sig["center_idx"] + sig["bw"]),
            ):
                side = abs(i - sig["center_idx"])
                val = sig["amp"] - (side / sig["bw"]) * 20 + np.random.uniform(-3, 3)
                spectrum[i] = max(spectrum[i], val)
        elif mod == "fm":
            for i in range(
                max(0, sig["center_idx"] - sig["bw"]),
                min(points, sig["center_idx"] + sig["bw"]),
            ):
                val = (
                    sig["amp"]
                    - ((i - sig["center_idx"]) ** 2) / (2 * (sig["bw"] / 2) ** 2) * 20
                    + np.random.uniform(-3, 3)
                )
                spectrum[i] = max(spectrum[i], val)
        elif mod == "qam":
            for i in range(
                max(0, sig["center_idx"] - sig["bw"]),
                min(points, sig["center_idx"] + sig["bw"]),
            ):
                val = sig["amp"] + np.random.uniform(-5, 5)
                spectrum[i] = max(spectrum[i], val)
        elif mod == "ofdm":
            n_carriers = random.randint(4, 16)
            for k in range(n_carriers):
                carrier_idx = (
                    sig["center_idx"] - sig["bw"] // 2 + k * sig["bw"] // n_carriers
                )
                if 0 <= carrier_idx < points:
                    for i in range(
                        max(0, carrier_idx - 2), min(points, carrier_idx + 2)
                    ):
                        val = sig["amp"] + np.random.uniform(-2, 2)
                        spectrum[i] = max(spectrum[i], val)
        elif mod == "pulse":
            for i in range(
                max(0, sig["center_idx"] - sig["bw"]),
                min(points, sig["center_idx"] + sig["bw"]),
            ):
                val = sig["amp"] + np.random.uniform(-10, 10)
                spectrum[i] = max(spectrum[i], val)
        else:
            for i in range(
                max(0, sig["center_idx"] - 2), min(points, sig["center_idx"] + 2)
            ):
                val = sig["amp"] + np.random.uniform(-2, 2)
                spectrum[i] = max(spectrum[i], val)
    arr = np.array(spectrum, dtype=np.float32)
    return arr.tobytes()


if __name__ == "__main__":
    print(f"开始写入频谱数据到共享内存... 点数: {DEFAULT_POINTS}")
    while True:
        time.sleep(1)
