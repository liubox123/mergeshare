/**
 * @file test_integration.cpp
 * @brief 完整的集成测试 - 测试所有组件协同工作
 * 
 * 测试：
 * 1. Runtime + Scheduler + Block + PortQueue + BufferPool + ShmManager
 * 2. 完整的数据流水线
 * 3. 详细的调度日志
 * 4. 验证调度行为符合预期
 */

#include <multiqueue/runtime.hpp>
#include <multiqueue/scheduler.hpp>
#include <multiqueue/block.hpp>
#include <multiqueue/blocks.hpp>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/port_queue.hpp>
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>

using namespace multiqueue;
using namespace multiqueue::blocks;
using namespace boost::interprocess;

/**
 * @brief 日志记录器
 */
class TestLogger {
public:
    static TestLogger& instance() {
        static TestLogger logger;
        return logger;
    }
    
    void log(const std::string& level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "[" << std::setw(13) << ms << "] "
                  << "[" << std::setw(5) << level << "] "
                  << message << std::endl;
        
        log_entries_.push_back({ms, level, message});
    }
    
    void info(const std::string& msg) { log("INFO", msg); }
    void debug(const std::string& msg) { log("DEBUG", msg); }
    void warn(const std::string& msg) { log("WARN", msg); }
    void error(const std::string& msg) { log("ERROR", msg); }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        log_entries_.clear();
    }
    
    size_t entry_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_entries_.size();
    }
    
    void save_to_file(const std::string& filename) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream file(filename);
        for (const auto& entry : log_entries_) {
            file << "[" << std::get<0>(entry) << "] "
                 << "[" << std::get<1>(entry) << "] "
                 << std::get<2>(entry) << "\n";
        }
    }
    
private:
    TestLogger() = default;
    
    mutable std::mutex mutex_;
    std::vector<std::tuple<uint64_t, std::string, std::string>> log_entries_;
};

#define LOG_INFO(msg) TestLogger::instance().info(msg)
#define LOG_DEBUG(msg) TestLogger::instance().debug(msg)
#define LOG_WARN(msg) TestLogger::instance().warn(msg)
#define LOG_ERROR(msg) TestLogger::instance().error(msg)

/**
 * @brief 带日志的测试 Source Block
 */
class LoggedSource : public SourceBlock {
public:
    LoggedSource(const BlockConfig& config, SharedBufferAllocator* allocator, 
                 size_t buffer_size, size_t num_buffers)
        : SourceBlock(config, allocator)
        , buffer_size_(buffer_size)
        , num_buffers_(num_buffers)
        , produced_count_(0)
    {
        LOG_INFO("LoggedSource: 构造 [" + config.name + "], 将产生 " + 
                 std::to_string(num_buffers) + " 个 buffer");
        
        // 添加输出端口
        PortConfig port_config;
        port_config.name = "out";
        port_config.type = PortType::OUTPUT;
        add_output_port(port_config);
        LOG_INFO("LoggedSource: 添加输出端口");
    }
    
    bool initialize() override {
        LOG_INFO("LoggedSource: 初始化 [" + config_.name + "]");
        set_state(BlockState::READY);
        return true;
    }
    
    bool start() override {
        LOG_INFO("LoggedSource: 启动 [" + config_.name + "]");
        set_state(BlockState::RUNNING);
        return true;
    }
    
    void stop() override {
        LOG_INFO("LoggedSource: 停止 [" + config_.name + "], 共产生 " + 
                 std::to_string(produced_count_.load()) + " 个 buffer");
        set_state(BlockState::STOPPED);
    }
    
    WorkResult work() override {
        size_t current = produced_count_.load(std::memory_order_relaxed);
        
        LOG_DEBUG("LoggedSource: work() 被调用 [" + config_.name + "], count=" + 
                  std::to_string(current) + "/" + std::to_string(num_buffers_));
        
        if (current >= num_buffers_) {
            LOG_INFO("LoggedSource: 完成所有生产 [" + config_.name + "]");
            return WorkResult::DONE;
        }
        
        // 分配 Buffer
        BufferId buffer_id = allocator_->allocate(buffer_size_);
        if (buffer_id == INVALID_BUFFER_ID) {
            LOG_WARN("LoggedSource: Buffer 分配失败 [" + config_.name + "]");
            return WorkResult::ERROR;
        }
        
        BufferPtr buffer(buffer_id, allocator_);
        
        // 填充数据
        uint32_t* data = buffer.as<uint32_t>();
        *data = static_cast<uint32_t>(current);
        
        LOG_DEBUG("LoggedSource: 生产 buffer #" + std::to_string(current) + 
                  ", data=" + std::to_string(*data));
        
        // 推送到输出端口
        auto out_port = get_output_port(0);
        if (!out_port || !out_port->queue()) {
            LOG_ERROR("LoggedSource: 输出端口不可用 [" + config_.name + "]");
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        if (!out_port->queue()->push(buffer_id)) {
            LOG_WARN("LoggedSource: 推送失败 [" + config_.name + "]");
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        produced_count_.fetch_add(1, std::memory_order_relaxed);
        LOG_DEBUG("LoggedSource: 成功推送 buffer #" + std::to_string(current));
        
        return WorkResult::OK;
    }
    
    size_t produced_count() const {
        return produced_count_.load(std::memory_order_relaxed);
    }
    
private:
    size_t buffer_size_;
    size_t num_buffers_;
    std::atomic<size_t> produced_count_;
};

/**
 * @brief 带日志的测试 Processing Block
 */
class LoggedAmplifier : public ProcessingBlock {
public:
    LoggedAmplifier(const BlockConfig& config, SharedBufferAllocator* allocator, float gain)
        : ProcessingBlock(config, allocator)
        , gain_(gain)
        , processed_count_(0)
    {
        LOG_INFO("LoggedAmplifier: 构造 [" + config.name + "], gain=" + 
                 std::to_string(gain));
        
        // 添加输入端口
        PortConfig in_config;
        in_config.name = "in";
        in_config.type = PortType::INPUT;
        add_input_port(in_config);
        
        // 添加输出端口
        PortConfig out_config;
        out_config.name = "out";
        out_config.type = PortType::OUTPUT;
        add_output_port(out_config);
        
        LOG_INFO("LoggedAmplifier: 添加输入/输出端口");
    }
    
    bool initialize() override {
        LOG_INFO("LoggedAmplifier: 初始化 [" + config_.name + "]");
        set_state(BlockState::READY);
        return true;
    }
    
    bool start() override {
        LOG_INFO("LoggedAmplifier: 启动 [" + config_.name + "]");
        set_state(BlockState::RUNNING);
        return true;
    }
    
    void stop() override {
        LOG_INFO("LoggedAmplifier: 停止 [" + config_.name + "], 共处理 " + 
                 std::to_string(processed_count_.load()) + " 个 buffer");
        set_state(BlockState::STOPPED);
    }
    
    WorkResult work() override {
        LOG_DEBUG("LoggedAmplifier: work() 被调用 [" + config_.name + "]");
        
        // 从输入端口读取
        auto in_port = get_input_port(0);
        if (!in_port || !in_port->queue()) {
            LOG_ERROR("LoggedAmplifier: 输入端口不可用");
            return WorkResult::ERROR;
        }
        
        BufferId input_buffer_id;
        if (!in_port->queue()->pop_with_timeout(input_buffer_id, 10)) {
            LOG_DEBUG("LoggedAmplifier: 无输入数据");
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        BufferPtr input_buffer(input_buffer_id, allocator_);
        const uint32_t* input_data = input_buffer.as<const uint32_t>();
        uint32_t input_value = *input_data;
        
        LOG_DEBUG("LoggedAmplifier: 读取输入 buffer, data=" + std::to_string(input_value));
        
        // 分配输出 Buffer
        BufferId output_buffer_id = allocator_->allocate(sizeof(uint32_t));
        if (output_buffer_id == INVALID_BUFFER_ID) {
            LOG_WARN("LoggedAmplifier: 输出 Buffer 分配失败");
            return WorkResult::ERROR;
        }
        
        BufferPtr output_buffer(output_buffer_id, allocator_);
        
        // 处理数据（放大）
        uint32_t* output_data = output_buffer.as<uint32_t>();
        *output_data = static_cast<uint32_t>(input_value * gain_);
        
        LOG_DEBUG("LoggedAmplifier: 处理数据 " + std::to_string(input_value) + 
                  " -> " + std::to_string(*output_data) + " (gain=" + 
                  std::to_string(gain_) + ")");
        
        // 推送到输出端口
        auto out_port = get_output_port(0);
        if (!out_port || !out_port->queue()) {
            LOG_ERROR("LoggedAmplifier: 输出端口不可用");
            return WorkResult::ERROR;
        }
        
        if (!out_port->queue()->push(output_buffer_id)) {
            LOG_WARN("LoggedAmplifier: 推送失败");
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        processed_count_.fetch_add(1, std::memory_order_relaxed);
        LOG_DEBUG("LoggedAmplifier: 成功处理并推送 buffer #" + 
                  std::to_string(processed_count_.load() - 1));
        
        return WorkResult::OK;
    }
    
    size_t processed_count() const {
        return processed_count_.load(std::memory_order_relaxed);
    }
    
    float gain() const { return gain_; }
    
private:
    float gain_;
    std::atomic<size_t> processed_count_;
};

/**
 * @brief 带日志的测试 Sink Block
 */
class LoggedSink : public SinkBlock {
public:
    LoggedSink(const BlockConfig& config, SharedBufferAllocator* allocator)
        : SinkBlock(config, allocator)
        , consumed_count_(0)
        , sum_(0)
    {
        LOG_INFO("LoggedSink: 构造 [" + config.name + "]");
        
        // 添加输入端口
        PortConfig port_config;
        port_config.name = "in";
        port_config.type = PortType::INPUT;
        add_input_port(port_config);
        LOG_INFO("LoggedSink: 添加输入端口");
    }
    
    bool initialize() override {
        LOG_INFO("LoggedSink: 初始化 [" + config_.name + "]");
        set_state(BlockState::READY);
        return true;
    }
    
    bool start() override {
        LOG_INFO("LoggedSink: 启动 [" + config_.name + "]");
        set_state(BlockState::RUNNING);
        return true;
    }
    
    void stop() override {
        LOG_INFO("LoggedSink: 停止 [" + config_.name + "], 共消费 " + 
                 std::to_string(consumed_count_.load()) + " 个 buffer, sum=" + 
                 std::to_string(sum_.load()));
        set_state(BlockState::STOPPED);
    }
    
    WorkResult work() override {
        LOG_DEBUG("LoggedSink: work() 被调用 [" + config_.name + "]");
        
        // 从输入端口读取
        auto in_port = get_input_port(0);
        if (!in_port || !in_port->queue()) {
            LOG_ERROR("LoggedSink: 输入端口不可用");
            return WorkResult::ERROR;
        }
        
        BufferId buffer_id;
        if (!in_port->queue()->pop_with_timeout(buffer_id, 10)) {
            LOG_DEBUG("LoggedSink: 无输入数据");
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        BufferPtr buffer(buffer_id, allocator_);
        const uint32_t* data = buffer.as<const uint32_t>();
        uint32_t value = *data;
        
        LOG_DEBUG("LoggedSink: 消费 buffer #" + 
                  std::to_string(consumed_count_.load()) + ", data=" + 
                  std::to_string(value));
        
        sum_.fetch_add(value, std::memory_order_relaxed);
        consumed_count_.fetch_add(1, std::memory_order_relaxed);
        
        return WorkResult::OK;
    }
    
    size_t consumed_count() const {
        return consumed_count_.load(std::memory_order_relaxed);
    }
    
    uint64_t sum() const {
        return sum_.load(std::memory_order_relaxed);
    }
    
private:
    std::atomic<size_t> consumed_count_;
    std::atomic<uint64_t> sum_;
};

/**
 * @brief 集成测试夹具
 */
class IntegrationTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_integration_registry";
    
    GlobalRegistry* registry_;
    shared_memory_object global_shm_;
    mapped_region global_region_;
    ProcessId process_id_;
    std::unique_ptr<SharedBufferAllocator> allocator_;
    std::unique_ptr<ShmManager> shm_manager_;
    std::unique_ptr<Scheduler> scheduler_;
    
    void SetUp() override {
        LOG_INFO("========== 开始新测试 ==========");
        TestLogger::instance().clear();
        
        // 清理旧的共享内存
        cleanup_shm();
        
        LOG_INFO("创建 GlobalRegistry...");
        global_shm_ = shared_memory_object(
            create_only,
            GLOBAL_REGISTRY_NAME,
            read_write
        );
        global_shm_.truncate(sizeof(GlobalRegistry));
        global_region_ = mapped_region(global_shm_, read_write);
        
        registry_ = new (global_region_.get_address()) GlobalRegistry();
        registry_->initialize();
        LOG_INFO("GlobalRegistry 创建成功");
        
        // 注册进程
        int32_t process_slot = registry_->process_registry.register_process("IntegrationTest");
        ASSERT_GE(process_slot, 0);
        process_id_ = registry_->process_registry.processes[process_slot].process_id;
        LOG_INFO("进程注册成功, ProcessId=" + std::to_string(process_id_));
        
        // 创建 SharedBufferAllocator
        allocator_ = std::make_unique<SharedBufferAllocator>(registry_, process_id_);
        LOG_INFO("SharedBufferAllocator 创建成功");
        
        // 创建 ShmManager
        shm_manager_ = std::make_unique<ShmManager>(registry_, process_id_);
        ASSERT_TRUE(shm_manager_->initialize());
        LOG_INFO("ShmManager 初始化成功");
        
        // 创建 Scheduler
        SchedulerConfig scheduler_config;
        scheduler_config.num_threads = 2;  // 2 个工作线程
        scheduler_config.idle_sleep_ms = 1;
        scheduler_ = std::make_unique<Scheduler>(scheduler_config);
        LOG_INFO("Scheduler 创建成功 (2 个工作线程)");
    }
    
    void TearDown() override {
        LOG_INFO("========== 测试结束，清理资源 ==========");
        
        if (scheduler_ && scheduler_->is_running()) {
            scheduler_->stop();
        }
        
        scheduler_.reset();
        shm_manager_.reset();
        allocator_.reset();
        
        cleanup_shm();
        
        // 保存日志到文件
        TestLogger::instance().save_to_file("integration_test.log");
        LOG_INFO("日志已保存到 integration_test.log");
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
        shared_memory_object::remove("mqshm_large");
        
        for (int i = 0; i < 10; ++i) {
            std::string queue_name = "test_queue_" + std::to_string(i);
            shared_memory_object::remove(queue_name.c_str());
        }
    }
};

/**
 * @brief 测试1: 简单的 Source -> Sink 流水线
 */
TEST_F(IntegrationTest, SimpleSourceToSink) {
    LOG_INFO("===== 测试: Source -> Sink 流水线 =====");
    
    // 创建 Block
    BlockConfig source_config{"TestSource", BlockType::SOURCE};
    BlockConfig sink_config{"TestSink", BlockType::SINK};
    
    auto source = std::make_unique<LoggedSource>(source_config, allocator_.get(), 
                                                   sizeof(uint32_t), 5);
    auto sink = std::make_unique<LoggedSink>(sink_config, allocator_.get());
    
    // 设置 Block ID（通常由 Runtime 或 Registry 分配）
    source->set_id(1);
    sink->set_id(2);
    LOG_INFO("设置 Block ID: source=1, sink=2");
    
    // 初始化
    ASSERT_TRUE(source->initialize());
    ASSERT_TRUE(sink->initialize());
    
    // 创建连接队列
    auto queue = std::make_unique<PortQueue>();
    ASSERT_TRUE(queue->create("test_queue_0", 0, 16));
    LOG_INFO("PortQueue 创建成功");
    
    // 连接端口
    LOG_INFO("准备连接端口...");
    auto src_out = source->get_output_port(0);
    LOG_INFO("获取 source 输出端口: " + std::string(src_out ? "OK" : "NULL"));
    
    auto sink_in = sink->get_input_port(0);
    LOG_INFO("获取 sink 输入端口: " + std::string(sink_in ? "OK" : "NULL"));
    
    if (src_out) {
        src_out->set_queue(queue.get());
        LOG_INFO("Source 输出端口连接完成");
    }
    
    if (sink_in) {
        sink_in->set_queue(queue.get());
        LOG_INFO("Sink 输入端口连接完成");
    }
    
    LOG_INFO("端口连接完成");
    
    // 启动
    ASSERT_TRUE(source->start());
    ASSERT_TRUE(sink->start());
    
    // 注册到 Scheduler
    ASSERT_TRUE(scheduler_->register_block(source.get()));
    ASSERT_TRUE(scheduler_->register_block(sink.get()));
    LOG_INFO("Block 已注册到 Scheduler");
    
    // 启动 Scheduler
    ASSERT_TRUE(scheduler_->start());
    LOG_INFO("Scheduler 已启动");
    
    // 等待 Source 完成
    for (int i = 0; i < 100 && source->state() == BlockState::RUNNING; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    LOG_INFO("等待数据流完...");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 停止 Scheduler
    scheduler_->stop();
    LOG_INFO("Scheduler 已停止");
    
    // 验证结果
    LOG_INFO("===== 验证结果 =====");
    LOG_INFO("Source 产生: " + std::to_string(source->produced_count()) + " 个 buffer");
    LOG_INFO("Sink 消费: " + std::to_string(sink->consumed_count()) + " 个 buffer");
    
    EXPECT_EQ(source->produced_count(), 5);
    EXPECT_EQ(sink->consumed_count(), 5);
    
    // 验证数据正确性 (0+1+2+3+4 = 10)
    EXPECT_EQ(sink->sum(), 10);
    LOG_INFO("数据总和: " + std::to_string(sink->sum()) + " (预期 10)");
    
    LOG_INFO("===== 测试通过 =====");
}

/**
 * @brief 测试2: Source -> Amplifier -> Sink 流水线
 */
TEST_F(IntegrationTest, SourceAmplifierSink) {
    LOG_INFO("===== 测试: Source -> Amplifier -> Sink 流水线 =====");
    
    // 创建 Block
    BlockConfig source_config{"TestSource", BlockType::SOURCE};
    BlockConfig amp_config{"TestAmplifier", BlockType::PROCESSING};
    BlockConfig sink_config{"TestSink", BlockType::SINK};
    
    auto source = std::make_unique<LoggedSource>(source_config, allocator_.get(), 
                                                   sizeof(uint32_t), 10);
    auto amplifier = std::make_unique<LoggedAmplifier>(amp_config, allocator_.get(), 2.0f);
    auto sink = std::make_unique<LoggedSink>(sink_config, allocator_.get());
    
    // 设置 Block ID（通常由 Runtime 或 Registry 分配）
    source->set_id(1);
    amplifier->set_id(2);
    sink->set_id(3);
    LOG_INFO("设置 Block ID: source=1, amplifier=2, sink=3");
    
    // 初始化
    ASSERT_TRUE(source->initialize());
    ASSERT_TRUE(amplifier->initialize());
    ASSERT_TRUE(sink->initialize());
    
    // 创建连接队列
    auto queue1 = std::make_unique<PortQueue>();
    auto queue2 = std::make_unique<PortQueue>();
    ASSERT_TRUE(queue1->create("test_queue_1", 0, 16));
    ASSERT_TRUE(queue2->create("test_queue_2", 1, 16));
    LOG_INFO("PortQueue 创建成功");
    
    // 连接端口: source -> amplifier -> sink
    source->get_output_port(0)->set_queue(queue1.get());
    amplifier->get_input_port(0)->set_queue(queue1.get());
    amplifier->get_output_port(0)->set_queue(queue2.get());
    sink->get_input_port(0)->set_queue(queue2.get());
    LOG_INFO("端口连接完成: Source -> Amplifier -> Sink");
    
    // 启动
    ASSERT_TRUE(source->start());
    ASSERT_TRUE(amplifier->start());
    ASSERT_TRUE(sink->start());
    
    // 注册到 Scheduler
    ASSERT_TRUE(scheduler_->register_block(source.get()));
    ASSERT_TRUE(scheduler_->register_block(amplifier.get()));
    ASSERT_TRUE(scheduler_->register_block(sink.get()));
    LOG_INFO("所有 Block 已注册到 Scheduler");
    
    // 启动 Scheduler
    ASSERT_TRUE(scheduler_->start());
    LOG_INFO("Scheduler 已启动 (2 个工作线程)");
    
    // 等待 Source 完成
    for (int i = 0; i < 100 && source->state() == BlockState::RUNNING; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    LOG_INFO("等待数据流完...");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 停止 Scheduler
    scheduler_->stop();
    LOG_INFO("Scheduler 已停止");
    
    // 验证结果
    LOG_INFO("===== 验证结果 =====");
    LOG_INFO("Source 产生: " + std::to_string(source->produced_count()) + " 个 buffer");
    LOG_INFO("Amplifier 处理: " + std::to_string(amplifier->processed_count()) + " 个 buffer");
    LOG_INFO("Sink 消费: " + std::to_string(sink->consumed_count()) + " 个 buffer");
    
    EXPECT_EQ(source->produced_count(), 10);
    EXPECT_EQ(amplifier->processed_count(), 10);
    EXPECT_EQ(sink->consumed_count(), 10);
    
    // 验证数据正确性
    // 原始数据: 0,1,2,3,4,5,6,7,8,9
    // 放大 2倍: 0,2,4,6,8,10,12,14,16,18
    // 总和: 90
    uint64_t expected_sum = 0 + 2 + 4 + 6 + 8 + 10 + 12 + 14 + 16 + 18;
    EXPECT_EQ(sink->sum(), expected_sum);
    LOG_INFO("数据总和: " + std::to_string(sink->sum()) + " (预期 " + 
             std::to_string(expected_sum) + ")");
    
    LOG_INFO("===== 测试通过 =====");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

