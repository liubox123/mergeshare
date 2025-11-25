/**
 * @file test_port_queue_multicast.cpp
 * @brief PortQueue 广播模式测试
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/buffer_allocator.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/shm_manager.hpp>
#include <thread>
#include <vector>

using namespace multiqueue;

class PortQueueMulticastTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 GlobalRegistry
        const char* registry_name = "test_multicast_registry";
        shared_memory_object::remove(registry_name);
        
        registry_shm_ = shared_memory_object(
            create_only,
            registry_name,
            read_write
        );
        registry_shm_.truncate(sizeof(GlobalRegistry));
        
        registry_region_ = mapped_region(registry_shm_, read_write);
        registry_ = new (registry_region_.get_address()) GlobalRegistry();
        registry_->initialize();  // 初始化 GlobalRegistry
        
        // 创建 ShmManager 并配置池
        process_id_ = 1;
        ShmConfig config;
        config.name_prefix = "mqshm_";
        config.pools = {
            PoolConfig("small", 4096, 100),
            PoolConfig("medium", 65536, 50),
            PoolConfig("large", 1048576, 20)
        };
        shm_manager_ = std::make_unique<ShmManager>(registry_, process_id_, config);
        
        // 初始化（这会创建 pools 和 allocator）
        ASSERT_TRUE(shm_manager_->initialize());
        
        // 使用 ShmManager 内部的 allocator
        allocator_ = shm_manager_->allocator();
    }
    
    void TearDown() override {
        shm_manager_.reset();
        shared_memory_object::remove("test_multicast_registry");
    }
    
    shared_memory_object registry_shm_;
    mapped_region registry_region_;
    GlobalRegistry* registry_;
    ProcessId process_id_;
    std::unique_ptr<ShmManager> shm_manager_;
    SharedBufferAllocator* allocator_;  // 指向 ShmManager 内部的 allocator
};

/**
 * @brief 测试：单生产者 + 单消费者（基本功能）
 */
TEST_F(PortQueueMulticastTest, SingleProducerSingleConsumer) {
    const char* queue_name = "test_multicast_queue_1";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册消费者
    ConsumerId consumer_id = queue.register_consumer();
    ASSERT_NE(consumer_id, INVALID_CONSUMER_ID);
    
    // 分配 Buffer
    auto buffer_ptr = shm_manager_->allocate(64);
    ASSERT_TRUE(buffer_ptr.valid());
    BufferId buffer = buffer_ptr.id();
    
    // 生产
    ASSERT_TRUE(queue.push(buffer));
    
    // 验证队列状态
    EXPECT_EQ(queue.size(consumer_id), 1);
    EXPECT_FALSE(queue.empty(consumer_id));
    
    // 消费
    BufferId consumed_buffer;
    ASSERT_TRUE(queue.pop(consumer_id, consumed_buffer));
    EXPECT_EQ(consumed_buffer, buffer);
    
    // 验证队列为空
    EXPECT_EQ(queue.size(consumer_id), 0);
    EXPECT_TRUE(queue.empty(consumer_id));
    
    // 清理
    queue.unregister_consumer(consumer_id);
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：单生产者 + 2 消费者（广播模式）
 */
TEST_F(PortQueueMulticastTest, SingleProducerTwoConsumers) {
    const char* queue_name = "test_multicast_queue_2";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册两个消费者
    ConsumerId consumer1 = queue.register_consumer();
    ConsumerId consumer2 = queue.register_consumer();
    ASSERT_NE(consumer1, INVALID_CONSUMER_ID);
    ASSERT_NE(consumer2, INVALID_CONSUMER_ID);
    ASSERT_NE(consumer1, consumer2);
    
    // 分配 Buffer
    auto buffer_ptr = shm_manager_->allocate(64);
    ASSERT_TRUE(buffer_ptr.valid());
    BufferId buffer = buffer_ptr.id();
    
    // 生产 1 个 Buffer
    ASSERT_TRUE(queue.push(buffer));
    
    // 两个消费者都应该能看到这个 Buffer
    EXPECT_EQ(queue.size(consumer1), 1);
    EXPECT_EQ(queue.size(consumer2), 1);
    
    // 消费者 1 消费
    BufferId consumed_buffer1;
    ASSERT_TRUE(queue.pop(consumer1, consumed_buffer1));
    EXPECT_EQ(consumed_buffer1, buffer);
    
    // 消费者 1 队列为空，但消费者 2 还有数据
    EXPECT_EQ(queue.size(consumer1), 0);
    EXPECT_EQ(queue.size(consumer2), 1);
    
    // 消费者 2 消费
    BufferId consumed_buffer2;
    ASSERT_TRUE(queue.pop(consumer2, consumed_buffer2));
    EXPECT_EQ(consumed_buffer2, buffer);
    
    // 两个消费者都消费完
    EXPECT_EQ(queue.size(consumer1), 0);
    EXPECT_EQ(queue.size(consumer2), 0);
    
    // 清理
    queue.unregister_consumer(consumer1);
    queue.unregister_consumer(consumer2);
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：单生产者 + 3 消费者（广播模式，多个 Buffer）
 */
TEST_F(PortQueueMulticastTest, SingleProducerThreeConsumersMultipleBuffers) {
    const char* queue_name = "test_multicast_queue_3";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册三个消费者
    ConsumerId consumers[3];
    for (int i = 0; i < 3; ++i) {
        consumers[i] = queue.register_consumer();
        ASSERT_NE(consumers[i], INVALID_CONSUMER_ID);
    }
    
    // 生产 5 个 Buffer
    const int NUM_BUFFERS = 5;
    BufferId buffers[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        auto buffer_ptr = shm_manager_->allocate(64);
        ASSERT_TRUE(buffer_ptr.valid());
        buffers[i] = buffer_ptr.id();
        ASSERT_TRUE(queue.push(buffers[i]));
    }
    
    // 每个消费者都应该看到 5 个 Buffer
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(queue.size(consumers[i]), NUM_BUFFERS);
    }
    
    // 每个消费者独立消费
    for (int consumer_idx = 0; consumer_idx < 3; ++consumer_idx) {
        for (int buffer_idx = 0; buffer_idx < NUM_BUFFERS; ++buffer_idx) {
            BufferId consumed_buffer;
            ASSERT_TRUE(queue.pop(consumers[consumer_idx], consumed_buffer));
            EXPECT_EQ(consumed_buffer, buffers[buffer_idx]);
        }
        
        // 该消费者消费完毕
        EXPECT_EQ(queue.size(consumers[consumer_idx]), 0);
    }
    
    // 清理
    for (int i = 0; i < 3; ++i) {
        queue.unregister_consumer(consumers[i]);
    }
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：慢消费者不影响快消费者读取
 */
TEST_F(PortQueueMulticastTest, SlowConsumerDoesNotBlockFastConsumer) {
    const char* queue_name = "test_multicast_queue_4";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册两个消费者
    ConsumerId fast_consumer = queue.register_consumer();
    ConsumerId slow_consumer = queue.register_consumer();
    ASSERT_NE(fast_consumer, INVALID_CONSUMER_ID);
    ASSERT_NE(slow_consumer, INVALID_CONSUMER_ID);
    
    // 生产 3 个 Buffer
    const int NUM_BUFFERS = 3;
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        auto buffer_ptr = shm_manager_->allocate(64);
        ASSERT_TRUE(buffer_ptr.valid());
        BufferId buffer = buffer_ptr.id();
        ASSERT_TRUE(queue.push(buffer));
    }
    
    // 快消费者全部消费
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        BufferId buffer;
        ASSERT_TRUE(queue.pop(fast_consumer, buffer));
    }
    EXPECT_EQ(queue.size(fast_consumer), 0);
    
    // 慢消费者还有数据
    EXPECT_EQ(queue.size(slow_consumer), NUM_BUFFERS);
    
    // 慢消费者消费第一个
    BufferId buffer;
    ASSERT_TRUE(queue.pop(slow_consumer, buffer));
    EXPECT_EQ(queue.size(slow_consumer), NUM_BUFFERS - 1);
    
    // 清理
    queue.unregister_consumer(fast_consumer);
    queue.unregister_consumer(slow_consumer);
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：动态添加消费者
 */
TEST_F(PortQueueMulticastTest, DynamicConsumerRegistration) {
    const char* queue_name = "test_multicast_queue_5";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册第一个消费者
    ConsumerId consumer1 = queue.register_consumer();
    ASSERT_NE(consumer1, INVALID_CONSUMER_ID);
    
    // 生产 2 个 Buffer
    auto buffer1_ptr = shm_manager_->allocate(64);
    auto buffer2_ptr = shm_manager_->allocate(64);
    ASSERT_TRUE(buffer1_ptr.valid());
    ASSERT_TRUE(buffer2_ptr.valid());
    BufferId buffer1 = buffer1_ptr.id();
    BufferId buffer2 = buffer2_ptr.id();
    ASSERT_TRUE(queue.push(buffer1));
    ASSERT_TRUE(queue.push(buffer2));
    
    // 消费者 1 应该看到 2 个
    EXPECT_EQ(queue.size(consumer1), 2);
    
    // 注册第二个消费者（应该从当前 tail 开始，看不到已有的数据）
    ConsumerId consumer2 = queue.register_consumer();
    ASSERT_NE(consumer2, INVALID_CONSUMER_ID);
    
    // 消费者 2 看不到之前的数据
    EXPECT_EQ(queue.size(consumer2), 0);
    
    // 再生产一个 Buffer
    auto buffer3_ptr = shm_manager_->allocate(64);
    ASSERT_TRUE(buffer3_ptr.valid());
    BufferId buffer3 = buffer3_ptr.id();
    ASSERT_TRUE(queue.push(buffer3));
    
    // 消费者 1 看到 3 个，消费者 2 看到 1 个
    EXPECT_EQ(queue.size(consumer1), 3);
    EXPECT_EQ(queue.size(consumer2), 1);
    
    // 清理
    queue.unregister_consumer(consumer1);
    queue.unregister_consumer(consumer2);
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：消费者注销释放引用
 */
TEST_F(PortQueueMulticastTest, ConsumerUnregisterReleasesReferences) {
    const char* queue_name = "test_multicast_queue_6";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册消费者
    ConsumerId consumer = queue.register_consumer();
    ASSERT_NE(consumer, INVALID_CONSUMER_ID);
    
    // 分配并生产 Buffer
    BufferId buffer = allocator_->allocate(sizeof(BufferId));
    ASSERT_NE(buffer, INVALID_BUFFER_ID);
    ASSERT_TRUE(queue.push(buffer));
    
    // 获取引用计数
    int32_t slot = registry_->buffer_metadata_table.find_slot_by_id(buffer);
    ASSERT_GE(slot, 0);
    uint32_t ref_count_before = registry_->buffer_metadata_table.entries[slot].ref_count.load();
    EXPECT_EQ(ref_count_before, 1);  // 1 个消费者
    
    // 注销消费者（应该释放未消费的 Buffer 的引用）
    queue.unregister_consumer(consumer);
    
    // 引用计数应该减少到 0
    uint32_t ref_count_after = registry_->buffer_metadata_table.entries[slot].ref_count.load();
    EXPECT_EQ(ref_count_after, 0);
    
    // 清理
    shared_memory_object::remove(queue_name);
}

/**
 * @brief 测试：最大消费者数量限制
 */
TEST_F(PortQueueMulticastTest, MaxConsumersLimit) {
    const char* queue_name = "test_multicast_queue_7";
    
    // 创建队列
    PortQueue queue;
    ASSERT_TRUE(queue.create(queue_name, 1, 10));
    queue.set_allocator(allocator_);
    
    // 注册最大数量的消费者
    std::vector<ConsumerId> consumers;
    for (uint32_t i = 0; i < PortQueueHeader::MAX_CONSUMERS; ++i) {
        ConsumerId consumer = queue.register_consumer();
        ASSERT_NE(consumer, INVALID_CONSUMER_ID);
        consumers.push_back(consumer);
    }
    
    // 尝试注册超过最大数量的消费者，应该失败
    ConsumerId extra_consumer = queue.register_consumer();
    EXPECT_EQ(extra_consumer, INVALID_CONSUMER_ID);
    
    // 注销一个消费者
    queue.unregister_consumer(consumers[0]);
    
    // 现在应该可以再注册一个
    ConsumerId new_consumer = queue.register_consumer();
    EXPECT_NE(new_consumer, INVALID_CONSUMER_ID);
    
    // 清理
    for (size_t i = 1; i < consumers.size(); ++i) {
        queue.unregister_consumer(consumers[i]);
    }
    queue.unregister_consumer(new_consumer);
    shared_memory_object::remove(queue_name);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

