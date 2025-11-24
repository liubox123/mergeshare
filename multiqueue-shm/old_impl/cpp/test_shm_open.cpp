#include <multiqueue/ring_queue.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace multiqueue;

int main() {
    std::string queue_name = "test_shm_open";
    
    std::cerr << "[MAIN] Creating producer queue..." << std::endl;
    RingQueue<int> producer_queue(queue_name, QueueConfig(1024));
    std::cerr << "[MAIN] Producer queue created" << std::endl;
    
    std::cerr << "[MAIN] Sleeping 1s..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cerr << "[MAIN] Creating consumer thread..." << std::endl;
    std::thread consumer_thread([&queue_name]() {
        std::cerr << "[THREAD] Creating consumer queue..." << std::endl;
        try {
            RingQueue<int> consumer_queue(queue_name, QueueConfig(1024));
            std::cerr << "[THREAD] Consumer queue created!" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[THREAD] ERROR: " << e.what() << std::endl;
        }
    });
    
    std::cerr << "[MAIN] Waiting for thread..." << std::endl;
    consumer_thread.join();
    std::cerr << "[MAIN] Done!" << std::endl;
    
    return 0;
}

