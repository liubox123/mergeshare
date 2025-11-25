/**
 * @file test_multicast_simple.cpp
 * @brief 简化的广播模式测试（用于调试）
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>

using namespace multiqueue;

/**
 * @brief 测试：单生产者 + 2 消费者（广播模式）
 */
TEST(SimpleMulticastTest, TwoConsumers) {
    const char* registry_name = "test_simple_registry";
    const char* queue_name = "test_simple_queue";
    
    // 清理旧的共享内存
    shared_memory_object::remove(registry_name);
    shared_memory_object::remove(queue_name);
    
    // 创建 GlobalRegistry
    shared_memory_object registry_shm(create_only, registry_name, read_write);
    registry_shm.truncate(sizeof(GlobalRegistry));
    mapped_region registry_region(registry_shm, read_write);
    GlobalRegistry* registry = new (registry_region.get_address()) GlobalRegistry();
    
    // 创建并初始化 ShmManager
    ProcessId process_id = 1;
    ShmConfig config;
    config.pools = {
        PoolConfig("pool_small", 128, 10),
        PoolConfig("pool_medium", 1024, 10)
    };
    ShmManager shm_manager(registry, process_id, config);
    ASSERT_TRUE(shm_manager.initialize());
    
    // 分配 Buffer（使用 ShmManager）
    auto buffer_ptr = shm_manager.allocate(64);
    ASSERT_TRUE(buffer_ptr.valid());
    BufferId buffer_id = buffer_ptr.id();
    std::cout << "Allocated buffer_id: " << buffer_id << std::endl;
    
    // 创建 PortQueue
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    
    // 设置 allocator（使用 ShmManager 的内部 allocator）
    auto allocator = std::make_unique<SharedBufferAllocator>(registry, process_id);
    allocator->register_pool(0, (config.name_prefix + "pool_small").c_str());
    allocator->register_pool(1, (config.name_prefix + "pool_medium").c_str());
    queue.set_allocator(allocator.get());
    
    // 注册两个消费者
    ConsumerId consumer1 = queue.register_consumer();
    ConsumerId consumer2 = queue.register_consumer();
    ASSERT_NE(consumer1, INVALID_CONSUMER_ID);
    ASSERT_NE(consumer2, INVALID_CONSUMER_ID);
    
    std::cout << "Registered consumer1: " << consumer1 << std::endl;
    std::cout << "Registered consumer2: " << consumer2 << std::endl;
    std::cout << "Consumer count: " << queue.get_consumer_count() << std::endl;
    
    // 生产 1 个 Buffer
    std::cout << "Pushing buffer_id " << buffer_id << " to queue..." << std::endl;
    ASSERT_TRUE(queue.push(buffer_id));
    
    std::cout << "Queue size for consumer1: " << queue.size(consumer1) << std::endl;
    std::cout << "Queue size for consumer2: " << queue.size(consumer2) << std::endl;
    
    // 两个消费者都应该能看到这个 Buffer
    EXPECT_EQ(queue.size(consumer1), 1);
    EXPECT_EQ(queue.size(consumer2), 1);
    
    // 消费者 1 消费
    BufferId consumed_buffer1;
    ASSERT_TRUE(queue.pop(consumer1, consumed_buffer1));
    EXPECT_EQ(consumed_buffer1, buffer_id);
    std::cout << "Consumer1 consumed buffer_id: " << consumed_buffer1 << std::endl;
    
    // 消费者 1 队列为空，但消费者 2 还有数据
    EXPECT_EQ(queue.size(consumer1), 0);
    EXPECT_EQ(queue.size(consumer2), 1);
    
    // 消费者 2 消费
    BufferId consumed_buffer2;
    ASSERT_TRUE(queue.pop(consumer2, consumed_buffer2));
    EXPECT_EQ(consumed_buffer2, buffer_id);
    std::cout << "Consumer2 consumed buffer_id: " << consumed_buffer2 << std::endl;
    
    // 两个消费者都消费完
    EXPECT_EQ(queue.size(consumer1), 0);
    EXPECT_EQ(queue.size(consumer2), 0);
    
    // 清理
    queue.unregister_consumer(consumer1);
    queue.unregister_consumer(consumer2);
    shared_memory_object::remove(registry_name);
    shared_memory_object::remove(queue_name);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}



