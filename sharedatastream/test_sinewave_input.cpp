#include "shared_ring_queue.hpp"
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    const std::string shm_name = "RingQueueSineWave";
    const uint32_t queue_len = 1024;
    const uint32_t block_size = 12800; // 3200 float32
    SharedRingQueueConsumer consumer(shm_name, queue_len, block_size);
    std::vector<char> buf(block_size);
    size_t count = 0;
    std::cout << "开始从共享内存读取正弦/余弦变形波..." << std::endl;
    while (true) {
        uint32_t sz = 0;
        if (consumer.pop(buf.data(), sz)) {
            ++count;
            if (count % 20 == 0) {
                std::cout << "received count: " << count << ", size: " << sz << std::endl;
            }
        }
        // 可适当sleep降低CPU占用
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
} 