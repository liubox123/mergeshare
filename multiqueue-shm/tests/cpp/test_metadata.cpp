/**
 * @file test_metadata.cpp
 * @brief 元数据结构单元测试
 */

#include <multiqueue/metadata.hpp>
#include <gtest/gtest.h>

using namespace multiqueue;

/**
 * @brief QueueMetadata 测试
 */
class MetadataTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.capacity = 1024;
        config_.queue_name = "test_queue";
        config_.blocking_mode = BlockingMode::BLOCKING;
        config_.timeout_ms = 1000;
        config_.has_timestamp = true;
    }
    
    QueueConfig config_;
};

TEST_F(MetadataTest, Initialize) {
    QueueMetadata metadata;
    metadata.initialize(config_, sizeof(int));
    
    EXPECT_EQ(metadata.magic_number, QUEUE_MAGIC_NUMBER);
    EXPECT_EQ(metadata.version, QUEUE_VERSION);
    EXPECT_EQ(metadata.element_size, sizeof(int));
    EXPECT_EQ(metadata.capacity, 1024u);
    EXPECT_TRUE(metadata.has_timestamp);
    EXPECT_EQ(metadata.blocking_mode, BlockingMode::BLOCKING);
    EXPECT_STREQ(metadata.queue_name, "test_queue");
}

TEST_F(MetadataTest, IsValid) {
    QueueMetadata metadata;
    metadata.initialize(config_, sizeof(int));
    
    EXPECT_TRUE(metadata.is_valid());
    
    // 破坏魔数
    metadata.magic_number = 0;
    EXPECT_FALSE(metadata.is_valid());
}

TEST_F(MetadataTest, VersionString) {
    QueueMetadata metadata;
    metadata.initialize(config_, sizeof(int));
    
    std::string version = metadata.get_version_string();
    EXPECT_FALSE(version.empty());
    EXPECT_EQ(version, "0.1.0");
}

/**
 * @brief ControlBlock 测试
 */
TEST(ControlBlockTest, Initialize) {
    ControlBlock control;
    control.initialize();
    
    EXPECT_EQ(control.write_offset.load(), 0u);
    EXPECT_EQ(control.read_offset.load(), 0u);
    EXPECT_EQ(control.producer_count.load(), 0u);
    EXPECT_EQ(control.consumer_count.load(), 0u);
    EXPECT_EQ(control.total_pushed.load(), 0u);
    EXPECT_EQ(control.total_popped.load(), 0u);
    EXPECT_EQ(control.overwrite_count.load(), 0u);
    EXPECT_FALSE(control.is_closed());
}

TEST(ControlBlockTest, AtomicOperations) {
    ControlBlock control;
    control.initialize();
    
    // 测试原子操作
    control.write_offset.fetch_add(1);
    EXPECT_EQ(control.write_offset.load(), 1u);
    
    control.read_offset.store(5);
    EXPECT_EQ(control.read_offset.load(), 5u);
    
    control.producer_count.fetch_add(1);
    EXPECT_EQ(control.producer_count.load(), 1u);
}

TEST(ControlBlockTest, CloseFlag) {
    ControlBlock control;
    control.initialize();
    
    EXPECT_FALSE(control.is_closed());
    
    control.close();
    EXPECT_TRUE(control.is_closed());
}

/**
 * @brief ElementHeader 测试
 */
TEST(ElementHeaderTest, Initialize) {
    ElementHeader header;
    header.initialize(42, 12345678, sizeof(int));
    
    EXPECT_EQ(header.sequence_id, 42u);
    EXPECT_EQ(header.timestamp, 12345678u);
    EXPECT_EQ(header.data_size, sizeof(int));
}

TEST(ElementHeaderTest, Flags) {
    ElementHeader header;
    header.initialize(0, 0, 0);
    
    EXPECT_FALSE(header.is_valid());
    EXPECT_FALSE(header.is_read());
    
    header.mark_valid();
    EXPECT_TRUE(header.is_valid());
    
    header.mark_read();
    EXPECT_TRUE(header.is_read());
    
    header.clear_flags();
    EXPECT_FALSE(header.is_valid());
    EXPECT_FALSE(header.is_read());
}

/**
 * @brief QueueStats 测试
 */
TEST(QueueStatsTest, DefaultConstruct) {
    QueueStats stats;
    
    EXPECT_EQ(stats.total_pushed, 0u);
    EXPECT_EQ(stats.total_popped, 0u);
    EXPECT_EQ(stats.overwrite_count, 0u);
    EXPECT_EQ(stats.producer_count, 0u);
    EXPECT_EQ(stats.consumer_count, 0u);
    EXPECT_EQ(stats.current_size, 0u);
    EXPECT_EQ(stats.capacity, 0u);
    EXPECT_FALSE(stats.is_closed);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


