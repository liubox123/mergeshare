#include "shared_ring_queue.hpp"
#include <cmath>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
//支持ctrl+c退出
void signal_handler(int signal) {
    std::cout << "Ctrl+C pressed, exiting..." << std::endl;
    exit(0);
}
int main() {
    const std::string shm_name = "RingQueueSineWave";
    const uint32_t queue_len = 1024;
    const uint32_t block_size = 12800; // 3200 float32
    const uint32_t total_refs = 1;
    const std::string metadata = "cpp sinewave";
    SharedRingQueueProducer producer(shm_name, queue_len, block_size, total_refs, metadata);

    int t = 0;
    const int fs = 1000; // 采样率
    const float freq = 1.0f; // 正弦波频率
    const float morph_period = 5.0f; // 形状变化周期（秒）
    int count = 0;
    std::cout << "开始写入正弦/余弦变形波到共享内存..." << std::endl;
    while (true) {
        float now = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        float alpha = 0.5f * (1.0f + std::sin(2 * M_PI * now / morph_period));
        std::vector<float> y(block_size / 4);
        for (size_t i = 0; i < y.size(); ++i) {
            float theta = 2 * M_PI * freq * (i + t) / fs;
            y[i] = (1 - alpha) * std::sin(theta) + alpha * std::cos(theta);
        }
        t += y.size();
        // 写入共享内存
        while (!producer.push(y.data(), y.size() * sizeof(float))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ++count;
        if (count % 20 == 0) {
            std::cout << "count: " << count << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
} 