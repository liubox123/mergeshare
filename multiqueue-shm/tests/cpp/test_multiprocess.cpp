/**
 * @file test_multiprocess.cpp
 * @brief 多进程集成测试
 * 
 * 测试完整的多进程数据流：
 * - 进程 A（生产者）：创建 GlobalRegistry、BufferPool、PortQueue，分配 Buffer 并发送
 * - 进程 B（消费者）：打开共享内存，接收 Buffer 并验证数据
 */

#include <multiqueue/multiqueue_shm.hpp>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>

using namespace multiqueue;
using namespace boost::interprocess;

class MultiProcessTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_mp_global_registry";
    static constexpr const char* POOL_NAME = "test_mp_pool";
    static constexpr const char* QUEUE_NAME = "test_mp_queue";
    
    void SetUp() override {
        // 清理旧的共享内存
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
        shared_memory_object::remove(QUEUE_NAME);
    }
    
    void TearDown() override {
        // 清理共享内存
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
        shared_memory_object::remove(QUEUE_NAME);
    }
};

TEST_F(MultiProcessTest, ProducerConsumer) {
    constexpr size_t NUM_MESSAGES = 10;
    constexpr size_t BLOCK_SIZE = 4096;
    constexpr size_t BLOCK_COUNT = 16;
    constexpr size_t QUEUE_CAPACITY = 8;
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // ===== 子进程：消费者 =====
        
        // 等待生产者初始化
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        try {
            // 打开全局注册表
            shared_memory_object global_shm(open_only, GLOBAL_REGISTRY_NAME, read_write);
            mapped_region global_region(global_shm, read_write);
            GlobalRegistry* registry = static_cast<GlobalRegistry*>(global_region.get_address());
            
            // 等待注册表初始化
            int retry_count = 0;
            while (!registry->is_valid() && retry_count < 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                ++retry_count;
            }
            
            if (!registry->is_valid()) {
                std::cerr << "Consumer: Registry not valid after waiting\n";
                exit(1);
            }
            
            // 注册消费者进程
            int32_t process_slot = registry->process_registry.register_process("Consumer");
            if (process_slot < 0) {
                std::cerr << "Consumer: Failed to register process\n";
                exit(1);
            }
            ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
            
            // 打开 Buffer Pool
            BufferPool pool;
            if (!pool.open(POOL_NAME)) {
                std::cerr << "Consumer: Failed to open buffer pool\n";
                exit(1);
            }
            
            // 创建 Buffer 分配器
            SharedBufferAllocator allocator(registry, process_id);
            if (!allocator.register_pool(0, POOL_NAME)) {
                std::cerr << "Consumer: Failed to register pool\n";
                exit(1);
            }
            
            // 打开 Port Queue
            PortQueue queue;
            if (!queue.open(QUEUE_NAME)) {
                std::cerr << "Consumer: Failed to open queue\n";
                exit(1);
            }
            
            // 接收并验证消息
            for (size_t i = 0; i < NUM_MESSAGES; ++i) {
                BufferId buffer_id;
                if (!queue.pop_with_timeout(buffer_id, 5000)) {
                    std::cerr << "Consumer: Failed to pop buffer " << i << "\n";
                    exit(1);
                }
                
                // 创建 BufferPtr（会增加引用计数）
                BufferPtr buffer(buffer_id, &allocator);
                
                if (!buffer.valid()) {
                    std::cerr << "Consumer: Invalid buffer " << i << "\n";
                    exit(1);
                }
                
                // 验证数据
                const char* data = buffer.as<const char>();
                char expected[64];
                snprintf(expected, sizeof(expected), "Message %zu", i);
                
                if (strcmp(data, expected) != 0) {
                    std::cerr << "Consumer: Data mismatch at " << i 
                             << " expected: " << expected 
                             << " got: " << data << "\n";
                    exit(1);
                }
                
                // BufferPtr 析构时自动减少引用计数
            }
            
            // 注销进程
            registry->process_registry.unregister_process(process_slot);
            
            exit(0);  // 成功
            
        } catch (const std::exception& e) {
            std::cerr << "Consumer exception: " << e.what() << "\n";
            exit(1);
        }
    } else if (pid > 0) {
        // ===== 父进程：生产者 =====
        
        try {
            // 创建全局注册表
            shared_memory_object global_shm(
                create_only,
                GLOBAL_REGISTRY_NAME,
                read_write
            );
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
            ASSERT_TRUE(pool.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
            
            // 注册 Pool 到全局注册表
            PoolId pool_id = registry->buffer_pool_registry.register_pool(
                BLOCK_SIZE, BLOCK_COUNT, POOL_NAME
            );
            ASSERT_NE(pool_id, INVALID_POOL_ID);
            
            // 创建 Buffer 分配器
            SharedBufferAllocator allocator(registry, process_id);
            ASSERT_TRUE(allocator.register_pool(pool_id, POOL_NAME));
            
            // 创建 Port Queue
            PortQueue queue;
            ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
            
            // 等待消费者准备好
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // 发送消息
            for (size_t i = 0; i < NUM_MESSAGES; ++i) {
                // 分配 Buffer
                BufferId buffer_id = allocator.allocate(256);
                ASSERT_NE(buffer_id, INVALID_BUFFER_ID);
                
                // 写入数据
                void* data = allocator.get_buffer_data(buffer_id);
                ASSERT_NE(data, nullptr);
                
                char message[64];
                snprintf(message, sizeof(message), "Message %zu", i);
                memcpy(data, message, strlen(message) + 1);
                
                // 发送到队列（引用计数由队列持有）
                ASSERT_TRUE(queue.push_with_timeout(buffer_id, 5000));
                
                // 减少本地引用计数
                allocator.remove_ref(buffer_id);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // 等待消费者完成
            int status;
            waitpid(pid, &status, 0);
            
            // 检查消费者是否成功退出
            ASSERT_TRUE(WIFEXITED(status));
            EXPECT_EQ(WEXITSTATUS(status), 0);
            
            // 注销进程
            registry->process_registry.unregister_process(process_slot);
            
        } catch (const std::exception& e) {
            FAIL() << "Producer exception: " << e.what();
        }
    } else {
        FAIL() << "Fork failed";
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

