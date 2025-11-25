/**
 * @file test_multicast_simple2.cpp
 * @brief 简化的广播模式测试（验证 Buffer 分配和 push）
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <iostream>

using namespace multiqueue;

TEST(SimpleMulticast2, BasicPushPop) {
    const char* registry_name = "test_simple2_registry";
    const char* queue_name = "test_simple2_queue";
    
    // 清理旧的共享内存
    shared_memory_object::remove(registry_name);
    shared_memory_object::remove(queue_name);
    
    // 创建 GlobalRegistry
    shared_memory_object registry_shm(create_only, registry_name, read_write);
    registry_shm.truncate(sizeof(GlobalRegistry));
    mapped_region registry_region(registry_shm, read_write);
    GlobalRegistry* registry = new (registry_region.get_address()) GlobalRegistry();
    registry->initialize();  // 初始化 GlobalRegistry
    
    // 创建并初始化 ShmManager
    ProcessId process_id = 1;
    ShmConfig config;
    config.name_prefix = "mqshm_";
    config.pools = {
        PoolConfig("small", 4096, 100),
        PoolConfig("medium", 65536, 50)
    };
    ShmManager manager(registry, process_id, config);
    ASSERT_TRUE(manager.initialize());
    
    // 分配 Buffer
    std::cout << "Allocating buffer..." << std::endl;
    auto buffer_ptr = manager.allocate(64);
    ASSERT_TRUE(buffer_ptr.valid()) << "Buffer allocation failed!";
    BufferId buffer_id = buffer_ptr.id();
    std::cout << "Allocated buffer_id: " << buffer_id << std::endl;
    
    // 创建 PortQueue
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(manager.allocator());
    
    // 注册消费者
    ConsumerId consumer = queue.register_consumer();
    ASSERT_NE(consumer, INVALID_CONSUMER_ID);
    std::cout << "Registered consumer: " << consumer << std::endl;
    
    // 生产 Buffer
    std::cout << "Pushing buffer_id " << buffer_id << " to queue..." << std::endl;
    ASSERT_TRUE(queue.push(buffer_id)) << "Push failed!";
    
    // 检查队列状态
    std::cout << "Queue size for consumer: " << queue.size(consumer) << std::endl;
    EXPECT_EQ(queue.size(consumer), 1);
    
    // 消费 Buffer
    BufferId consumed_buffer;
    ASSERT_TRUE(queue.pop(consumer, consumed_buffer)) << "Pop failed!";
    EXPECT_EQ(consumed_buffer, buffer_id);
    std::cout << "Consumed buffer_id: " << consumed_buffer << std::endl;
    
    // 验证队列为空
    EXPECT_EQ(queue.size(consumer), 0);
    
    // 清理
    queue.unregister_consumer(consumer);
    shared_memory_object::remove(registry_name);
    shared_memory_object::remove(queue_name);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

