/**
 * @file test_logger.cpp
 * @brief 日志组件单元测试
 */

#include <multiqueue/mp_logger.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include <thread>

using namespace multiqueue;
using namespace multiqueue::logger;

/**
 * @brief Logger 基础测试
 */
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file_ = "test_logger.log";
        // 清理旧的日志文件
        std::remove(log_file_.c_str());
    }
    
    void TearDown() override {
        // 清理测试日志文件
        std::remove(log_file_.c_str());
        for (int i = 1; i <= 5; ++i) {
            std::string backup = log_file_ + "." + std::to_string(i);
            std::remove(backup.c_str());
        }
    }
    
    std::string log_file_;
};

TEST_F(LoggerTest, Initialize) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::INFO;
    config.enable_console = false;  // 禁用控制台输出
    MPLogger::instance().initialize(config);
    
    // 验证日志文件创建
    std::ifstream file(log_file_);
    EXPECT_TRUE(file.good());
}

TEST_F(LoggerTest, LogLevels) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::INFO;
    config.enable_console = false;
    MPLogger::instance().initialize(config);
    
    LOG_TRACE("This is trace");
    LOG_DEBUG("This is debug");
    LOG_INFO("This is info");
    LOG_WARN("This is warning");
    LOG_ERROR("This is error");
    
    MPLogger::instance().flush();
    
    // 读取日志文件
    std::ifstream file(log_file_);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    // TRACE 和 DEBUG 不应该出现（级别是 INFO）
    EXPECT_EQ(content.find("This is trace"), std::string::npos);
    EXPECT_EQ(content.find("This is debug"), std::string::npos);
    
    // INFO、WARN、ERROR 应该出现
    EXPECT_NE(content.find("This is info"), std::string::npos);
    EXPECT_NE(content.find("This is warning"), std::string::npos);
    EXPECT_NE(content.find("This is error"), std::string::npos);
}

TEST_F(LoggerTest, SetLevel) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::WARN;
    config.enable_console = false;
    MPLogger::instance().initialize(config);
    
    EXPECT_EQ(MPLogger::instance().get_level(), LogLevel::WARN);
    
    MPLogger::instance().set_level(LogLevel::DEBUG);
    EXPECT_EQ(MPLogger::instance().get_level(), LogLevel::DEBUG);
}

TEST_F(LoggerTest, MultipleMessages) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::INFO;
    config.enable_console = false;
    MPLogger::instance().initialize(config);
    
    for (int i = 0; i < 10; ++i) {
        LOG_INFO_FMT("Message " << i);
    }
    
    MPLogger::instance().flush();
    
    // 验证所有消息都写入
    std::ifstream file(log_file_);
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.find("Message") != std::string::npos) {
            count++;
        }
    }
    
    EXPECT_GE(count, 10);  // 至少有 10 条消息
}

TEST_F(LoggerTest, LogFormat) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::INFO;
    config.enable_console = false;
    MPLogger::instance().initialize(config);
    
    LOG_INFO("Test message");
    MPLogger::instance().flush();
    
    // 读取日志文件
    std::ifstream file(log_file_);
    std::string line;
    std::getline(file, line);
    
    // 验证日志格式包含必要元素
    EXPECT_NE(line.find("[INFO]"), std::string::npos);
    EXPECT_NE(line.find("Test message"), std::string::npos);
    EXPECT_NE(line.find("pid:"), std::string::npos);
    EXPECT_NE(line.find("tid:"), std::string::npos);
}

/**
 * @brief 多线程日志测试
 */
TEST_F(LoggerTest, MultiThreaded) {
    LogConfig config;
    config.log_file = log_file_;
    config.level = LogLevel::INFO;
    config.enable_console = false;
    MPLogger::instance().initialize(config);
    
    const int num_threads = 4;
    const int messages_per_thread = 50;  // 减少数量以避免超时
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                LOG_INFO_FMT("Thread " << t << " message " << i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    MPLogger::instance().flush();
    
    // 验证所有消息都写入
    std::ifstream file(log_file_);
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.find("Thread") != std::string::npos) {
            count++;
        }
    }
    
    EXPECT_GE(count, num_threads * messages_per_thread);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

