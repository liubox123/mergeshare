// testr.cpp
#include "shared_ring_queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

void producer_test(const std::string& shm_name, size_t shm_size, uint32_t queue_len, uint32_t data_block_size, uint32_t total_refs, const std::string& metadata, size_t msg_count) {
    SharedRingQueueProducer queue(shm_name, queue_len, data_block_size, total_refs, metadata);
    std::vector<char> msg(data_block_size, 'A');
    auto start = std::chrono::steady_clock::now();
    size_t pushed = 0;
    for (size_t i = 0; i < msg_count; ++i) {
        while (!queue.push(msg.data(), data_block_size)) {
            std::this_thread::yield();
        }
        ++pushed;
        printf("pushed: %zu\n", pushed);
        if (pushed % 100000 == 0) std::cout << "pushed: " << pushed << std::endl;
    }
    auto end = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();
    std::cout << "Producer pushed " << pushed << " msgs, " << (pushed/sec) << "/s" << std::endl;
}

void consumer_test(const std::string& shm_name, size_t shm_size, uint32_t queue_len, uint32_t data_block_size, size_t msg_count) {
    SharedRingQueueConsumer queue(shm_name, queue_len, data_block_size);
    std::vector<char> buf(data_block_size);
    size_t popped = 0;
    auto start = std::chrono::steady_clock::now();
    while (popped < msg_count) {
        uint32_t sz = 0;
        if (queue.pop(buf.data(), sz)) {
            ++popped;
            printf("popped: %zu\n", popped);
            if (popped % 100000 == 0) std::cout << "popped: " << popped << std::endl;
        } else {
            std::this_thread::yield();
        }
    }
    auto end = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();
    std::cout << "Consumer popped " << popped << " msgs, " << (popped/sec) << "/s" << std::endl;
}

int main(int argc, char* argv[]) {

    // ./testr producer 10000000 10240 RingQueueSharedMemory44 16777216 1024 1 test meta
    // ./testr consumer 10000000 10240 RingQueueSharedMemory44 16777216 1024 1 test meta
    std::string role = "producer";
    size_t msg_count = 1000000;
    size_t msg_size = 128;
    std::string shm_name = "RingQueueSharedMemory44";
    size_t shm_size = 16 * 1024 * 1024;
    uint32_t queue_len = 1024;
    uint32_t total_refs = 1;
    std::string metadata = "test meta";
    if (argc >= 2) role = argv[1];
    if (argc >= 3) msg_count = std::stoull(argv[2]);
    if (argc >= 4) msg_size = std::stoull(argv[3]);
    if (argc >= 5) shm_name = argv[4];
    if (argc >= 6) shm_size = std::stoull(argv[5]);
    if (argc >= 7) queue_len = std::stoul(argv[6]);
    if (argc >= 8) total_refs = std::stoul(argv[7]);
    if (argc >= 9) metadata = argv[8];
    if (msg_size + sizeof(Node) - 1 > shm_size / queue_len) {
        std::cerr << "msg_size too large for given shm_size/queue_len" << std::endl;
        return 1;
    }
    if (role == "producer") {
        producer_test(shm_name, shm_size, queue_len, msg_size, total_refs, metadata, msg_count);
    } else if (role == "consumer") {
        consumer_test(shm_name, shm_size, queue_len, msg_size, msg_count);
    } else {
        std::cerr << "Unknown role: " << role << std::endl;
        return 1;
    }
    return 0;
}