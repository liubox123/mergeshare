/**
 * @file test_continuous_stream.cpp
 * @brief 持续流测试
 * 
 * 测试长时间运行的数据流，验证系统在持续运行下的稳定性
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/timestamp.hpp>
// #include <multiqueue/mp_logger.hpp>  // 暂时不使用日志系统，直接用 std::cout
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace multiqueue;
using namespace boost::interprocess;

class ContinuousStreamTest : public ::testing::Test {
protected:
    static constexpr const char* REGISTRY_NAME = "test_continuous_registry";
    static constexpr const char* QUEUE_NAME = "test_continuous_queue";
    
    void SetUp() override {
        std::cout << "[SetUp] 开始初始化测试环境..." << std::endl;
        
        cleanup_shm();
        std::cout << "[SetUp] 已清理共享内存" << std::endl;
        
        // 创建 GlobalRegistry
        std::cout << "[SetUp] 创建 GlobalRegistry..." << std::endl;
        registry_shm_ = shared_memory_object(create_only, REGISTRY_NAME, read_write);
        registry_shm_.truncate(sizeof(GlobalRegistry));
        registry_region_ = mapped_region(registry_shm_, read_write);
        registry_ = new (registry_region_.get_address()) GlobalRegistry();
        registry_->initialize();
        std::cout << "[SetUp] GlobalRegistry 初始化完成" << std::endl;
        
        // 创建 ShmManager
        std::cout << "[SetUp] 创建 ShmManager..." << std::endl;
        process_id_ = 1;
        ShmConfig config;
        config.name_prefix = "mqshm_";
        config.pools = {
            PoolConfig("small", 4096, 200),
            PoolConfig("medium", 65536, 100)
        };
        shm_manager_ = std::make_unique<ShmManager>(registry_, process_id_, config);
        ASSERT_TRUE(shm_manager_->initialize()) << "ShmManager 初始化失败";
        std::cout << "[SetUp] ShmManager 初始化完成" << std::endl;
        
        allocator_ = shm_manager_->allocator();
        ASSERT_NE(allocator_, nullptr) << "Allocator 为空";
        std::cout << "[SetUp] 测试环境初始化完成" << std::endl;
    }
    
    void TearDown() override {
        shm_manager_.reset();
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(REGISTRY_NAME);
        shared_memory_object::remove(QUEUE_NAME);
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
    }
    
    shared_memory_object registry_shm_;
    mapped_region registry_region_;
    GlobalRegistry* registry_;
    ProcessId process_id_;
    std::unique_ptr<ShmManager> shm_manager_;
    SharedBufferAllocator* allocator_;
};

/**
 * @brief 测试：持续流（长时间运行）
 * 
 * 场景：
 * - 生产者持续生产数据（1000 个 Buffer）
 * - 消费者持续消费数据
 * - 验证系统在长时间运行下的稳定性
 */
TEST_F(ContinuousStreamTest, LongRunningStream) {
    const int NUM_BUFFERS = 1000;
    const int BATCH_SIZE = 100;
    
    std::cout << "\n=== 测试: 持续流（" << NUM_BUFFERS << " 个 Buffer）===" << std::endl;
    
    // 创建 PortQueue
    std::cout << "[TEST] 创建 PortQueue..." << std::endl;
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, 200)) << "PortQueue 创建失败";
    queue.set_allocator(allocator_);
    std::cout << "[TEST] PortQueue 创建成功" << std::endl;
    
    // 注册消费者
    std::cout << "[TEST] 注册消费者..." << std::endl;
    ConsumerId consumer = queue.register_consumer();
    ASSERT_NE(consumer, INVALID_CONSUMER_ID) << "消费者注册失败";
    std::cout << "[TEST] 消费者注册成功，ID=" << consumer << std::endl;
    
    // 生产者线程
    std::atomic<int> produced_count(0);
    std::atomic<bool> producer_done(false);
    
    std::thread producer_thread([&]() {
        std::cout << "[Producer] 线程启动" << std::endl;
        auto start_time = std::chrono::steady_clock::now();
        
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto loop_start = std::chrono::steady_clock::now();
            
            // 分配 Buffer
            auto buffer_ptr = shm_manager_->allocate(64);
            if (!buffer_ptr.valid()) {
                std::cerr << "[Producer] ERROR: Buffer 分配失败，i=" << i << std::endl;
                break;
            }
            
            // 设置时间戳
            Timestamp ts = Timestamp::now();
            buffer_ptr.set_timestamp(ts);
            
            // 写入数据
            int* data = buffer_ptr.as<int>();
            *data = i;
            
            // 推送到队列
            if (!queue.push(buffer_ptr.id())) {
                std::cerr << "[Producer] ERROR: 推送失败，i=" << i << std::endl;
                break;
            }
            
            produced_count.fetch_add(1);
            
            if ((i + 1) % BATCH_SIZE == 0) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                std::cout << "[Producer] 已生产 " << (i + 1) << "/" << NUM_BUFFERS 
                          << " 个 Buffer，耗时 " << ms << " ms" << std::endl;
            }
            
            // 检查是否超时（超过 30 秒）
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 30) {
                std::cerr << "[Producer] WARNING: 生产超时，已生产 " << (i + 1) << " 个" << std::endl;
                break;
            }
            
            // 模拟生产速率（每 1ms 一个）
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        producer_done.store(true);
        auto total_time = std::chrono::steady_clock::now() - start_time;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
        std::cout << "[Producer] 完成！总共生产 " << produced_count.load() 
                  << " 个 Buffer，耗时 " << ms << " ms" << std::endl;
    });
    
    // 消费者线程
    std::atomic<int> consumed_count(0);
    std::vector<int> consumed_values;
    consumed_values.reserve(NUM_BUFFERS);
    
    std::thread consumer_thread([&]() {
        std::cout << "[Consumer] 线程启动" << std::endl;
        auto start_time = std::chrono::steady_clock::now();
        int empty_count = 0;
        int last_count = 0;
        
        while (consumed_count.load() < NUM_BUFFERS) {
            BufferId buffer_id;
            if (queue.pop(consumer, buffer_id)) {
                empty_count = 0;  // 重置空计数
                
                BufferPtr buffer(buffer_id, allocator_);
                if (!buffer.valid()) {
                    std::cerr << "[Consumer] ERROR: Buffer 无效" << std::endl;
                    continue;
                }
                
                // 读取数据
                int value = *buffer.as<int>();
                consumed_values.push_back(value);
                
                // 验证时间戳
                Timestamp ts = buffer.timestamp();
                if (!ts.valid()) {
                    std::cerr << "[Consumer] WARNING: 时间戳无效，value=" << value << std::endl;
                }
                
                consumed_count.fetch_add(1);
                
                if (consumed_count.load() % BATCH_SIZE == 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                    std::cout << "[Consumer] 已消费 " << consumed_count.load() << "/" << NUM_BUFFERS 
                              << " 个 Buffer，耗时 " << ms << " ms" << std::endl;
                }
            } else {
                empty_count++;
                // 如果没有数据，短暂休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                // 如果连续 1000 次空（约 1 秒），输出警告
                if (empty_count % 1000 == 0 && empty_count > 0) {
                    std::cout << "[Consumer] WARNING: 连续 " << empty_count 
                              << " 次未获取到数据，已消费 " << consumed_count.load() 
                              << "，生产者完成=" << producer_done.load() << std::endl;
                }
                
                // 检查是否超时（超过 30 秒且没有新数据）
                if (empty_count > 10000 && consumed_count.load() == last_count) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                    std::cerr << "[Consumer] ERROR: 超时！已等待 " << sec 
                              << " 秒，已消费 " << consumed_count.load() << " 个" << std::endl;
                    break;
                }
                
                if (consumed_count.load() != last_count) {
                    last_count = consumed_count.load();
                    empty_count = 0;
                }
            }
        }
        
        auto total_time = std::chrono::steady_clock::now() - start_time;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
        std::cout << "[Consumer] 完成！总共消费 " << consumed_count.load() 
                  << " 个 Buffer，耗时 " << ms << " ms" << std::endl;
    });
    
    // 等待完成
    std::cout << "[TEST] 等待生产者线程完成..." << std::endl;
    producer_thread.join();
    std::cout << "[TEST] 生产者线程已结束" << std::endl;
    
    std::cout << "[TEST] 等待消费者线程完成..." << std::endl;
    consumer_thread.join();
    std::cout << "[TEST] 消费者线程已结束" << std::endl;
    
    // 验证
    std::cout << "[TEST] 验证结果..." << std::endl;
    std::cout << "[TEST] 生产数量: " << produced_count.load() << "/" << NUM_BUFFERS << std::endl;
    std::cout << "[TEST] 消费数量: " << consumed_count.load() << "/" << NUM_BUFFERS << std::endl;
    std::cout << "[TEST] 消费值数量: " << consumed_values.size() << std::endl;
    
    EXPECT_EQ(produced_count.load(), NUM_BUFFERS);
    EXPECT_EQ(consumed_count.load(), NUM_BUFFERS);
    EXPECT_EQ(consumed_values.size(), NUM_BUFFERS);
    
    // 验证数据完整性
    std::cout << "[TEST] 验证数据完整性..." << std::endl;
    std::sort(consumed_values.begin(), consumed_values.end());
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        if (consumed_values[i] != i) {
            std::cerr << "[TEST] ERROR: 数据不匹配，位置 " << i 
                      << " 期望 " << i << " 实际 " << consumed_values[i] << std::endl;
        }
        EXPECT_EQ(consumed_values[i], i);
    }
    
    std::cout << "✅ 持续流测试完成\n";
    
    // 清理
    std::cout << "[TEST] 清理资源..." << std::endl;
    queue.unregister_consumer(consumer);
    std::cout << "[TEST] 清理完成" << std::endl;
}

/**
 * @brief 测试：持续流 + 多消费者（广播模式）
 * 
 * 场景：
 * - 生产者持续生产数据（500 个 Buffer）
 * - 3 个消费者同时消费
 * - 验证每个消费者都收到完整数据
 */
TEST_F(ContinuousStreamTest, LongRunningStreamWithMultipleConsumers) {
    const int NUM_BUFFERS = 500;
    const int NUM_CONSUMERS = 3;
    
    std::cout << "\n=== 测试: 持续流 + " << NUM_CONSUMERS << " 消费者（广播模式）===" << std::endl;
    
    // 创建 PortQueue
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, 200));
    queue.set_allocator(allocator_);
    
    // 注册多个消费者
    std::vector<ConsumerId> consumers;
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        ConsumerId consumer = queue.register_consumer();
        ASSERT_NE(consumer, INVALID_CONSUMER_ID);
        consumers.push_back(consumer);
    }
    
    // 生产者线程
    std::atomic<int> produced_count(0);
    
    std::thread producer_thread([&]() {
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto buffer_ptr = shm_manager_->allocate(64);
            ASSERT_TRUE(buffer_ptr.valid());
            
            // 设置时间戳
            Timestamp ts = Timestamp::now();
            buffer_ptr.set_timestamp(ts);
            
            // 写入数据
            int* data = buffer_ptr.as<int>();
            *data = i;
            
            // 推送到队列
            ASSERT_TRUE(queue.push(buffer_ptr.id()));
            
            produced_count.fetch_add(1);
            
            if ((i + 1) % 100 == 0) {
                std::cout << "已生产 " << (i + 1) << " 个 Buffer\n";
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        std::cout << "生产者完成\n";
    });
    
    // 消费者线程
    std::vector<std::thread> consumer_threads;
    std::vector<std::atomic<int>> consumed_counts(NUM_CONSUMERS);
    std::vector<std::vector<int>> consumed_values(NUM_CONSUMERS);
    
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumed_values[i].reserve(NUM_BUFFERS);
        consumer_threads.emplace_back([&, i]() {
            ConsumerId consumer_id = consumers[i];
            
            while (consumed_counts[i].load() < NUM_BUFFERS) {
                BufferId buffer_id;
                if (queue.pop(consumer_id, buffer_id)) {
                    BufferPtr buffer(buffer_id, allocator_);
                    ASSERT_TRUE(buffer.valid());
                    
                    // 读取数据
                    int value = *buffer.as<int>();
                    consumed_values[i].push_back(value);
                    
                    consumed_counts[i].fetch_add(1);
                    
                    if (consumed_counts[i].load() % 100 == 0) {
                        std::cout << "消费者 " << i << " 已消费 " 
                                  << consumed_counts[i].load() << " 个 Buffer\n";
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            
            std::cout << "消费者 " << i << " 完成\n";
        });
    }
    
    // 等待完成
    producer_thread.join();
    for (auto& t : consumer_threads) {
        t.join();
    }
    
    // 验证
    EXPECT_EQ(produced_count.load(), NUM_BUFFERS);
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        EXPECT_EQ(consumed_counts[i].load(), NUM_BUFFERS);
        EXPECT_EQ(consumed_values[i].size(), NUM_BUFFERS);
        
        // 验证数据完整性
        std::sort(consumed_values[i].begin(), consumed_values[i].end());
        for (int j = 0; j < NUM_BUFFERS; ++j) {
            EXPECT_EQ(consumed_values[i][j], j);
        }
    }
    
    std::cout << "✅ 持续流 + 多消费者测试完成\n";
    
    // 清理
    for (ConsumerId consumer : consumers) {
        queue.unregister_consumer(consumer);
    }
}

/**
 * @brief 测试：持续流 + 时间戳验证
 * 
 * 场景：
 * - 生产者持续生产带时间戳的数据
 * - 验证时间戳的连续性和正确性
 */
TEST_F(ContinuousStreamTest, LongRunningStreamWithTimestamps) {
    const int NUM_BUFFERS = 300;
    
    std::cout << "\n=== 测试: 持续流 + 时间戳验证 ===" << std::endl;
    
    // 创建 PortQueue
    PortQueue queue;
    ASSERT_TRUE(queue.create(QUEUE_NAME, 1, 200));
    queue.set_allocator(allocator_);
    
    // 注册消费者
    ConsumerId consumer = queue.register_consumer();
    ASSERT_NE(consumer, INVALID_CONSUMER_ID);
    
    // 生产者线程
    std::atomic<int> produced_count(0);
    Timestamp start_time = Timestamp::now();
    
    std::thread producer_thread([&]() {
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto buffer_ptr = shm_manager_->allocate(64);
            ASSERT_TRUE(buffer_ptr.valid());
            
            // 设置时间戳（递增的时间戳）
            Timestamp ts = start_time + Timestamp::from_milliseconds(i * 10.0);  // 每 10ms 一个
            buffer_ptr.set_timestamp(ts);
            
            // 写入数据
            int* data = buffer_ptr.as<int>();
            *data = i;
            
            // 推送到队列
            ASSERT_TRUE(queue.push(buffer_ptr.id()));
            
            produced_count.fetch_add(1);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        
        std::cout << "生产者完成\n";
    });
    
    // 消费者线程
    std::atomic<int> consumed_count(0);
    Timestamp last_timestamp;
    bool first_timestamp = true;
    
    std::thread consumer_thread([&]() {
        while (consumed_count.load() < NUM_BUFFERS) {
            BufferId buffer_id;
            if (queue.pop(consumer, buffer_id)) {
                BufferPtr buffer(buffer_id, allocator_);
                ASSERT_TRUE(buffer.valid());
                
                // 验证时间戳
                Timestamp ts = buffer.timestamp();
                ASSERT_TRUE(ts.valid());
                
                if (!first_timestamp) {
                    // 验证时间戳是递增的（允许小的误差）
                    EXPECT_GE(ts.to_nanoseconds(), last_timestamp.to_nanoseconds() - 1000000);  // 1ms 误差
                } else {
                    first_timestamp = false;
                }
                
                last_timestamp = ts;
                consumed_count.fetch_add(1);
                
                if (consumed_count.load() % 50 == 0) {
                    std::cout << "已消费 " << consumed_count.load() << " 个 Buffer，"
                              << "最新时间戳: " << ts.to_milliseconds() << " ms\n";
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        std::cout << "消费者完成\n";
    });
    
    // 等待完成
    producer_thread.join();
    consumer_thread.join();
    
    // 验证
    EXPECT_EQ(produced_count.load(), NUM_BUFFERS);
    EXPECT_EQ(consumed_count.load(), NUM_BUFFERS);
    
    std::cout << "✅ 持续流 + 时间戳验证测试完成\n";
    
    // 清理
    queue.unregister_consumer(consumer);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


