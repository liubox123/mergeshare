/**
 * @file test_timestamp_sync.cpp
 * @brief 时间戳同步测试
 */

#include <multiqueue/timestamp_sync.hpp>
#include <multiqueue/ring_queue.hpp>
#include <gtest/gtest.h>
#include <boost/interprocess/shared_memory_object.hpp>

using namespace multiqueue;
namespace bip = boost::interprocess;

/**
 * @brief 时间戳同步测试
 */
class TimestampSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试队列
        queue_name1_ = "test_ts_queue1_" + std::to_string(std::rand());
        queue_name2_ = "test_ts_queue2_" + std::to_string(std::rand());
        
        // 清理可能存在的共享内存
        bip::shared_memory_object::remove(queue_name1_.c_str());
        bip::shared_memory_object::remove(queue_name2_.c_str());
    }
    
    void TearDown() override {
        // 清理共享内存
        bip::shared_memory_object::remove(queue_name1_.c_str());
        bip::shared_memory_object::remove(queue_name2_.c_str());
    }
    
    std::string queue_name1_;
    std::string queue_name2_;
};

/**
 * @brief 测试时间戳生成
 */
TEST_F(TimestampSyncTest, TimestampGeneration) {
    uint64_t ts1 = TimestampSynchronizer::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t ts2 = TimestampSynchronizer::now();
    
    EXPECT_LT(ts1, ts2);
    EXPECT_GT(ts2 - ts1, 10000000ULL);  // > 10ms in nanoseconds
}

TEST_F(TimestampSyncTest, TimestampFormats) {
    uint64_t nanos = TimestampSynchronizer::now();
    uint64_t micros = TimestampSynchronizer::now_micros();
    uint64_t millis = TimestampSynchronizer::now_millis();
    
    // 基本范围检查
    EXPECT_GT(nanos, micros);
    EXPECT_GT(micros, millis);
    
    // 转换关系检查 (允许一些误差)
    EXPECT_NEAR(nanos / 1000.0, micros, 1000.0);
    EXPECT_NEAR(micros / 1000.0, millis, 10.0);
}

/**
 * @brief 测试 MergedQueueView
 */
TEST_F(TimestampSyncTest, MergedQueueView_Basic) {
    QueueConfig config(1024);
    config.has_timestamp = true;
    
    auto queue1 = std::make_shared<RingQueue<int>>(queue_name1_, config);
    auto queue2 = std::make_shared<RingQueue<int>>(queue_name2_, config);
    
    // 向队列1写入数据 (时间戳: 1000, 3000, 5000)
    ASSERT_TRUE(queue1->push(1, 1000));
    ASSERT_TRUE(queue1->push(3, 3000));
    ASSERT_TRUE(queue1->push(5, 5000));
    
    // 向队列2写入数据 (时间戳: 2000, 4000, 6000)
    ASSERT_TRUE(queue2->push(2, 2000));
    ASSERT_TRUE(queue2->push(4, 4000));
    ASSERT_TRUE(queue2->push(6, 6000));
    
    // 创建合并视图
    std::vector<std::shared_ptr<RingQueue<int>>> queues = {queue1, queue2};
    MergedQueueView<int> merged(queues, 1000);
    
    // 按时间戳顺序读取
    int value;
    uint64_t timestamp;
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 1);
    EXPECT_EQ(timestamp, 1000u);
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 2);
    EXPECT_EQ(timestamp, 2000u);
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 3);
    EXPECT_EQ(timestamp, 3000u);
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 4);
    EXPECT_EQ(timestamp, 4000u);
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 5);
    EXPECT_EQ(timestamp, 5000u);
    
    ASSERT_TRUE(merged.next(value, &timestamp));
    EXPECT_EQ(value, 6);
    EXPECT_EQ(timestamp, 6000u);
}

TEST_F(TimestampSyncTest, MergedQueueView_EmptyQueues) {
    QueueConfig config(1024);
    config.has_timestamp = true;
    
    auto queue1 = std::make_shared<RingQueue<int>>(queue_name1_, config);
    auto queue2 = std::make_shared<RingQueue<int>>(queue_name2_, config);
    
    std::vector<std::shared_ptr<RingQueue<int>>> queues = {queue1, queue2};
    MergedQueueView<int> merged(queues, 100);  // 100ms timeout
    
    int value;
    EXPECT_FALSE(merged.next(value));  // Should timeout
}

TEST_F(TimestampSyncTest, MergedQueueView_MultipleReads) {
    QueueConfig config(1024);
    config.has_timestamp = true;
    
    auto queue1 = std::make_shared<RingQueue<int>>(queue_name1_, config);
    auto queue2 = std::make_shared<RingQueue<int>>(queue_name2_, config);
    
    // 写入更多数据测试合并顺序
    for (int i = 0; i < 10; i += 2) {
        queue1->push(i, i * 1000);
        queue2->push(i + 1, (i + 1) * 1000);
    }
    
    std::vector<std::shared_ptr<RingQueue<int>>> queues = {queue1, queue2};
    MergedQueueView<int> merged(queues, 1000);
    
    // 验证按时间戳顺序输出
    int value;
    uint64_t timestamp;
    int expected = 0;
    
    while (merged.next(value, &timestamp)) {
        EXPECT_EQ(value, expected);
        EXPECT_EQ(timestamp, expected * 1000u);
        expected++;
        if (expected >= 10) break;
    }
    
    EXPECT_EQ(expected, 10);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

