/**
 * @file test_config.cpp
 * @brief 配置结构单元测试
 */

#include <multiqueue/config.hpp>
#include <gtest/gtest.h>

using namespace multiqueue;

/**
 * @brief QueueConfig 测试
 */
TEST(QueueConfigTest, DefaultConstruct) {
    QueueConfig config;
    
    EXPECT_EQ(config.capacity, 1024u);
    EXPECT_EQ(config.blocking_mode, BlockingMode::BLOCKING);
    EXPECT_EQ(config.timeout_ms, 1000u);
    EXPECT_FALSE(config.has_timestamp);
    EXPECT_FALSE(config.enable_async);
}

TEST(QueueConfigTest, CapacityConstruct) {
    QueueConfig config(2048);
    
    EXPECT_EQ(config.capacity, 2048u);
}

TEST(QueueConfigTest, IsValid) {
    QueueConfig config;
    config.capacity = 1024;
    EXPECT_TRUE(config.is_valid());
    
    // 容量为 0
    config.capacity = 0;
    EXPECT_FALSE(config.is_valid());
    
    // 超时时间过大
    config.capacity = 1024;
    config.timeout_ms = 4000000;  // > 1 小时
    EXPECT_FALSE(config.is_valid());
    
    // 用户元数据过大
    config.timeout_ms = 1000;
    config.user_metadata = std::string(600, 'x');  // > 512 字节
    EXPECT_FALSE(config.is_valid());
}

TEST(QueueConfigTest, IsPowerOfTwo) {
    QueueConfig config;
    
    config.capacity = 1024;
    EXPECT_TRUE(config.is_power_of_two());
    
    config.capacity = 1000;
    EXPECT_FALSE(config.is_power_of_two());
    
    config.capacity = 2048;
    EXPECT_TRUE(config.is_power_of_two());
    
    config.capacity = 1;
    EXPECT_TRUE(config.is_power_of_two());
}

TEST(QueueConfigTest, RoundUpToPowerOfTwo) {
    QueueConfig config;
    
    config.capacity = 1000;
    config.round_up_capacity_to_power_of_two();
    EXPECT_EQ(config.capacity, 1024u);
    EXPECT_TRUE(config.is_power_of_two());
    
    config.capacity = 1024;
    config.round_up_capacity_to_power_of_two();
    EXPECT_EQ(config.capacity, 1024u);  // 已经是 2 的幂次
    
    config.capacity = 100;
    config.round_up_capacity_to_power_of_two();
    EXPECT_EQ(config.capacity, 128u);
}

/**
 * @brief BlockingMode 测试
 */
TEST(BlockingModeTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(BlockingMode::BLOCKING), 0);
    EXPECT_EQ(static_cast<int>(BlockingMode::NON_BLOCKING), 1);
}

/**
 * @brief LogLevel 测试
 */
TEST(LogLevelTest, EnumValues) {
    EXPECT_LT(LogLevel::TRACE, LogLevel::DEBUG);
    EXPECT_LT(LogLevel::DEBUG, LogLevel::INFO);
    EXPECT_LT(LogLevel::INFO, LogLevel::WARN);
    EXPECT_LT(LogLevel::WARN, LogLevel::ERROR);
    EXPECT_LT(LogLevel::ERROR, LogLevel::FATAL);
}

/**
 * @brief LogConfig 测试
 */
TEST(LogConfigTest, DefaultConstruct) {
    LogConfig config;
    
    EXPECT_EQ(config.log_file, "multiqueue.log");
    EXPECT_EQ(config.level, LogLevel::INFO);
    EXPECT_TRUE(config.enable_console);
    EXPECT_FALSE(config.enable_async);
    EXPECT_EQ(config.max_file_size, 100 * 1024 * 1024u);  // 100MB
    EXPECT_EQ(config.max_backup_files, 3);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


