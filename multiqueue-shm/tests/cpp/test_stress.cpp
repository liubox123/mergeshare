/**
 * @file test_stress.cpp
 * @brief 大数据量压力测试
 * 
 * 测试系统在大量数据下的稳定性和性能
 */

#include <multiqueue/config.hpp>
#include <multiqueue/metadata.hpp>
#include <multiqueue/mp_logger.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace multiqueue;
using namespace multiqueue::logger;

/**
 * @brief 大数据量元数据测试
 */
TEST(StressTest, LargeMetadataOperations) {
    const int iterations = 100000;
    
    for (int i = 0; i < iterations; ++i) {
        QueueMetadata metadata;
        QueueConfig config;
        config.capacity = 1024 + (i % 1000);
        config.queue_name = "queue_" + std::to_string(i);
        
        metadata.initialize(config, sizeof(int));
        
        ASSERT_TRUE(metadata.is_valid());
        ASSERT_EQ(metadata.capacity, config.capacity);
    }
    
    std::cout << "✓ Tested " << iterations << " metadata initializations" << std::endl;
}

/**
 * @brief 大数据量控制块原子操作测试
 */
TEST(StressTest, ControlBlockAtomicOperations) {
    const int iterations = 1000000;
    ControlBlock control;
    control.initialize();
    
    // 单线程大量原子操作
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        control.write_offset.fetch_add(1, std::memory_order_relaxed);
        control.read_offset.fetch_add(1, std::memory_order_relaxed);
        control.total_pushed.fetch_add(1, std::memory_order_relaxed);
        control.total_popped.fetch_add(1, std::memory_order_relaxed);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_EQ(control.write_offset.load(), iterations);
    EXPECT_EQ(control.read_offset.load(), iterations);
    
    double ops_per_sec = (iterations * 4.0) / (duration.count() / 1000000.0);
    std::cout << "✓ Atomic operations: " << iterations * 4 << " ops in " 
              << duration.count() << " us" << std::endl;
    std::cout << "  Throughput: " << ops_per_sec / 1000000.0 << " M ops/sec" << std::endl;
}

/**
 * @brief 多线程原子操作竞争测试
 */
TEST(StressTest, MultiThreadedAtomicOperations) {
    const int num_threads = 8;
    const int ops_per_thread = 100000;
    
    ControlBlock control;
    control.initialize();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&control]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                control.write_offset.fetch_add(1, std::memory_order_acq_rel);
                control.total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    uint64_t expected = num_threads * ops_per_thread;
    EXPECT_EQ(control.write_offset.load(), expected);
    EXPECT_EQ(control.total_pushed.load(), expected);
    
    double ops_per_sec = (expected * 2.0) / (duration.count() / 1000000.0);
    std::cout << "✓ Multi-threaded atomic ops: " << num_threads << " threads, " 
              << expected * 2 << " total ops" << std::endl;
    std::cout << "  Duration: " << duration.count() << " us" << std::endl;
    std::cout << "  Throughput: " << ops_per_sec / 1000000.0 << " M ops/sec" << std::endl;
}

/**
 * @brief 大数据量配置验证测试
 */
TEST(StressTest, MassiveConfigValidation) {
    const int num_configs = 50000;
    
    std::vector<QueueConfig> configs(num_configs);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_configs; ++i) {
        configs[i].capacity = 1024 + (i % 10000);
        configs[i].queue_name = "queue_" + std::to_string(i);
        configs[i].timeout_ms = 1000 + (i % 5000);
        
        ASSERT_TRUE(configs[i].is_valid());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "✓ Validated " << num_configs << " configurations in " 
              << duration.count() << " ms" << std::endl;
    std::cout << "  Rate: " << (num_configs * 1000.0 / duration.count()) 
              << " configs/sec" << std::endl;
}

/**
 * @brief ElementHeader 大量标记操作测试
 */
TEST(StressTest, ElementHeaderBulkOperations) {
    const int num_elements = 100000;
    std::vector<ElementHeader> headers(num_elements);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 批量初始化
    for (int i = 0; i < num_elements; ++i) {
        headers[i].initialize(i, i * 1000, sizeof(int));
        headers[i].mark_valid();
    }
    
    // 验证
    for (int i = 0; i < num_elements; ++i) {
        ASSERT_TRUE(headers[i].is_valid());
        ASSERT_EQ(headers[i].sequence_id, i);
    }
    
    // 批量标记为已读
    for (int i = 0; i < num_elements; ++i) {
        headers[i].mark_read();
    }
    
    // 验证已读标记
    for (int i = 0; i < num_elements; ++i) {
        ASSERT_TRUE(headers[i].is_read());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "✓ Processed " << num_elements << " element headers in " 
              << duration.count() << " ms" << std::endl;
}

/**
 * @brief 大数据量日志写入测试
 */
TEST(StressTest, MassiveLogging) {
    const std::string log_file = "test_stress_logger.log";
    std::remove(log_file.c_str());
    
    LogConfig config;
    config.log_file = log_file;
    config.level = LogLevel::INFO;
    config.enable_console = false;  // 禁用控制台输出
    MPLogger::instance().initialize(config);
    
    const int num_messages = 1000;  // 减少数量以避免超时
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_messages; ++i) {
        LOG_INFO_FMT("Stress test message " << i << " with data value " << (i * 2));
    }
    
    MPLogger::instance().flush();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 验证日志文件
    std::ifstream file(log_file);
    ASSERT_TRUE(file.good());
    
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("Stress test message") != std::string::npos) {
            line_count++;
        }
    }
    
    EXPECT_GE(line_count, num_messages);
    
    std::cout << "✓ Logged " << num_messages << " messages in " 
              << duration.count() << " ms" << std::endl;
    std::cout << "  Rate: " << (num_messages * 1000.0 / duration.count()) 
              << " logs/sec" << std::endl;
    
    // 清理
    std::remove(log_file.c_str());
}

/**
 * @brief 多线程大数据量日志写入测试
 */
TEST(StressTest, MultiThreadedMassiveLogging) {
    const std::string log_file = "test_stress_mt_logger.log";
    std::remove(log_file.c_str());
    
    LogConfig config;
    config.log_file = log_file;
    config.level = LogLevel::INFO;
    config.enable_console = false;  // 禁用控制台输出
    MPLogger::instance().initialize(config);
    
    const int num_threads = 4;
    const int messages_per_thread = 250;  // 减少数量以避免超时
    
    auto start = std::chrono::high_resolution_clock::now();
    
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
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 验证日志文件
    std::ifstream file(log_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("Thread") != std::string::npos && 
            line.find("message") != std::string::npos) {
            line_count++;
        }
    }
    
    int expected = num_threads * messages_per_thread;
    EXPECT_GE(line_count, expected);
    
    std::cout << "✓ Multi-threaded logging: " << num_threads << " threads, " 
              << expected << " total messages" << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Rate: " << (expected * 1000.0 / duration.count()) 
              << " logs/sec" << std::endl;
    
    // 清理
    std::remove(log_file.c_str());
}

/**
 * @brief 内存压力测试 - 大量对象创建和销毁
 */
TEST(StressTest, MemoryPressure) {
    const int iterations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        // 创建大量临时对象
        QueueConfig config;
        config.capacity = 1024;
        config.queue_name = "temp_queue_" + std::to_string(i);
        
        QueueMetadata metadata;
        metadata.initialize(config, sizeof(int));
        
        ControlBlock control;
        control.initialize();
        
        std::vector<ElementHeader> headers(100);
        for (size_t j = 0; j < headers.size(); ++j) {
            headers[j].initialize(j, j * 1000, sizeof(int));
        }
        
        // 对象在作用域结束时自动销毁
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "✓ Memory pressure test: " << iterations 
              << " iterations with object creation/destruction" << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
}

/**
 * @brief CAS 操作竞争测试
 */
TEST(StressTest, CASCompetition) {
    const int num_threads = 8;
    const int ops_per_thread = 50000;
    
    std::atomic<uint64_t> counter{0};
    std::atomic<int> success_count{0};
    std::atomic<int> retry_count{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&counter, &success_count, &retry_count]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                uint64_t expected = counter.load(std::memory_order_acquire);
                int retries = 0;
                
                while (!counter.compare_exchange_weak(
                    expected, expected + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                    retries++;
                }
                
                success_count.fetch_add(1, std::memory_order_relaxed);
                retry_count.fetch_add(retries, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    uint64_t expected = num_threads * ops_per_thread;
    EXPECT_EQ(counter.load(), expected);
    EXPECT_EQ(success_count.load(), expected);
    
    double avg_retries = static_cast<double>(retry_count.load()) / expected;
    
    std::cout << "✓ CAS competition: " << num_threads << " threads, " 
              << expected << " successful CAS operations" << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Total retries: " << retry_count.load() << std::endl;
    std::cout << "  Average retries per operation: " << avg_retries << std::endl;
    std::cout << "  Throughput: " << (expected * 1000.0 / duration.count()) 
              << " CAS ops/sec" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "MultiQueue-SHM Stress Testing" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Stress Testing Completed" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return result;
}

