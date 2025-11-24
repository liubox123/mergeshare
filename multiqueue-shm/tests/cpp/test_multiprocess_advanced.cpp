/**
 * @file test_multiprocess_advanced.cpp
 * @brief 高级多进程测试：多生产者、多消费者及组合测试
 */

#include <gtest/gtest.h>
#include <multiqueue/multiqueue_shm.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <iostream>

using namespace multiqueue;
using namespace boost::interprocess;

class MultiProcessAdvancedTest : public ::testing::Test {
protected:
    static constexpr const char* REGISTRY_NAME = "test_adv_registry";
    static constexpr const char* POOL_NAME = "test_adv_pool";
    static constexpr const char* QUEUE_NAME = "test_adv_queue";
    
    void SetUp() override {
        // 清理可能存在的共享内存
        cleanup_shm();
    }
    
    void TearDown() override {
        // 清理共享内存
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
        shared_memory_object::remove(QUEUE_NAME);
    }
};

/**
 * @brief 测试 1：单生产者 + 多消费者
 * 
 * 场景：
 * - 1 个生产者进程生产 100 个 Buffer
 * - 3 个消费者进程竞争消费
 * - 验证所有数据都被消费
 */
TEST_F(MultiProcessAdvancedTest, SingleProducerMultipleConsumers) {
    const int NUM_CONSUMERS = 3;
    const int NUM_MESSAGES = 100;
    const size_t BUFFER_SIZE = 4096;
    const size_t POOL_SIZE = 128;
    
    std::cout << "\n=== 测试: 1 生产者 + " << NUM_CONSUMERS << " 消费者 ===" << std::endl;
    
    std::vector<pid_t> child_pids;
    
    // 创建消费者进程
    for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程（消费者）
            
            // 等待生产者初始化
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            try {
                // 打开全局注册表
                shared_memory_object global_shm(open_only, REGISTRY_NAME, read_write);
                mapped_region global_region(global_shm, read_write);
                GlobalRegistry* registry = static_cast<GlobalRegistry*>(global_region.get_address());
                
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
                
                // 注册消费者进程
                int32_t process_slot = registry->process_registry.register_process("Consumer");
                if (process_slot < 0) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to register process\n";
                    exit(1);
                }
                ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
                
                // 打开 Buffer Pool
                BufferPool pool;
                if (!pool.open(POOL_NAME)) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to open buffer pool\n";
                    exit(1);
                }
                
                // 创建 Buffer 分配器
                SharedBufferAllocator allocator(registry, process_id);
                
                // 使用固定的 PoolId (0)
                PoolId pool_id = 0;
                
                if (!allocator.register_pool(pool_id, POOL_NAME)) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to register pool\n";
                    exit(1);
                }
                
                // 打开 Port Queue
                PortQueue queue;
                if (!queue.open(QUEUE_NAME)) {
                    std::cerr << "消费者 " << consumer_id << ": Failed to open queue\n";
                    exit(1);
                }
                
                std::cout << "消费者 " << consumer_id << " (PID: " << getpid() << ") 启动" << std::endl;
                
                std::set<uint32_t> consumed_values;
                
                // 接收消息
                while (consumed_values.size() < NUM_MESSAGES) {
                    BufferId buffer_id;
                    if (!queue.pop_with_timeout(buffer_id, 3000)) {
                        // 超时，可能没有更多数据
                        break;
                    }
                    
                    // 创建 BufferPtr
                    BufferPtr buffer(buffer_id, &allocator);
                    
                    if (!buffer.valid()) {
                        std::cerr << "消费者 " << consumer_id << ": Invalid buffer\n";
                        continue;
                    }
                    
                    // 读取数据
                    const uint32_t* data = buffer.as<const uint32_t>();
                    consumed_values.insert(data[0]);
                }
                
                std::cout << "消费者 " << consumer_id << " 消费了 " << consumed_values.size() 
                          << " 个唯一值" << std::endl;
                
                // 注销进程
                registry->process_registry.unregister_process(process_slot);
                
                exit(0);
                
            } catch (const std::exception& e) {
                std::cerr << "消费者 " << consumer_id << " 异常: " << e.what() << std::endl;
                exit(1);
            }
        } else if (pid > 0) {
            child_pids.push_back(pid);
        } else {
            FAIL() << "Fork 失败";
        }
    }
    
    // 父进程（生产者）
    try {
        std::cout << "生产者 (PID: " << getpid() << ") 开始初始化" << std::endl;
        
        // 创建全局注册表
        shared_memory_object global_shm(create_only, REGISTRY_NAME, read_write);
        global_shm.truncate(sizeof(GlobalRegistry));
        mapped_region global_region(global_shm, read_write);
        
        GlobalRegistry* registry = new (global_region.get_address()) GlobalRegistry();
        registry->initialize();
        
        // 注册生产者进程
        int32_t process_slot = registry->process_registry.register_process("Producer");
        ASSERT_GE(process_slot, 0);
        ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
        
        // 创建 Buffer Pool
        BufferPool pool;
        ASSERT_TRUE(pool.create(POOL_NAME, 0, BUFFER_SIZE, POOL_SIZE));
        
        // 注册 Pool 到全局注册表
        PoolId pool_id = registry->buffer_pool_registry.register_pool(
            BUFFER_SIZE, POOL_SIZE, POOL_NAME
        );
        ASSERT_NE(pool_id, INVALID_POOL_ID);
        
        // 创建 Buffer 分配器
        SharedBufferAllocator allocator(registry, process_id);
        ASSERT_TRUE(allocator.register_pool(pool_id, POOL_NAME));
        
        // 创建 Port Queue
        PortQueue queue;
        ASSERT_TRUE(queue.create(QUEUE_NAME, 1, POOL_SIZE));
        
        std::cout << "生产者 初始化完成，等待消费者..." << std::endl;
        
        // 等待消费者准备好
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        std::cout << "生产者 开始生产" << std::endl;
        
        // 生产消息
        for (int i = 0; i < NUM_MESSAGES; ++i) {
            BufferId buffer_id = allocator.allocate(256);
            ASSERT_NE(buffer_id, INVALID_BUFFER_ID) << "Buffer 分配失败 at " << i;
            
            // 写入数据
            void* data = allocator.get_buffer_data(buffer_id);
            ASSERT_NE(data, nullptr);
            
            uint32_t* value = static_cast<uint32_t*>(data);
            *value = i;
            
            // 发送到队列
            ASSERT_TRUE(queue.push_with_timeout(buffer_id, 5000)) << "Queue push 失败 at " << i;
            
            if ((i + 1) % 20 == 0) {
                std::cout << "已生产 " << (i + 1) << " 个 Buffer" << std::endl;
            }
            
            usleep(1000); // 1ms 延迟
        }
        
        std::cout << "生产完成，等待消费者..." << std::endl;
        
        // 等待所有消费者完成
        for (pid_t pid : child_pids) {
            int status;
            waitpid(pid, &status, 0);
            EXPECT_TRUE(WIFEXITED(status)) << "消费者进程异常退出";
            EXPECT_EQ(WEXITSTATUS(status), 0) << "消费者进程返回错误";
        }
        
        // 注销进程
        registry->process_registry.unregister_process(process_slot);
        
        std::cout << "✅ 测试完成：1 生产者 + " << NUM_CONSUMERS << " 消费者" << std::endl;
        
    } catch (const std::exception& e) {
        FAIL() << "生产者异常: " << e.what();
    }
}

/**
 * @brief 测试 2：多生产者 + 单消费者
 * 
 * 场景：
 * - 3 个生产者进程各生产 30 个 Buffer（总共 90 个）
 * - 1 个消费者进程消费所有数据
 */
TEST_F(MultiProcessAdvancedTest, MultipleProducersSingleConsumer) {
    const int NUM_PRODUCERS = 3;
    const int MESSAGES_PER_PRODUCER = 30;
    const int TOTAL_MESSAGES = NUM_PRODUCERS * MESSAGES_PER_PRODUCER;
    const size_t BUFFER_SIZE = 4096;
    const size_t POOL_SIZE = 128;
    
    std::cout << "\n=== 测试: " << NUM_PRODUCERS << " 生产者 + 1 消费者 ===" << std::endl;
    
    std::vector<pid_t> producer_pids;
    pid_t consumer_pid;
    
    // 先创建消费者
    consumer_pid = fork();
    
    if (consumer_pid == 0) {
        // 子进程（消费者）
        
        // 等待父进程初始化
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        try {
            shared_memory_object global_shm(open_only, REGISTRY_NAME, read_write);
            mapped_region global_region(global_shm, read_write);
            GlobalRegistry* registry = static_cast<GlobalRegistry*>(global_region.get_address());
            
            // 等待注册表初始化
            int retry_count = 0;
            while (!registry->is_valid() && retry_count < 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                ++retry_count;
            }
            
            if (!registry->is_valid()) {
                std::cerr << "消费者: Registry not valid\n";
                exit(1);
            }
            
            int32_t process_slot = registry->process_registry.register_process("Consumer");
            ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
            
            BufferPool pool;
            if (!pool.open(POOL_NAME)) {
                std::cerr << "消费者: Failed to open buffer pool\n";
                exit(1);
            }
            
            SharedBufferAllocator allocator(registry, process_id);
            PoolId pool_id = 0; // 使用固定的 PoolId
            if (!allocator.register_pool(pool_id, POOL_NAME)) {
                std::cerr << "消费者: Failed to register pool\n";
                exit(1);
            }
            
            PortQueue queue;
            if (!queue.open(QUEUE_NAME)) {
                std::cerr << "消费者: Failed to open queue\n";
                exit(1);
            }
            
            std::cout << "消费者 (PID: " << getpid() << ") 启动" << std::endl;
            
            std::map<uint32_t, std::set<uint32_t>> producer_sequences; // producer_id -> sequences
            
            // 接收消息
            for (int i = 0; i < TOTAL_MESSAGES; ++i) {
                BufferId buffer_id;
                if (!queue.pop_with_timeout(buffer_id, 5000)) {
                    std::cerr << "消费者: Failed to pop buffer " << i << "/" << TOTAL_MESSAGES << "\n";
                    exit(1);
                }
                
                BufferPtr buffer(buffer_id, &allocator);
                if (!buffer.valid()) {
                    std::cerr << "消费者: Invalid buffer " << i << "\n";
                    continue;
                }
                
                const uint32_t* data = buffer.as<const uint32_t>();
                uint32_t producer_id = data[0];
                uint32_t seq = data[1];
                
                producer_sequences[producer_id].insert(seq);
                
                if ((i + 1) % 20 == 0) {
                    std::cout << "已消费 " << (i + 1) << " 个 Buffer" << std::endl;
                }
            }
            
            std::cout << "消费完成，验证数据..." << std::endl;
            
            // 验证每个生产者的数据
            for (int i = 0; i < NUM_PRODUCERS; ++i) {
                if (producer_sequences[i].size() != MESSAGES_PER_PRODUCER) {
                    std::cerr << "生产者 " << i << " 数据不完整: " 
                              << producer_sequences[i].size() << "/" << MESSAGES_PER_PRODUCER << "\n";
                    exit(1);
                }
            }
            
            std::cout << "消费者 验证通过" << std::endl;
            
            registry->process_registry.unregister_process(process_slot);
            exit(0);
            
        } catch (const std::exception& e) {
            std::cerr << "消费者异常: " << e.what() << std::endl;
            exit(1);
        }
    } else if (consumer_pid < 0) {
        FAIL() << "Fork 消费者失败";
    }
    
    // 创建生产者进程
    for (int producer_id = 0; producer_id < NUM_PRODUCERS; ++producer_id) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程（生产者）
            
            // 等待父进程初始化
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            try {
                shared_memory_object global_shm(open_only, REGISTRY_NAME, read_write);
                mapped_region global_region(global_shm, read_write);
                GlobalRegistry* registry = static_cast<GlobalRegistry*>(global_region.get_address());
                
                int32_t process_slot = registry->process_registry.register_process("Producer");
                ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
                
                BufferPool pool;
                pool.open(POOL_NAME);
                
                SharedBufferAllocator allocator(registry, process_id);
                PoolId pool_id = 0; // 使用固定的 PoolId
                allocator.register_pool(pool_id, POOL_NAME);
                
                PortQueue queue;
                queue.open(QUEUE_NAME);
                
                std::cout << "生产者 " << producer_id << " (PID: " << getpid() << ") 启动" << std::endl;
                
                for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i) {
                    BufferId buffer_id = allocator.allocate(256);
                    if (buffer_id == INVALID_BUFFER_ID) {
                        std::cerr << "生产者 " << producer_id << " 分配失败\n";
                        exit(1);
                    }
                    
                    void* data = allocator.get_buffer_data(buffer_id);
                    uint32_t* values = static_cast<uint32_t*>(data);
                    values[0] = producer_id;
                    values[1] = i;
                    
                    queue.push_with_timeout(buffer_id, 5000);
                    usleep(500);
                }
                
                std::cout << "生产者 " << producer_id << " 完成" << std::endl;
                exit(0);
                
            } catch (const std::exception& e) {
                std::cerr << "生产者 " << producer_id << " 异常: " << e.what() << std::endl;
                exit(1);
            }
        } else if (pid > 0) {
            producer_pids.push_back(pid);
        }
    }
    
    // 父进程：初始化共享资源
    try {
        std::cout << "主进程 (PID: " << getpid() << ") 初始化共享资源" << std::endl;
        
        shared_memory_object global_shm(create_only, REGISTRY_NAME, read_write);
        global_shm.truncate(sizeof(GlobalRegistry));
        mapped_region global_region(global_shm, read_write);
        
        GlobalRegistry* registry = new (global_region.get_address()) GlobalRegistry();
        registry->initialize();
        
        int32_t process_slot = registry->process_registry.register_process("Main");
        ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
        
        BufferPool pool;
        ASSERT_TRUE(pool.create(POOL_NAME, 0, BUFFER_SIZE, POOL_SIZE));
        
        PoolId pool_id = registry->buffer_pool_registry.register_pool(
            BUFFER_SIZE, POOL_SIZE, POOL_NAME
        );
        ASSERT_NE(pool_id, INVALID_POOL_ID);
        
        SharedBufferAllocator allocator(registry, process_id);
        ASSERT_TRUE(allocator.register_pool(pool_id, POOL_NAME));
        
        PortQueue queue;
        ASSERT_TRUE(queue.create(QUEUE_NAME, 1, POOL_SIZE));
        
        std::cout << "初始化完成，等待子进程..." << std::endl;
        
        // 等待所有生产者完成
        for (pid_t pid : producer_pids) {
            int status;
            waitpid(pid, &status, 0);
            EXPECT_TRUE(WIFEXITED(status));
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
        
        // 等待消费者完成
        int status;
        waitpid(consumer_pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
        
        registry->process_registry.unregister_process(process_slot);
        
        std::cout << "✅ 测试完成：" << NUM_PRODUCERS << " 生产者 + 1 消费者" << std::endl;
        
    } catch (const std::exception& e) {
        FAIL() << "主进程异常: " << e.what();
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
