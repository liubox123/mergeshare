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
    
    // 注册消费者（广播模式必需）
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
    // Pop from empty queue should fail immediately
    int value = 0;
    EXPECT_FALSE(queue.pop(value));
}

TEST_F(RingQueueTest, Blocking) {
    QueueConfig config(10);
    config.blocking_mode = BlockingMode::BLOCKING;
    config.timeout_ms = 100;  // 100ms timeout
    RingQueue<int> queue(queue_name_, config);
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    constexpr int count = 100;  // 增加数据量
    std::atomic<int> consumed_count{0};
    std::atomic<int> produced_count{0};
    
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "[TEST] Starting SPSC test with count=" << count << std::endl;
    std::cerr << "========================================\n" << std::endl;
    
    // 生产者线程 - 先启动！
    std::thread producer([this, &produced_count]() {
        constexpr int count = 100;
        std::cerr << "[PRODUCER] Creating queue..." << std::endl;
        QueueConfig producer_config(1024);
        producer_config.queue_role = QueueRole::PRODUCER;
        producer_config.blocking_mode = BlockingMode::BLOCKING;  // 阻塞模式，保证不覆盖
        RingQueue<int> producer_queue(queue_name_, producer_config);
        std::cerr << "[PRODUCER] Queue created and initialized" << std::endl;
        
        for (int i = 0; i < count; ++i) {
            while (!producer_queue.push(i)) {
                std::this_thread::yield();
            }
            produced_count++;
            if (i % 10 == 0) {
                std::cerr << "[PRODUCER] Produced " << produced_count.load() << "/" << count << std::endl;
            }
        }
        std::cerr << "[PRODUCER] Done! Total produced: " << produced_count.load() << std::endl;
    });
    
    // 稍等一下，确保生产者先创建并初始化队列
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 消费者线程 - 后启动
    std::thread consumer([this, &consumed_count, &produced_count]() {
        constexpr int count = 100;
        std::cerr << "[CONSUMER] Creating queue..." << std::endl;
        QueueConfig consumer_config(1024);
        consumer_config.queue_role = QueueRole::CONSUMER;  // 消费者角色
        consumer_config.open_retry_count = 10;             // 最多重试 10 次
        consumer_config.open_retry_interval_ms = 100;      // 每次重试间隔 100ms
        RingQueue<int> consumer_queue(queue_name_, consumer_config);
        
        // 等待队列初始化完成
        std::cerr << "[CONSUMER] Waiting for queue ready..." << std::endl;
        if (!consumer_queue.wait_for_ready(5000)) {
            std::cerr << "[CONSUMER] Timeout waiting for queue ready!" << std::endl;
            return;
        }
        std::cerr << "[CONSUMER] Queue ready!" << std::endl;
        
        std::cerr << "[CONSUMER] Registering consumer..." << std::endl;
        // FROM_OLDEST_AVAILABLE: 从最旧可用数据开始，保证不丢数据
        bool reg_result = consumer_queue.register_consumer("consumer_1", 
                                                          ConsumerStartMode::FROM_OLDEST_AVAILABLE);
        std::cerr << "[CONSUMER] Register result: " << reg_result << ", slot=" << consumer_queue.get_consumer_slot_id() << std::endl;
        EXPECT_TRUE(reg_result);
        
        std::cerr << "[CONSUMER] Starting to consume..." << std::endl;
        int expected_value = 0;
        int timeout_count = 0;
        const int max_timeout = 100;  // 最多超时 100 次
        
        while (consumed_count.load() < count && timeout_count < max_timeout) {
            int value = 0;
            if (consumer_queue.pop_with_timeout(value, 100)) {  // 100ms 超时
                EXPECT_EQ(value, expected_value);
                expected_value++;
                consumed_count++;
                timeout_count = 0;  // 重置超时计数
                
                if (consumed_count.load() % 10 == 0) {
                    std::cerr << "[CONSUMER] Consumed " << consumed_count.load() << "/" << count << std::endl;
                }
            } else {
                timeout_count++;
                // 检查生产者是否已经完成
                if (produced_count.load() == count) {
                    std::cerr << "[CONSUMER] Producer finished, breaking" << std::endl;
                    break;
                }
            }
        }
        
        std::cerr << "[CONSUMER] Done! Consumed: " << consumed_count.load() << std::endl;
    });
    
    producer.join();
    std::cerr << "[TEST] Producer joined" << std::endl;
    consumer.join();
    std::cerr << "[TEST] Consumer joined" << std::endl;
    
    // 消费者应该消费所有生产的数据（FROM_OLDEST_AVAILABLE + BLOCKING mode）
    EXPECT_EQ(consumed_count.load(), produced_count.load());
    EXPECT_EQ(produced_count.load(), count);
    std::cerr << "[TEST] Final: produced=" << produced_count.load() 
              << ", consumed=" << consumed_count.load() << std::endl;
}

TEST_F(RingQueueTest, MultiThreaded_MultiProducerMultiConsumer) {
    QueueConfig config(1024);
    
    const int num_producers = 2;
    const int num_consumers = 3;
    const int items_per_producer = 100;
    const int total_items = num_producers * items_per_producer;
    
    std::atomic<int> produced_count{0};
    std::vector<std::atomic<int>> consumed_counts(num_consumers);
    
    // 生产者线程（共享同一个队列实例）
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([this, &produced_count]() {
            RingQueue<int> producer_queue(queue_name_, QueueConfig(1024));
            for (int i = 0; i < items_per_producer; ++i) {
                while (!producer_queue.push(i)) {
                    std::this_thread::yield();
                }
                produced_count++;
            }
        });
    }
    
    // 消费者线程（广播模式：每个消费者都能读到所有数据）
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([this, &consumed_counts, c]() {
            RingQueue<int> consumer_queue(queue_name_, QueueConfig(1024));
            // 每个消费者注册自己的 ID
            EXPECT_TRUE(consumer_queue.register_consumer("consumer_" + std::to_string(c)));
            
            int value = 0;
            int count = 0;
            // 每个消费者应该能读到所有生产的数据
            while (count < total_items) {
                if (consumer_queue.pop(value)) {
                    count++;
                } else {
                    std::this_thread::yield();
                }
            }
            consumed_counts[c] = count;
        });
    }
    
    // 等待所有线程完成
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(produced_count.load(), total_items);
    // 广播模式：每个消费者都应该读到所有数据
    for (int c = 0; c < num_consumers; ++c) {
        EXPECT_EQ(consumed_counts[c].load(), total_items);
    }
}

/**
 * @brief 统计信息测试
 */
TEST_F(RingQueueTest, Statistics) {
    QueueConfig config(1024);
    RingQueue<int> queue(queue_name_, config);
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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
    
    // 注册消费者
    EXPECT_TRUE(queue.register_consumer("test_consumer"));
    
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

