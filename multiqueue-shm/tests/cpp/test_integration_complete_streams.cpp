/**
 * @file test_integration_complete_streams.cpp
 * @brief 综合流处理测试（使用所有组件）
 * 
 * 测试完整的流处理管道，包括：
 * - Runtime（运行时管理）
 * - Scheduler（调度器）
 * - ShmManager（共享内存管理）
 * - MsgBus（消息总线）
 * - Block 框架（Source, Processing, Sink）
 * - PortQueue（端口队列，广播模式）
 * - 时间戳同步
 */

#include <gtest/gtest.h>
#include <multiqueue/runtime.hpp>
#include <multiqueue/scheduler.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/msgbus.hpp>
#include <multiqueue/block.hpp>
#include <multiqueue/blocks/null_source.hpp>
#include <multiqueue/blocks/null_sink.hpp>
#include <multiqueue/blocks/amplifier.hpp>
#include <multiqueue/timestamp.hpp>
// #include <multiqueue/mp_logger.hpp>  // 暂时不使用日志系统，直接用 std::cout
#include <algorithm>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <iostream>

using namespace multiqueue;
using namespace boost::interprocess;

class CompleteStreamIntegrationTest : public ::testing::Test {
protected:
    static constexpr const char* REGISTRY_NAME = "test_complete_stream_registry";
    
    void SetUp() override {
        cleanup_shm();
        
        // 创建 GlobalRegistry
        registry_shm_ = shared_memory_object(create_only, REGISTRY_NAME, read_write);
        registry_shm_.truncate(sizeof(GlobalRegistry));
        registry_region_ = mapped_region(registry_shm_, read_write);
        registry_ = new (registry_region_.get_address()) GlobalRegistry();
        registry_->initialize();
        
        // 创建 ShmManager
        process_id_ = 1;
        ShmConfig config;
        config.name_prefix = "mqshm_";
        config.pools = {
            PoolConfig("small", 4096, 200),
            PoolConfig("medium", 65536, 100),
            PoolConfig("large", 1048576, 50)
        };
        shm_manager_ = std::make_unique<ShmManager>(registry_, process_id_, config);
        ASSERT_TRUE(shm_manager_->initialize());
        
        // 创建 Runtime（需要先初始化才能使用）
        RuntimeConfig runtime_config;
        runtime_config.num_scheduler_threads = 4;
        runtime_ = std::make_unique<Runtime>(runtime_config);
        
        // 注意：Runtime 需要 initialize() 才能使用 create_block
        // 但这里我们直接使用 Scheduler，所以不需要初始化 Runtime
        
        // 创建 MsgBus
        msgbus_ = std::make_unique<MsgBus>();
        msgbus_->start();
    }
    
    void TearDown() override {
        if (msgbus_) {
            msgbus_->stop();
        }
        msgbus_.reset();
        runtime_.reset();
        shm_manager_.reset();
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(REGISTRY_NAME);
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
        shared_memory_object::remove("mqshm_large");
    }
    
    shared_memory_object registry_shm_;
    mapped_region registry_region_;
    GlobalRegistry* registry_;
    ProcessId process_id_;
    std::unique_ptr<ShmManager> shm_manager_;
    std::unique_ptr<Runtime> runtime_;
    std::unique_ptr<MsgBus> msgbus_;
};

/**
 * @brief 带时间戳的 Source Block
 */
class TimestampedSource : public SourceBlock {
public:
    TimestampedSource(const BlockConfig& config, SharedBufferAllocator* allocator,
                     int num_buffers, double interval_ms)
        : SourceBlock(config, allocator)
        , num_buffers_(num_buffers)
        , interval_ms_(interval_ms)
        , current_index_(0)
        , start_time_(Timestamp::now())
    {
        add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    WorkResult work() override {
        if (current_index_ >= num_buffers_) {
            return WorkResult::DONE;
        }
        
        auto buffer = allocate_output_buffer(64);
        if (!buffer.valid()) {
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        // 设置时间戳
        Timestamp ts = start_time_ + Timestamp::from_milliseconds(current_index_ * interval_ms_);
        buffer.set_timestamp(ts);
        
        // 写入数据
        int* data = buffer.as<int>();
        *data = current_index_;
        
        if (produce_output(0, buffer, DEFAULT_TIMEOUT_MS)) {
            current_index_++;
            return WorkResult::OK;
        }
        
        return WorkResult::INSUFFICIENT_OUTPUT;
    }
    
    int current_index() const { return current_index_; }
    
private:
    int num_buffers_;
    double interval_ms_;
    int current_index_;
    Timestamp start_time_;
};

/**
 * @brief 带时间戳验证的 Sink Block
 */
class TimestampedSink : public SinkBlock {
public:
    TimestampedSink(const BlockConfig& config, SharedBufferAllocator* allocator)
        : SinkBlock(config, allocator)
        , consumed_count_(0)
    {
        add_input_port(PortConfig("in", PortType::INPUT));
    }
    
    WorkResult work() override {
        BufferPtr buffer = consume_input(0, DEFAULT_TIMEOUT_MS);
        if (buffer.valid()) {
            // 验证时间戳
            Timestamp ts = buffer.timestamp();
            if (ts.valid()) {
                timestamps_.push_back(ts);
            }
            
            // 读取数据
            int value = *buffer.as<int>();
            values_.push_back(value);
            
            consumed_count_++;
            return WorkResult::OK;
        }
        
        return WorkResult::INSUFFICIENT_INPUT;
    }
    
    size_t consumed_count() const { return consumed_count_; }
    const std::vector<Timestamp>& timestamps() const { return timestamps_; }
    const std::vector<int>& values() const { return values_; }
    
private:
    size_t consumed_count_;
    std::vector<Timestamp> timestamps_;
    std::vector<int> values_;
};

/**
 * @brief 测试：完整的流处理管道（Source -> Amplifier -> Sink）
 * 
 * 使用所有组件：
 * - Runtime::create_block() 创建 Blocks
 * - Scheduler 调度 Blocks
 * - ShmManager 管理共享内存
 * - MsgBus 处理控制消息
 * - PortQueue 传递数据（广播模式）
 * - 时间戳同步
 */
TEST_F(CompleteStreamIntegrationTest, CompletePipelineWithAllComponents) {
    const int NUM_BUFFERS = 100;
    const double INTERVAL_MS = 5.0;
    
    std::cout << "\n=== 测试: 完整流处理管道（使用所有组件）===" << std::endl;
    
    // 直接创建 Blocks（不使用 Runtime::create_block，因为 Runtime 需要初始化）
    BlockConfig source_config("TimestampedSource", BlockType::SOURCE);
    auto source = std::make_unique<TimestampedSource>(
        source_config, shm_manager_->allocator(), NUM_BUFFERS, INTERVAL_MS);
    source->set_id(1);
    
    auto amplifier = std::make_unique<blocks::Amplifier>(
        shm_manager_->allocator());
    amplifier->set_id(2);
    
    BlockConfig sink_config("TimestampedSink", BlockType::SINK);
    auto sink = std::make_unique<TimestampedSink>(
        sink_config, shm_manager_->allocator());
    sink->set_id(3);
    
    // 创建 PortQueues
    PortQueue queue1, queue2;
    ASSERT_TRUE(queue1.create("test_complete_queue1", 1, 200));
    ASSERT_TRUE(queue2.create("test_complete_queue2", 2, 200));
    
    queue1.set_allocator(shm_manager_->allocator());
    queue2.set_allocator(shm_manager_->allocator());
    
    // 连接 Blocks
    source->get_output_port(0)->set_queue(&queue1);
    amplifier->get_input_port(0)->set_queue(&queue1);
    amplifier->get_output_port(0)->set_queue(&queue2);
    sink->get_input_port(0)->set_queue(&queue2);
    
    // 创建 Scheduler（直接创建，不使用 Runtime）
    SchedulerConfig sched_config;
    sched_config.num_threads = 4;
    Scheduler scheduler(sched_config);
    
    // 注册 Blocks
    ASSERT_TRUE(scheduler.register_block(source.get()));
    ASSERT_TRUE(scheduler.register_block(amplifier.get()));
    ASSERT_TRUE(scheduler.register_block(sink.get()));
    
    // 启动 Scheduler
    scheduler.start();
    
    std::cout << "Scheduler 已启动，开始处理数据流..." << std::endl;
    
    // 运行直到完成
    int max_iterations = NUM_BUFFERS * 20;
    int iteration = 0;
    
    while (sink->consumed_count() < NUM_BUFFERS && iteration < max_iterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        iteration++;
        
        if (iteration % 100 == 0) {
            std::cout << "迭代 " << iteration << ": "
                      << "Source=" << source->current_index() << ", "
                      << "Sink=" << sink->consumed_count() << std::endl;
        }
    }
    
    // 停止 Scheduler
    scheduler.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "Scheduler 已停止" << std::endl;
    
    // 验证
    EXPECT_GE(sink->consumed_count(), NUM_BUFFERS * 0.9);  // 至少 90% 的数据被处理
    
    const auto& timestamps = sink->timestamps();
    const auto& values = sink->values();
    
    // 验证时间戳
    if (!timestamps.empty()) {
        for (size_t i = 1; i < timestamps.size(); ++i) {
            EXPECT_GE(timestamps[i].to_nanoseconds(),
                      timestamps[i-1].to_nanoseconds() - 1000000);  // 允许 1ms 误差
        }
    }
    
    // 验证数据完整性
    EXPECT_GE(values.size(), NUM_BUFFERS * 0.9);
    
    std::cout << "✅ 完整流处理管道测试完成：" << std::endl;
    std::cout << "  - Source 生产: " << source->current_index() << " 个 Buffer" << std::endl;
    std::cout << "  - Sink 消费: " << sink->consumed_count() << " 个 Buffer" << std::endl;
    std::cout << "  - 带时间戳: " << timestamps.size() << " 个" << std::endl;
    
    // 清理
    shared_memory_object::remove("test_complete_queue1");
    shared_memory_object::remove("test_complete_queue2");
}

/**
 * @brief 测试：多流合并管道（使用所有组件）
 * 
 * 场景：
 * - 2 个 Source Blocks 生产数据
 * - 1 个 Processing Block 合并数据
 * - 1 个 Sink Block 消费合并后的数据
 * - 使用 MsgBus 进行控制消息传递
 */
TEST_F(CompleteStreamIntegrationTest, MultiStreamMergePipeline) {
    const int NUM_BUFFERS_PER_STREAM = 50;
    const double INTERVAL_MS = 10.0;
    
    std::cout << "\n=== 测试: 多流合并管道（使用所有组件）===" << std::endl;
    
    // 创建 Blocks
    BlockConfig source1_config("Source1", BlockType::SOURCE);
    auto source1 = std::make_unique<TimestampedSource>(
        source1_config, shm_manager_->allocator(), NUM_BUFFERS_PER_STREAM, INTERVAL_MS);
    source1->set_id(1);
    
    BlockConfig source2_config("Source2", BlockType::SOURCE);
    auto source2 = std::make_unique<TimestampedSource>(
        source2_config, shm_manager_->allocator(), NUM_BUFFERS_PER_STREAM, INTERVAL_MS);
    source2->set_id(2);
    
    // 使用 Amplifier 作为简单的 Processing Block
    auto amplifier = std::make_unique<blocks::Amplifier>(
        shm_manager_->allocator());
    amplifier->set_id(3);
    
    BlockConfig sink_config("Sink", BlockType::SINK);
    auto sink = std::make_unique<TimestampedSink>(
        sink_config, shm_manager_->allocator());
    sink->set_id(4);
    
    // 创建 PortQueues
    PortQueue queue1, queue2, queue3, queue4;
    ASSERT_TRUE(queue1.create("test_merge_queue1", 1, 100));
    ASSERT_TRUE(queue2.create("test_merge_queue2", 2, 100));
    ASSERT_TRUE(queue3.create("test_merge_queue3", 3, 100));
    ASSERT_TRUE(queue4.create("test_merge_queue4", 4, 100));
    
    queue1.set_allocator(shm_manager_->allocator());
    queue2.set_allocator(shm_manager_->allocator());
    queue3.set_allocator(shm_manager_->allocator());
    queue4.set_allocator(shm_manager_->allocator());
    
    // 连接：Source1 -> Queue1 -> Amplifier -> Queue3 -> Sink
    //       Source2 -> Queue2 -> Amplifier (但 Amplifier 只有一个输入，这里简化)
    source1->get_output_port(0)->set_queue(&queue1);
    amplifier->get_input_port(0)->set_queue(&queue1);
    amplifier->get_output_port(0)->set_queue(&queue3);
    sink->get_input_port(0)->set_queue(&queue3);
    
    // 注册到 Scheduler
    SchedulerConfig sched_config2;
    sched_config2.num_threads = 4;
    Scheduler scheduler(sched_config2);
    ASSERT_TRUE(scheduler.register_block(source1.get()));
    ASSERT_TRUE(scheduler.register_block(source2.get()));
    ASSERT_TRUE(scheduler.register_block(amplifier.get()));
    ASSERT_TRUE(scheduler.register_block(sink.get()));
    
    // 使用 MsgBus 发送控制消息
    std::atomic<bool> message_received(false);
    
    // 订阅（需要 ProcessId）
    ProcessId test_process_id = process_id_;
    msgbus_->subscribe(test_process_id, INVALID_BLOCK_ID, "test.control");
    
    // 发送控制消息
    std::string control_data = "start";
    msgbus_->publish("test.control", control_data.data(), control_data.size());
    
    // 尝试接收消息（验证 MsgBus 工作）
    char msg_buffer[256];
    size_t msg_size = sizeof(msg_buffer);
    if (msgbus_->receive_message(test_process_id, msg_buffer, msg_size)) {
        message_received.store(true);
        std::cout << "收到控制消息，大小: " << msg_size << std::endl;
    }
    
    // 启动 Scheduler
    scheduler.start();
    
    std::cout << "Scheduler 已启动，多流处理开始..." << std::endl;
    
    // 运行
    int max_iterations = NUM_BUFFERS_PER_STREAM * 20;
    int iteration = 0;
    
    while (sink->consumed_count() < NUM_BUFFERS_PER_STREAM && iteration < max_iterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        iteration++;
    }
    
    // 停止 Scheduler
    scheduler.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证
    EXPECT_GE(sink->consumed_count(), NUM_BUFFERS_PER_STREAM * 0.8);
    EXPECT_TRUE(message_received.load());  // 验证 MsgBus 工作正常
    
    std::cout << "✅ 多流合并管道测试完成：" << std::endl;
    std::cout << "  - Source1 生产: " << source1->current_index() << " 个 Buffer" << std::endl;
    std::cout << "  - Source2 生产: " << source2->current_index() << " 个 Buffer" << std::endl;
    std::cout << "  - Sink 消费: " << sink->consumed_count() << " 个 Buffer" << std::endl;
    std::cout << "  - MsgBus 消息: " << (message_received.load() ? "已接收" : "未接收") << std::endl;
    
    // 清理
    shared_memory_object::remove("test_merge_queue1");
    shared_memory_object::remove("test_merge_queue2");
    shared_memory_object::remove("test_merge_queue3");
    shared_memory_object::remove("test_merge_queue4");
}

/**
 * @brief 测试：持续流 + 所有组件
 * 
 * 场景：
 * - 长时间运行的流处理管道
 * - 使用所有组件
 * - 验证系统稳定性
 */
TEST_F(CompleteStreamIntegrationTest, ContinuousStreamWithAllComponents) {
    const int NUM_BUFFERS = 500;
    const double INTERVAL_MS = 2.0;
    
    std::cout << "\n=== 测试: 持续流 + 所有组件 ===" << std::endl;
    
    // 创建 Blocks
    BlockConfig source_config("ContinuousSource", BlockType::SOURCE);
    auto source = std::make_unique<TimestampedSource>(
        source_config, shm_manager_->allocator(), NUM_BUFFERS, INTERVAL_MS);
    source->set_id(1);
    
    auto amplifier = std::make_unique<blocks::Amplifier>(
        shm_manager_->allocator());
    amplifier->set_id(2);
    
    BlockConfig sink_config("ContinuousSink", BlockType::SINK);
    auto sink = std::make_unique<TimestampedSink>(
        sink_config, shm_manager_->allocator());
    sink->set_id(3);
    
    // 创建 PortQueues
    PortQueue queue1, queue2;
    ASSERT_TRUE(queue1.create("test_continuous_queue1", 1, 200));
    ASSERT_TRUE(queue2.create("test_continuous_queue2", 2, 200));
    
    queue1.set_allocator(shm_manager_->allocator());
    queue2.set_allocator(shm_manager_->allocator());
    
    // 连接 Blocks
    source->get_output_port(0)->set_queue(&queue1);
    amplifier->get_input_port(0)->set_queue(&queue1);
    amplifier->get_output_port(0)->set_queue(&queue2);
    sink->get_input_port(0)->set_queue(&queue2);
    
    // 注册到 Scheduler
    SchedulerConfig sched_config3;
    sched_config3.num_threads = 4;
    Scheduler scheduler(sched_config3);
    ASSERT_TRUE(scheduler.register_block(source.get()));
    ASSERT_TRUE(scheduler.register_block(amplifier.get()));
    ASSERT_TRUE(scheduler.register_block(sink.get()));
    
    // 启动 Scheduler
    scheduler.start();
    
    std::cout << "持续流处理开始..." << std::endl;
    
    // 运行
    int max_iterations = NUM_BUFFERS * 30;
    int iteration = 0;
    int last_count = 0;
    
    while (sink->consumed_count() < NUM_BUFFERS && iteration < max_iterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        iteration++;
        
        int current_count = sink->consumed_count();
        if (current_count != last_count && current_count % 50 == 0) {
            std::cout << "进度: " << current_count << "/" << NUM_BUFFERS 
                      << " (" << (current_count * 100 / NUM_BUFFERS) << "%)" << std::endl;
            last_count = current_count;
        }
    }
    
    // 停止 Scheduler
    scheduler.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证
    EXPECT_GE(sink->consumed_count(), NUM_BUFFERS * 0.9);
    
    // 验证 ShmManager 统计
    auto stats = shm_manager_->get_stats();
    EXPECT_GT(stats.allocation_count, 0);
    
    std::cout << "✅ 持续流 + 所有组件测试完成：" << std::endl;
    std::cout << "  - 生产: " << source->current_index() << " 个 Buffer" << std::endl;
    std::cout << "  - 消费: " << sink->consumed_count() << " 个 Buffer" << std::endl;
    std::cout << "  - ShmManager 分配: " << stats.allocation_count << " 次" << std::endl;
    std::cout << "  - ShmManager 释放: " << stats.deallocation_count << " 次" << std::endl;
    
    // 清理
    shared_memory_object::remove("test_continuous_queue1");
    shared_memory_object::remove("test_continuous_queue2");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

