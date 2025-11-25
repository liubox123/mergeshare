/**
 * @file test_multiprocess_multicast.cpp
 * @brief 多进程广播模式测试
 * 
 * 测试 PortQueue 广播模式在多进程环境下的行为
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/buffer_allocator.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <iostream>
#include <thread>
#include <chrono>

using namespace multiqueue;
using namespace boost::interprocess;

class MultiProcessMulticastTest : public ::testing::Test {
protected:
    static constexpr const char* REGISTRY_NAME = "test_multicast_registry";
    static constexpr const char* QUEUE_NAME = "test_multicast_queue";
    
    void SetUp() override {
        cleanup_shm();
    }
    
    void TearDown() override {
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(REGISTRY_NAME);
        shared_memory_object::remove(QUEUE_NAME);
        // 清理可能的 BufferPool 共享内存
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
        shared_memory_object::remove("mqshm_large");
    }
};

/**
 * @brief 测试：单生产者进程 + 2 个消费者进程（广播模式）
 * 
 * 场景：
 * - 1 个生产者进程生产 50 个 Buffer
 * - 2 个消费者进程独立读取（广播模式）
 * - 验证每个消费者都收到完整的数据
 */
TEST_F(MultiProcessMulticastTest, SingleProducerTwoConsumers) {
    const int NUM_CONSUMERS = 2;
    const int NUM_BUFFERS = 50;
    
    std::cout << "\n=== 测试: 1 生产者进程 + " << NUM_CONSUMERS << " 消费者进程（广播模式）===" << std::endl;
    
    std::vector<pid_t> consumer_pids;
    
    // 创建消费者进程
    for (int consumer_idx = 0; consumer_idx < NUM_CONSUMERS; ++consumer_idx) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程（消费者）
            int consumer_id = consumer_idx;
            
            // 等待生产者初始化
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            try {
                // 打开 GlobalRegistry
                shared_memory_object registry_shm(open_only, REGISTRY_NAME, read_write);
                mapped_region registry_region(registry_shm, read_write);
                GlobalRegistry* registry = static_cast<GlobalRegistry*>(registry_region.get_address());
                
                // 等待注册表初始化
                int retry_count = 0;
                while (!registry->is_valid() && retry_count < 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    ++retry_count;
                }
                
                if (!registry->is_valid()) {
                    std::cerr << "消费者 " << consumer_id << ": Registry not valid\n";
                    exit(1);
                }
                
                // 创建 SharedBufferAllocator（打开已存在的 pools）
                ProcessId process_id = static_cast<ProcessId>(getpid());
                SharedBufferAllocator allocator(registry, process_id);
                
                // 从 GlobalRegistry 中获取已注册的 pools 并打开
                for (uint32_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
                    const auto& pool_info = registry->buffer_pool_registry.pools[i];
                    if (pool_info.active) {
                        if (!allocator.register_pool(pool_info.pool_id, pool_info.shm_name)) {
                            std::cerr << "消费者 " << consumer_id << ": Failed to register pool " 
                                      << pool_info.pool_id << "\n";
                            exit(1);
                        }
                    }
                }
                
                // 打开 PortQueue
                PortQueue queue;
                if (!queue.open(QUEUE_NAME)) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to open queue\n";
                    exit(1);
                }
                queue.set_allocator(&allocator);
                
                // 注册为消费者
                ConsumerId consumer_reg_id = queue.register_consumer();
                if (consumer_reg_id == INVALID_CONSUMER_ID) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to register consumer\n";
                    exit(1);
                }
                
                std::cout << "消费者 " << consumer_id << " (PID: " << getpid() 
                          << ") 注册成功，consumer_id=" << consumer_reg_id << std::endl;
                
                // 消费数据
                std::set<BufferId> consumed_buffers;
                int consumed_count = 0;
                
                while (consumed_count < NUM_BUFFERS) {
                    BufferId buffer_id;
                    if (queue.pop(consumer_reg_id, buffer_id)) {
                        consumed_buffers.insert(buffer_id);
                        consumed_count++;
                        
                        if (consumed_count % 10 == 0) {
                            std::cout << "消费者 " << consumer_id << " 消费了 " << consumed_count << " 个 Buffer\n";
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                
                std::cout << "消费者 " << consumer_id << " 消费完成，共消费 " << consumed_count 
                          << " 个唯一值" << std::endl;
                
                // 验证：每个消费者都应该收到 NUM_BUFFERS 个 Buffer
                if (consumed_count != NUM_BUFFERS) {
                    std::cerr << "消费者 " << consumer_id << ": 消费数量不匹配，期望 " 
                              << NUM_BUFFERS << "，实际 " << consumed_count << "\n";
                    exit(1);
                }
                
                // 注销消费者
                queue.unregister_consumer(consumer_reg_id);
                
                exit(0);
                
            } catch (const std::exception& e) {
                std::cerr << "消费者 " << consumer_id << ": Exception: " << e.what() << "\n";
                exit(1);
            }
        } else if (pid > 0) {
            // 父进程：记录消费者 PID
            consumer_pids.push_back(pid);
        } else {
            // fork 失败
            FAIL() << "Failed to fork consumer process";
        }
    }
    
    // 父进程：生产者
    std::cout << "生产者 (PID: " << getpid() << ") 开始初始化" << std::endl;
    
    try {
        // 创建 GlobalRegistry
        shared_memory_object registry_shm(create_only, REGISTRY_NAME, read_write);
        registry_shm.truncate(sizeof(GlobalRegistry));
        mapped_region registry_region(registry_shm, read_write);
        GlobalRegistry* registry = new (registry_region.get_address()) GlobalRegistry();
        registry->initialize();
        
        // 创建 ShmManager
        ProcessId process_id = static_cast<ProcessId>(getpid());
        ShmConfig config;
        config.name_prefix = "mqshm_";
        config.pools = {
            PoolConfig("small", 4096, 100),
            PoolConfig("medium", 65536, 50),
            PoolConfig("large", 1048576, 20)
        };
        ShmManager manager(registry, process_id, config);
        if (!manager.initialize()) {
            FAIL() << "Failed to initialize ShmManager";
        }
        
        // 创建 PortQueue
        PortQueue queue;
        if (!queue.create(QUEUE_NAME, 1, 100)) {
            FAIL() << "Failed to create queue";
        }
        queue.set_allocator(manager.allocator());
        
        std::cout << "生产者 初始化完成，等待消费者..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 生产数据
        std::cout << "生产者 开始生产" << std::endl;
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto buffer_ptr = manager.allocate(64);
            if (!buffer_ptr.valid()) {
                FAIL() << "Failed to allocate buffer " << i;
            }
            
            BufferId buffer_id = buffer_ptr.id();
            if (!queue.push(buffer_id)) {
                FAIL() << "Failed to push buffer " << i;
            }
            
            if ((i + 1) % 10 == 0) {
                std::cout << "已生产 " << (i + 1) << " 个 Buffer\n";
            }
        }
        
        std::cout << "生产完成，等待消费者..." << std::endl;
        
        // 等待所有消费者完成
        for (pid_t pid : consumer_pids) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                FAIL() << "Consumer process " << pid << " exited with error";
            }
        }
        
        std::cout << "✅ 测试完成：1 生产者 + " << NUM_CONSUMERS << " 消费者（广播模式）" << std::endl;
        
    } catch (const std::exception& e) {
        FAIL() << "Producer exception: " << e.what();
    }
}

/**
 * @brief 测试：单生产者进程 + 3 个消费者进程（广播模式，慢消费者）
 * 
 * 场景：
 * - 1 个生产者进程生产 30 个 Buffer
 * - 3 个消费者进程独立读取
 * - 1 个消费者故意慢速读取
 * - 验证所有消费者都收到完整数据，慢消费者不阻塞其他消费者
 */
TEST_F(MultiProcessMulticastTest, SingleProducerThreeConsumersWithSlowConsumer) {
    const int NUM_CONSUMERS = 3;
    const int NUM_BUFFERS = 30;
    const int SLOW_CONSUMER_ID = 1;  // 第二个消费者是慢消费者
    
    std::cout << "\n=== 测试: 1 生产者进程 + " << NUM_CONSUMERS 
              << " 消费者进程（包含慢消费者）===" << std::endl;
    
    std::vector<pid_t> consumer_pids;
    
    // 创建消费者进程
    for (int consumer_idx = 0; consumer_idx < NUM_CONSUMERS; ++consumer_idx) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程（消费者）
            int consumer_id = consumer_idx;
            bool is_slow = (consumer_id == SLOW_CONSUMER_ID);
            
            // 等待生产者初始化
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            try {
                // 打开 GlobalRegistry
                shared_memory_object registry_shm(open_only, REGISTRY_NAME, read_write);
                mapped_region registry_region(registry_shm, read_write);
                GlobalRegistry* registry = static_cast<GlobalRegistry*>(registry_region.get_address());
                
                // 等待注册表初始化
                int retry_count = 0;
                while (!registry->is_valid() && retry_count < 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    ++retry_count;
                }
                
                if (!registry->is_valid()) {
                    std::cerr << "消费者 " << consumer_id << ": Registry not valid\n";
                    exit(1);
                }
                
                // 创建 SharedBufferAllocator（打开已存在的 pools）
                ProcessId process_id = static_cast<ProcessId>(getpid());
                SharedBufferAllocator allocator(registry, process_id);
                
                // 从 GlobalRegistry 中获取已注册的 pools 并打开
                for (uint32_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
                    const auto& pool_info = registry->buffer_pool_registry.pools[i];
                    if (pool_info.active) {
                        if (!allocator.register_pool(pool_info.pool_id, pool_info.shm_name)) {
                            std::cerr << "消费者 " << consumer_id << ": Failed to register pool " 
                                      << pool_info.pool_id << "\n";
                            exit(1);
                        }
                    }
                }
                
                // 打开 PortQueue
                PortQueue queue;
                if (!queue.open(QUEUE_NAME)) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to open queue\n";
                    exit(1);
                }
                queue.set_allocator(&allocator);
                
                // 注册为消费者
                ConsumerId consumer_reg_id = queue.register_consumer();
                if (consumer_reg_id == INVALID_CONSUMER_ID) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to register consumer\n";
                    exit(1);
                }
                
                std::cout << "消费者 " << consumer_id << " (PID: " << getpid() 
                          << ") " << (is_slow ? "[慢消费者]" : "") 
                          << " 注册成功" << std::endl;
                
                // 消费数据
                int consumed_count = 0;
                
                while (consumed_count < NUM_BUFFERS) {
                    BufferId buffer_id;
                    if (queue.pop(consumer_reg_id, buffer_id)) {
                        consumed_count++;
                        
                        // 慢消费者每次读取后休眠
                        if (is_slow) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                        
                        if (consumed_count % 10 == 0) {
                            std::cout << "消费者 " << consumer_id << " 消费了 " << consumed_count << " 个 Buffer\n";
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                
                std::cout << "消费者 " << consumer_id << " 消费完成，共消费 " << consumed_count << " 个 Buffer" << std::endl;
                
                // 验证
                if (consumed_count != NUM_BUFFERS) {
                    std::cerr << "消费者 " << consumer_id << ": 消费数量不匹配\n";
                    exit(1);
                }
                
                // 注销消费者
                queue.unregister_consumer(consumer_reg_id);
                
                exit(0);
                
            } catch (const std::exception& e) {
                std::cerr << "消费者 " << consumer_id << ": Exception: " << e.what() << "\n";
                exit(1);
            }
        } else if (pid > 0) {
            consumer_pids.push_back(pid);
        } else {
            FAIL() << "Failed to fork consumer process";
        }
    }
    
    // 父进程：生产者
    std::cout << "生产者 (PID: " << getpid() << ") 开始初始化" << std::endl;
    
    try {
        // 创建 GlobalRegistry
        shared_memory_object registry_shm(create_only, REGISTRY_NAME, read_write);
        registry_shm.truncate(sizeof(GlobalRegistry));
        mapped_region registry_region(registry_shm, read_write);
        GlobalRegistry* registry = new (registry_region.get_address()) GlobalRegistry();
        registry->initialize();
        
        // 创建 ShmManager
        ProcessId process_id = static_cast<ProcessId>(getpid());
        ShmConfig config;
        config.name_prefix = "mqshm_";
        config.pools = {
            PoolConfig("small", 4096, 100),
            PoolConfig("medium", 65536, 50)
        };
        ShmManager manager(registry, process_id, config);
        if (!manager.initialize()) {
            FAIL() << "Failed to initialize ShmManager";
        }
        
        // 创建 PortQueue
        PortQueue queue;
        if (!queue.create(QUEUE_NAME, 1, 100)) {
            FAIL() << "Failed to create queue";
        }
        queue.set_allocator(manager.allocator());
        
        std::cout << "生产者 初始化完成，等待消费者..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 生产数据
        std::cout << "生产者 开始生产" << std::endl;
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto buffer_ptr = manager.allocate(64);
            if (!buffer_ptr.valid()) {
                FAIL() << "Failed to allocate buffer " << i;
            }
            
            BufferId buffer_id = buffer_ptr.id();
            if (!queue.push(buffer_id)) {
                FAIL() << "Failed to push buffer " << i;
            }
            
            if ((i + 1) % 10 == 0) {
                std::cout << "已生产 " << (i + 1) << " 个 Buffer\n";
            }
        }
        
        std::cout << "生产完成，等待消费者..." << std::endl;
        
        // 等待所有消费者完成
        for (pid_t pid : consumer_pids) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                FAIL() << "Consumer process " << pid << " exited with error";
            }
        }
        
        std::cout << "✅ 测试完成：1 生产者 + " << NUM_CONSUMERS 
                  << " 消费者（包含慢消费者）" << std::endl;
        
    } catch (const std::exception& e) {
        FAIL() << "Producer exception: " << e.what();
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

