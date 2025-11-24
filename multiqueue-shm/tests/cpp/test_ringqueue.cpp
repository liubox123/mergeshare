/**
 * @file test_ringqueue.cpp
 * @brief RingQueue 单元测试
 */

#include <multiqueue/ring_queue.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <boost/interprocess/shared_memory_object.hpp>

using namespace multiqueue;
namespace bip = boost::interprocess;

/**
 * @brief RingQueue 测试基类
 */
class RingQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue_name_ = "test_ringqueue_" + std::to_string(std::rand());
        
        // 清理可能存在的共享内存
        bip::shared_memory_object::remove(queue_name_.c_str());
    }
    
    void TearDown() override {
        // 清理共享内存
        bip::shared_memory_object::remove(queue_name_.c_str());
    }
    
    std::string queue_name_;
};

/**
 * @brief 基本功能测试
 */
TEST_F(RingQueueTest, CreateQueue) {
    QueueConfig config(1024);
    
    ASSERT_NO_THROW({
        RingQueue<int> queue(queue_name_, config);
        EXPECT_EQ(queue.capacity(), 1024u);
        EXPECT_TRUE(queue.empty());
        EXPECT_FALSE(queue.full());
    });
}

TEST_F(RingQueueTest, PushPop) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    // Push 一个元素
    EXPECT_TRUE(queue.push(42));
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_FALSE(queue.empty());
    
    // Pop 元素
    int value = 0;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
}

TEST_F(RingQueueTest, PushPopMultiple) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    const int count = 100;
    
    // Push 多个元素
    for (int i = 0; i < count; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    
    EXPECT_EQ(queue.size(), count);
    
    // Pop 多个元素
    for (int i = 0; i < count; ++i) {
        int value = 0;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
    
    EXPECT_TRUE(queue.empty());
}

TEST_F(RingQueueTest, Timestamp) {
    QueueConfig config(1024);
    config.has_timestamp = true;
    RingQueue<int> queue(queue_name_, config);
    
    uint64_t ts_in = 12345678;
    EXPECT_TRUE(queue.push(42, ts_in));
    
    int value = 0;
    uint64_t ts_out = 0;
    EXPECT_TRUE(queue.pop(value, &ts_out));
    
    EXPECT_EQ(value, 42);
    EXPECT_EQ(ts_out, ts_in);
}

TEST_F(RingQueueTest, NonBlocking) {
    QueueConfig config(10);
    config.blocking_mode = BlockingMode::NON_BLOCKING;
    RingQueue<int> queue(queue_name_, config);
    
    // Pop from empty queue should fail immediately
    int value = 0;
    EXPECT_FALSE(queue.pop(value));
}

TEST_F(RingQueueTest, Blocking) {
    QueueConfig config(10);
    config.blocking_mode = BlockingMode::BLOCKING;
    config.timeout_ms = 100;  // 100ms timeout
    RingQueue<int> queue(queue_name_, config);
    
    // Pop from empty queue should timeout
    auto start = std::chrono::steady_clock::now();
    int value = 0;
    EXPECT_FALSE(queue.pop(value));
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    // Should have waited approximately 100ms
    EXPECT_GE(elapsed, std::chrono::milliseconds(90));
}

/**
 * @brief 多线程测试
 */
TEST_F(RingQueueTest, MultiThreaded_SingleProducerSingleConsumer) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    const int count = 1000;
    std::atomic<int> consumed_count{0};
    
    // 生产者线程
    std::thread producer([&queue]() {
        for (int i = 0; i < count; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    // 消费者线程
    std::thread consumer([&queue, &consumed_count]() {
        for (int i = 0; i < count; ++i) {
            int value = 0;
            while (!queue.pop(value)) {
                std::this_thread::yield();
            }
            EXPECT_EQ(value, i);
            consumed_count++;
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed_count.load(), count);
    EXPECT_TRUE(queue.empty());
}

TEST_F(RingQueueTest, MultiThreaded_MultiProducerMultiConsumer) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 250;
    const int total_items = num_producers * items_per_producer;
    
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    
    // 生产者线程
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&queue, &produced_count]() {
            for (int i = 0; i < items_per_producer; ++i) {
                while (!queue.push(i)) {
                    std::this_thread::yield();
                }
                produced_count++;
            }
        });
    }
    
    // 消费者线程
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&queue, &consumed_count]() {
            int value = 0;
            while (consumed_count.load() < total_items) {
                if (queue.pop(value)) {
                    consumed_count++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(produced_count.load(), total_items);
    EXPECT_EQ(consumed_count.load(), total_items);
}

/**
 * @brief 统计信息测试
 */
TEST_F(RingQueueTest, Statistics) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    // Push some items
    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }
    
    // Get stats
    QueueStats stats = queue.get_stats();
    EXPECT_EQ(stats.total_pushed, 10u);
    EXPECT_EQ(stats.total_popped, 0u);
    EXPECT_EQ(stats.current_size, 10u);
    EXPECT_EQ(stats.capacity, 1024u);
    EXPECT_FALSE(stats.is_closed);
    
    // Pop some items
    for (int i = 0; i < 5; ++i) {
        int value;
        queue.pop(value);
    }
    
    stats = queue.get_stats();
    EXPECT_EQ(stats.total_pushed, 10u);
    EXPECT_EQ(stats.total_popped, 5u);
    EXPECT_EQ(stats.current_size, 5u);
}

TEST_F(RingQueueTest, CloseQueue) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    // Push some items
    queue.push(1);
    queue.push(2);
    
    // Close queue
    queue.close();
    EXPECT_TRUE(queue.is_closed());
    
    // 注意：当前实现中，close() 只是设置标志，并不阻止操作
    // 这是有意的设计，允许完成进行中的操作
    
    // Pop 应该仍然可以工作
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 2);
}

/**
 * @brief 结构体类型测试
 */
struct TestStruct {
    int id;
    double value;
    char name[32];
};

TEST_F(RingQueueTest, StructType) {
    QueueConfig config(1024);
    RingQueue<TestStruct> queue(queue_name_, config);
    
    TestStruct data_in;
    data_in.id = 123;
    data_in.value = 3.14;
    std::strncpy(data_in.name, "test", sizeof(data_in.name));
    
    EXPECT_TRUE(queue.push(data_in));
    
    TestStruct data_out;
    EXPECT_TRUE(queue.pop(data_out));
    
    EXPECT_EQ(data_out.id, 123);
    EXPECT_DOUBLE_EQ(data_out.value, 3.14);
    EXPECT_STREQ(data_out.name, "test");
}

/**
 * @brief 容量测试
 */
TEST_F(RingQueueTest, CapacityRounding) {
    // 注意：当前实现不会自动取整容量
    // 如果需要2的幂次，应该在配置中手动设置
    QueueConfig config(1000);
    RingQueue<int> queue(queue_name_, config);
    
    // 容量应该保持为配置值
    EXPECT_EQ(queue.capacity(), 1000u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

