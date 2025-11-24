/**
 * @file test_port_queue.cpp
 * @brief 测试 PortQueue
 */

#include <multiqueue/port_queue.hpp>
#include <gtest/gtest.h>
#include <thread>

using namespace multiqueue;

class PortQueueTest : public ::testing::Test {
protected:
    static constexpr const char* QUEUE_NAME = "test_port_queue";
    static constexpr size_t QUEUE_CAPACITY = 16;
    
    void SetUp() override {
        // 清理旧的共享内存
        boost::interprocess::shared_memory_object::remove(QUEUE_NAME);
    }
    
    void TearDown() override {
        // 清理共享内存
        boost::interprocess::shared_memory_object::remove(QUEUE_NAME);
    }
};

TEST_F(PortQueueTest, CreateAndOpen) {
    // 创建队列
    PortQueue queue1;
    ASSERT_TRUE(queue1.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    EXPECT_TRUE(queue1.is_valid());
    EXPECT_EQ(queue1.capacity(), QUEUE_CAPACITY);
    EXPECT_EQ(queue1.size(), 0);
    EXPECT_TRUE(queue1.empty());
    
    // 打开队列
    PortQueue queue2;
    ASSERT_TRUE(queue2.open(QUEUE_NAME));
    EXPECT_TRUE(queue2.is_valid());
    EXPECT_EQ(queue2.capacity(), QUEUE_CAPACITY);
}

TEST_F(PortQueueTest, PushAndPop) {
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    
    // 推送数据
    ASSERT_TRUE(queue.push_with_timeout(100, 1000));
    EXPECT_EQ(queue.size(), 1);
    EXPECT_FALSE(queue.empty());
    
    ASSERT_TRUE(queue.push_with_timeout(200, 1000));
    EXPECT_EQ(queue.size(), 2);
    
    // 弹出数据
    BufferId value1;
    ASSERT_TRUE(queue.pop_with_timeout(value1, 1000));
    EXPECT_EQ(value1, 100);
    EXPECT_EQ(queue.size(), 1);
    
    BufferId value2;
    ASSERT_TRUE(queue.pop_with_timeout(value2, 1000));
    EXPECT_EQ(value2, 200);
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());
}

TEST_F(PortQueueTest, FullQueue) {
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    
    // 填满队列
    for (size_t i = 0; i < QUEUE_CAPACITY; ++i) {
        ASSERT_TRUE(queue.push_with_timeout(i, 1000));
    }
    
    EXPECT_TRUE(queue.full());
    EXPECT_EQ(queue.size(), QUEUE_CAPACITY);
    
    // 尝试再推送（应该超时）
    bool result = queue.push_with_timeout(999, 100);
    EXPECT_FALSE(result);
    
    // 弹出一个
    BufferId value;
    ASSERT_TRUE(queue.pop_with_timeout(value, 1000));
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(queue.full());
    
    // 现在应该可以推送了
    ASSERT_TRUE(queue.push_with_timeout(1000, 1000));
}

TEST_F(PortQueueTest, EmptyQueuePopTimeout) {
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    
    // 从空队列弹出（应该超时）
    BufferId value;
    bool result = queue.pop_with_timeout(value, 100);
    EXPECT_FALSE(result);
}

TEST_F(PortQueueTest, ProducerConsumer) {
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    
    constexpr size_t NUM_ITEMS = 100;
    std::atomic<size_t> consumed_count{0};
    std::atomic<BufferId> last_value{0};
    
    // 消费者线程
    std::thread consumer([&]() {
        PortQueue consumer_queue;
        ASSERT_TRUE(consumer_queue.open(QUEUE_NAME));
        
        for (size_t i = 0; i < NUM_ITEMS; ++i) {
            BufferId value;
            if (consumer_queue.pop_with_timeout(value, 5000)) {
                last_value.store(value);
                consumed_count.fetch_add(1);
            }
        }
    });
    
    // 生产者线程
    std::thread producer([&]() {
        PortQueue producer_queue;
        ASSERT_TRUE(producer_queue.open(QUEUE_NAME));
        
        for (size_t i = 0; i < NUM_ITEMS; ++i) {
            ASSERT_TRUE(producer_queue.push_with_timeout(i, 5000));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed_count.load(), NUM_ITEMS);
    EXPECT_EQ(last_value.load(), NUM_ITEMS - 1);
}

TEST_F(PortQueueTest, CloseQueue) {
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, QUEUE_CAPACITY));
    
    // 关闭队列
    queue.close();
    EXPECT_TRUE(queue.is_closed());
    
    // 尝试推送（应该失败）
    bool result = queue.push_with_timeout(100, 1000);
    EXPECT_FALSE(result);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

