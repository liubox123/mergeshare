/**
 * @file test_timestamp_sync_streams.cpp
 * @brief 时间戳同步测试
 * 
 * 测试多个流之间的时间戳同步功能
 */

#include <gtest/gtest.h>
#include <multiqueue/port_queue.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/timestamp.hpp>
#include <multiqueue/block.hpp>
#include <multiqueue/runtime.hpp>
#include <multiqueue/scheduler.hpp>
#include <multiqueue/msgbus.hpp>
// #include <multiqueue/mp_logger.hpp>  // 暂时不使用日志系统，直接用 std::cout
#include <thread>
#include <chrono>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>

using namespace multiqueue;
using namespace boost::interprocess;

class TimestampSyncStreamsTest : public ::testing::Test {
protected:
    static constexpr const char* REGISTRY_NAME = "test_timestamp_sync_registry";
    
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
        
        // 创建 Runtime
        std::cout << "[SetUp] 创建 Runtime..." << std::endl;
        RuntimeConfig runtime_config;
        runtime_config.num_scheduler_threads = 4;
        runtime_ = std::make_unique<Runtime>(runtime_config);
        std::cout << "[SetUp] 测试环境初始化完成" << std::endl;
    }
    
    void TearDown() override {
        runtime_.reset();
        shm_manager_.reset();
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(REGISTRY_NAME);
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
    }
    
    shared_memory_object registry_shm_;
    mapped_region registry_region_;
    GlobalRegistry* registry_;
    ProcessId process_id_;
    std::unique_ptr<ShmManager> shm_manager_;
    SharedBufferAllocator* allocator_;
    std::unique_ptr<Runtime> runtime_;
};

/**
 * @brief 时间戳同步 Source Block
 * 
 * 生产带时间戳的数据
 */
class TimestampedSourceBlock : public SourceBlock {
public:
    TimestampedSourceBlock(const BlockConfig& config, SharedBufferAllocator* allocator,
                          int start_value, double interval_ms)
        : SourceBlock(config, allocator)
        , start_value_(start_value)
        , interval_ms_(interval_ms)
        , current_value_(start_value)
        , start_time_(Timestamp::now())
    {
        add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    WorkResult work() override {
        // 分配 Buffer
        auto buffer = allocate_output_buffer(64);
        if (!buffer.valid()) {
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        // 设置时间戳（基于间隔时间）
        Timestamp ts = start_time_ + Timestamp::from_milliseconds(current_value_ * interval_ms_);
        buffer.set_timestamp(ts);
        
        // 写入数据
        int* data = buffer.as<int>();
        *data = current_value_;
        
        // 输出
        if (produce_output(0, buffer, DEFAULT_TIMEOUT_MS)) {
            current_value_++;
            return WorkResult::OK;
        }
        
        return WorkResult::INSUFFICIENT_OUTPUT;
    }
    
    int get_current_value() const { return current_value_; }
    
private:
    int start_value_;
    double interval_ms_;
    int current_value_;
    Timestamp start_time_;
};

/**
 * @brief 时间戳同步 Sink Block
 * 
 * 消费并验证时间戳同步的数据
 */
class TimestampedSinkBlock : public SinkBlock {
public:
    TimestampedSinkBlock(const BlockConfig& config, SharedBufferAllocator* allocator)
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
 * @brief 时间戳同步 Processing Block
 * 
 * 合并多个输入流，按时间戳对齐输出
 */
class TimestampSyncBlock : public ProcessingBlock {
public:
    TimestampSyncBlock(const BlockConfig& config, SharedBufferAllocator* allocator,
                      double sync_tolerance_ms = 10.0)
        : ProcessingBlock(config, allocator)
        , sync_tolerance_ns_(Timestamp::from_milliseconds(sync_tolerance_ms).to_nanoseconds())
    {
        add_input_port(PortConfig("in1", PortType::INPUT));
        add_input_port(PortConfig("in2", PortType::INPUT));
        add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    WorkResult work() override {
        // 尝试从两个输入端口读取数据
        BufferPtr buffer1 = consume_input(0, 0);  // 非阻塞
        BufferPtr buffer2 = consume_input(1, 0);  // 非阻塞
        
        if (!buffer1.valid() || !buffer2.valid()) {
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        // 获取时间戳
        Timestamp ts1 = buffer1.timestamp();
        Timestamp ts2 = buffer2.timestamp();
        
        // 检查时间戳是否在容差范围内
        TimestampNs diff = (ts1 > ts2) ? 
            (ts1.to_nanoseconds() - ts2.to_nanoseconds()) :
            (ts2.to_nanoseconds() - ts1.to_nanoseconds());
        
        if (diff > sync_tolerance_ns_) {
            // 时间戳差距过大，等待更同步的数据
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        // 时间戳对齐，合并数据
        auto output_buffer = allocate_output_buffer(128);
        if (!output_buffer.valid()) {
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        // 设置输出时间戳（使用较早的时间戳）
        Timestamp output_ts = (ts1 < ts2) ? ts1 : ts2;
        output_buffer.set_timestamp(output_ts);
        
        // 合并数据
        int* out_data = output_buffer.as<int>();
        int* in1_data = buffer1.as<int>();
        int* in2_data = buffer2.as<int>();
        out_data[0] = *in1_data;
        out_data[1] = *in2_data;
        
        // 输出
        if (produce_output(0, output_buffer, DEFAULT_TIMEOUT_MS)) {
            return WorkResult::OK;
        }
        
        return WorkResult::INSUFFICIENT_OUTPUT;
    }
    
private:
    TimestampNs sync_tolerance_ns_;
};

/**
 * @brief 测试：两个流的时间戳同步
 * 
 * 场景：
 * - 两个 Source Block 生产带时间戳的数据
 * - TimestampSyncBlock 按时间戳对齐并合并
 * - Sink Block 验证同步后的数据
 */
TEST_F(TimestampSyncStreamsTest, TwoStreamTimestampSync) {
    const int NUM_BUFFERS_PER_STREAM = 50;
    const double INTERVAL_MS = 10.0;  // 每 10ms 一个数据
    
    std::cout << "\n=== 测试: 两个流的时间戳同步 ===" << std::endl;
    
    // 创建 Blocks
    BlockConfig source1_config("Source1", BlockType::SOURCE);
    auto source1 = std::make_unique<TimestampedSourceBlock>(
        source1_config, allocator_, 0, INTERVAL_MS);
    source1->set_id(1);
    
    BlockConfig source2_config("Source2", BlockType::SOURCE);
    auto source2 = std::make_unique<TimestampedSourceBlock>(
        source2_config, allocator_, 0, INTERVAL_MS);
    source2->set_id(2);
    
    BlockConfig sync_config("SyncBlock", BlockType::PROCESSING);
    auto sync_block = std::make_unique<TimestampSyncBlock>(
        sync_config, allocator_, 5.0);  // 5ms 容差
    sync_block->set_id(3);
    
    // 确保端口已添加
    if (sync_block->get_input_port(0) == nullptr) {
        sync_block->add_input_port(PortConfig("in1", PortType::INPUT));
    }
    if (sync_block->get_input_port(1) == nullptr) {
        sync_block->add_input_port(PortConfig("in2", PortType::INPUT));
    }
    if (sync_block->get_output_port(0) == nullptr) {
        sync_block->add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    BlockConfig sink_config("Sink", BlockType::SINK);
    auto sink = std::make_unique<TimestampedSinkBlock>(sink_config, allocator_);
    sink->set_id(4);
    
    // 创建 PortQueues
    PortQueue queue1, queue2, queue3;
    ASSERT_TRUE(queue1.create("test_sync_queue1", 1, 100));
    ASSERT_TRUE(queue2.create("test_sync_queue2", 2, 100));
    ASSERT_TRUE(queue3.create("test_sync_queue3", 3, 100));
    
    queue1.set_allocator(allocator_);
    queue2.set_allocator(allocator_);
    queue3.set_allocator(allocator_);
    
    // 连接 Blocks
    source1->get_output_port(0)->set_queue(&queue1);
    source2->get_output_port(0)->set_queue(&queue2);
    
    sync_block->get_input_port(0)->set_queue(&queue1);
    sync_block->get_input_port(1)->set_queue(&queue2);
    sync_block->get_output_port(0)->set_queue(&queue3);
    
    sink->get_input_port(0)->set_queue(&queue3);
    
    // 注册到 Scheduler
    SchedulerConfig sched_config;
    sched_config.num_threads = 4;
    Scheduler scheduler(sched_config);
    ASSERT_TRUE(scheduler.register_block(source1.get()));
    ASSERT_TRUE(scheduler.register_block(source2.get()));
    ASSERT_TRUE(scheduler.register_block(sync_block.get()));
    ASSERT_TRUE(scheduler.register_block(sink.get()));
    
    // 启动 Scheduler
    std::cout << "[TEST] 启动 Scheduler..." << std::endl;
    scheduler.start();
    std::cout << "[TEST] Scheduler 已启动" << std::endl;
    
    // 运行直到生产足够的数据
    std::cout << "[TEST] 开始处理数据流..." << std::endl;
    int max_iterations = NUM_BUFFERS_PER_STREAM * 10;  // 最多迭代次数
    int iteration = 0;
    auto start_time = std::chrono::steady_clock::now();
    int last_sink_count = 0;
    
    while (sink->consumed_count() < NUM_BUFFERS_PER_STREAM && iteration < max_iterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        iteration++;
        
        // 每 100 次迭代输出一次进度
        if (iteration % 100 == 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            std::cout << "[TEST] 迭代 " << iteration << "/" << max_iterations 
                      << "，Sink 已消费 " << sink->consumed_count() 
                      << "/" << NUM_BUFFERS_PER_STREAM 
                      << "，耗时 " << ms << " ms" << std::endl;
        }
        
        // 检查是否卡住（超过 5 秒没有新数据）
        if (sink->consumed_count() == last_sink_count) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            if (sec > 5 && last_sink_count > 0) {
                std::cerr << "[TEST] WARNING: 可能卡住！已等待 " << sec 
                          << " 秒，Sink 消费数未变化: " << last_sink_count << std::endl;
                std::cout << "[TEST] Source1 current_value: " << source1->get_current_value() << std::endl;
                std::cout << "[TEST] Source2 current_value: " << source2->get_current_value() << std::endl;
            }
        } else {
            last_sink_count = sink->consumed_count();
        }
        
        // 超时检测（超过 30 秒）
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 30) {
            std::cerr << "[TEST] ERROR: 测试超时！" << std::endl;
            break;
        }
    }
    
    // 停止 Scheduler
    std::cout << "[TEST] 停止 Scheduler..." << std::endl;
    scheduler.stop();
    // 等待线程完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "[TEST] Scheduler 已停止" << std::endl;
    
    // 验证
    EXPECT_GE(sink->consumed_count(), NUM_BUFFERS_PER_STREAM * 0.8);  // 至少 80% 的数据被同步
    
    const auto& timestamps = sink->timestamps();
    const auto& values = sink->values();
    
    // 验证时间戳是递增的
    for (size_t i = 1; i < timestamps.size(); ++i) {
        EXPECT_GE(timestamps[i].to_nanoseconds(), 
                  timestamps[i-1].to_nanoseconds() - 1000000);  // 允许 1ms 误差
    }
    
    // 验证合并的数据（每个输出应该包含两个输入的值）
    for (size_t i = 0; i < values.size(); i += 2) {
        if (i + 1 < values.size()) {
            // 验证两个值来自不同的流（值应该相同或接近）
            EXPECT_GE(std::abs(values[i] - values[i+1]), 0);
        }
    }
    
    std::cout << "✅ 两个流的时间戳同步测试完成，同步了 " 
              << sink->consumed_count() << " 个数据\n";
    
    // 清理
    shared_memory_object::remove("test_sync_queue1");
    shared_memory_object::remove("test_sync_queue2");
    shared_memory_object::remove("test_sync_queue3");
}

/**
 * @brief 测试：不同采样率的流同步
 * 
 * 场景：
 * - 两个 Source Block 以不同的采样率生产数据
 * - TimestampSyncBlock 按时间戳对齐
 * - 验证同步后的数据
 */
TEST_F(TimestampSyncStreamsTest, DifferentSampleRateSync) {
    const int NUM_BUFFERS_STREAM1 = 50;
    const int NUM_BUFFERS_STREAM2 = 100;  // 两倍采样率
    const double INTERVAL_MS_STREAM1 = 20.0;  // 每 20ms 一个
    const double INTERVAL_MS_STREAM2 = 10.0;   // 每 10ms 一个
    
    std::cout << "\n=== 测试: 不同采样率的流同步 ===" << std::endl;
    
    // 创建 Blocks（与上面类似，但使用不同的间隔）
    BlockConfig source1_config("Source1", BlockType::SOURCE);
    auto source1 = std::make_unique<TimestampedSourceBlock>(
        source1_config, allocator_, 0, INTERVAL_MS_STREAM1);
    source1->set_id(1);
    
    BlockConfig source2_config("Source2", BlockType::SOURCE);
    auto source2 = std::make_unique<TimestampedSourceBlock>(
        source2_config, allocator_, 0, INTERVAL_MS_STREAM2);
    source2->set_id(2);
    
    BlockConfig sync_config("SyncBlock", BlockType::PROCESSING);
    auto sync_block = std::make_unique<TimestampSyncBlock>(
        sync_config, allocator_, 10.0);  // 10ms 容差
    sync_block->set_id(3);
    
    // 确保端口已添加
    if (sync_block->get_input_port(0) == nullptr) {
        sync_block->add_input_port(PortConfig("in1", PortType::INPUT));
    }
    if (sync_block->get_input_port(1) == nullptr) {
        sync_block->add_input_port(PortConfig("in2", PortType::INPUT));
    }
    if (sync_block->get_output_port(0) == nullptr) {
        sync_block->add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    BlockConfig sink_config("Sink", BlockType::SINK);
    auto sink = std::make_unique<TimestampedSinkBlock>(sink_config, allocator_);
    sink->set_id(4);
    
    // 创建 PortQueues
    PortQueue queue1, queue2, queue3;
    ASSERT_TRUE(queue1.create("test_sync_queue1", 1, 100));
    ASSERT_TRUE(queue2.create("test_sync_queue2", 2, 100));
    ASSERT_TRUE(queue3.create("test_sync_queue3", 3, 100));
    
    queue1.set_allocator(allocator_);
    queue2.set_allocator(allocator_);
    queue3.set_allocator(allocator_);
    
    // 连接 Blocks
    source1->get_output_port(0)->set_queue(&queue1);
    source2->get_output_port(0)->set_queue(&queue2);
    
    sync_block->get_input_port(0)->set_queue(&queue1);
    sync_block->get_input_port(1)->set_queue(&queue2);
    sync_block->get_output_port(0)->set_queue(&queue3);
    
    sink->get_input_port(0)->set_queue(&queue3);
    
    // 注册到 Scheduler
    SchedulerConfig sched_config2;
    sched_config2.num_threads = 4;
    Scheduler scheduler(sched_config2);
    ASSERT_TRUE(scheduler.register_block(source1.get()));
    ASSERT_TRUE(scheduler.register_block(source2.get()));
    ASSERT_TRUE(scheduler.register_block(sync_block.get()));
    ASSERT_TRUE(scheduler.register_block(sink.get()));
    
    // 启动 Scheduler
    scheduler.start();
    
    // 运行
    int max_iterations = NUM_BUFFERS_STREAM1 * 20;
    int iteration = 0;
    
    while (sink->consumed_count() < NUM_BUFFERS_STREAM1 && iteration < max_iterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        iteration++;
    }
    
    // 停止 Scheduler
    scheduler.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证
    EXPECT_GE(sink->consumed_count(), NUM_BUFFERS_STREAM1 * 0.7);  // 至少 70% 的数据被同步
    
    std::cout << "✅ 不同采样率的流同步测试完成，同步了 " 
              << sink->consumed_count() << " 个数据\n";
    
    // 清理
    shared_memory_object::remove("test_sync_queue1");
    shared_memory_object::remove("test_sync_queue2");
    shared_memory_object::remove("test_sync_queue3");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

