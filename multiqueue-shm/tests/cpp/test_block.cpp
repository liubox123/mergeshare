/**
 * @file test_block.cpp
 * @brief 测试 Block 框架
 */

#include <multiqueue/block.hpp>
#include <multiqueue/blocks.hpp>
#include <multiqueue/buffer_allocator.hpp>
#include <multiqueue/global_registry.hpp>
#include <gtest/gtest.h>
#include <thread>

using namespace multiqueue;
using namespace multiqueue::blocks;
using namespace boost::interprocess;

class BlockTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_block_global_registry";
    static constexpr const char* POOL_NAME = "test_block_pool";
    static constexpr const char* QUEUE_NAME_PREFIX = "test_block_queue_";
    
    GlobalRegistry* registry_;
    shared_memory_object global_shm_;
    mapped_region global_region_;
    SharedBufferAllocator* allocator_;
    ProcessId process_id_;
    
    void SetUp() override {
        // 清理旧的共享内存
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
        
        // 创建全局注册表
        global_shm_ = shared_memory_object(
            create_only,
            GLOBAL_REGISTRY_NAME,
            read_write
        );
        global_shm_.truncate(sizeof(GlobalRegistry));
        global_region_ = mapped_region(global_shm_, read_write);
        
        registry_ = new (global_region_.get_address()) GlobalRegistry();
        registry_->initialize();
        
        // 注册进程
        int32_t process_slot = registry_->process_registry.register_process("TestProcess");
        ASSERT_GE(process_slot, 0);
        process_id_ = registry_->process_registry.processes[process_slot].process_id;
        
        // 创建 Buffer Pool
        BufferPool pool;
        ASSERT_TRUE(pool.create(POOL_NAME, 0, 4096, 64));
        
        // 注册 Pool
        registry_->buffer_pool_registry.register_pool(4096, 64, POOL_NAME);
        
        // 创建 Buffer 分配器
        allocator_ = new SharedBufferAllocator(registry_, process_id_);
        ASSERT_TRUE(allocator_->register_pool(0, POOL_NAME));
    }
    
    void TearDown() override {
        delete allocator_;
        
        // 清理共享内存
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
    }
    
    /**
     * @brief 创建并打开端口队列
     */
    std::unique_ptr<PortQueue> create_queue(const std::string& name) {
        auto queue = std::make_unique<PortQueue>();
        
        std::string full_name = QUEUE_NAME_PREFIX + name;
        shared_memory_object::remove(full_name.c_str());
        
        if (!queue->create(full_name.c_str(), 1, 16)) {
            return nullptr;
        }
        
        return queue;
    }
};

TEST_F(BlockTest, NullSourceConstruction) {
    NullSource source(allocator_, 1024, 10);
    
    EXPECT_EQ(source.type(), BlockType::SOURCE);
    EXPECT_EQ(source.output_port_count(), 1);
    EXPECT_EQ(source.input_port_count(), 0);
    EXPECT_EQ(source.produced_count(), 0);
}

TEST_F(BlockTest, NullSourceWork) {
    NullSource source(allocator_, 1024, 5);
    
    // 创建输出队列
    auto queue = create_queue("source_out");
    ASSERT_NE(queue, nullptr);
    
    // 连接端口
    source.get_output_port(0)->set_queue(queue.get());
    
    // 初始化
    ASSERT_TRUE(source.initialize());
    ASSERT_TRUE(source.start());
    
    // 执行 work 5 次
    for (size_t i = 0; i < 5; ++i) {
        WorkResult result = source.work();
        EXPECT_EQ(result, WorkResult::OK);
    }
    
    EXPECT_EQ(source.produced_count(), 5);
    
    // 第 6 次应该返回 DONE
    WorkResult result = source.work();
    EXPECT_EQ(result, WorkResult::DONE);
}

TEST_F(BlockTest, NullSinkConstruction) {
    NullSink sink(allocator_);
    
    EXPECT_EQ(sink.type(), BlockType::SINK);
    EXPECT_EQ(sink.input_port_count(), 1);
    EXPECT_EQ(sink.output_port_count(), 0);
    EXPECT_EQ(sink.consumed_count(), 0);
}

TEST_F(BlockTest, NullSinkWork) {
    NullSink sink(allocator_);
    
    // 创建输入队列
    auto queue = create_queue("sink_in");
    ASSERT_NE(queue, nullptr);
    
    // 连接端口
    sink.get_input_port(0)->set_queue(queue.get());
    
    // 初始化
    ASSERT_TRUE(sink.initialize());
    ASSERT_TRUE(sink.start());
    
    // 手动推送一些 Buffer
    for (size_t i = 0; i < 5; ++i) {
        BufferId buffer_id = allocator_->allocate(1024);
        ASSERT_NE(buffer_id, INVALID_BUFFER_ID);
        ASSERT_TRUE(queue->push(buffer_id));
    }
    
    // 执行 work 5 次
    for (size_t i = 0; i < 5; ++i) {
        WorkResult result = sink.work();
        EXPECT_EQ(result, WorkResult::OK);
    }
    
    EXPECT_EQ(sink.consumed_count(), 5);
}

TEST_F(BlockTest, AmplifierConstruction) {
    Amplifier amplifier(allocator_, 2.5f);
    
    EXPECT_EQ(amplifier.type(), BlockType::PROCESSING);
    EXPECT_EQ(amplifier.input_port_count(), 1);
    EXPECT_EQ(amplifier.output_port_count(), 1);
    EXPECT_FLOAT_EQ(amplifier.gain(), 2.5f);
    EXPECT_EQ(amplifier.processed_count(), 0);
}

TEST_F(BlockTest, AmplifierWork) {
    Amplifier amplifier(allocator_, 2.0f);
    
    // 创建输入和输出队列
    auto input_queue = create_queue("amp_in");
    auto output_queue = create_queue("amp_out");
    ASSERT_NE(input_queue, nullptr);
    ASSERT_NE(output_queue, nullptr);
    
    // 连接端口
    amplifier.get_input_port(0)->set_queue(input_queue.get());
    amplifier.get_output_port(0)->set_queue(output_queue.get());
    
    // 初始化
    ASSERT_TRUE(amplifier.initialize());
    ASSERT_TRUE(amplifier.start());
    
    // 创建输入 Buffer 并填充数据
    BufferId input_buffer_id = allocator_->allocate(sizeof(float) * 10);
    ASSERT_NE(input_buffer_id, INVALID_BUFFER_ID);
    
    {
        BufferPtr input_buffer(input_buffer_id, allocator_);
        float* data = input_buffer.as<float>();
        for (size_t i = 0; i < 10; ++i) {
            data[i] = static_cast<float>(i + 1);  // 1, 2, 3, ..., 10
        }
        
        // 推送到输入队列
        ASSERT_TRUE(input_queue->push(input_buffer_id));
    }
    
    // 执行 work
    WorkResult result = amplifier.work();
    EXPECT_EQ(result, WorkResult::OK);
    EXPECT_EQ(amplifier.processed_count(), 1);
    
    // 检查输出
    BufferId output_buffer_id;
    ASSERT_TRUE(output_queue->pop_with_timeout(output_buffer_id, 1000));
    
    {
        BufferPtr output_buffer(output_buffer_id, allocator_);
        const float* data = output_buffer.as<const float>();
        
        for (size_t i = 0; i < 10; ++i) {
            EXPECT_FLOAT_EQ(data[i], static_cast<float>((i + 1) * 2));  // 2, 4, 6, ..., 20
        }
    }
}

TEST_F(BlockTest, SourceToSinkPipeline) {
    // 创建 Source 和 Sink
    NullSource source(allocator_, 1024, 10);
    NullSink sink(allocator_);
    
    // 创建队列
    auto queue = create_queue("pipeline");
    ASSERT_NE(queue, nullptr);
    
    // 连接
    source.get_output_port(0)->set_queue(queue.get());
    sink.get_input_port(0)->set_queue(queue.get());
    
    // 初始化
    ASSERT_TRUE(source.initialize());
    ASSERT_TRUE(source.start());
    ASSERT_TRUE(sink.initialize());
    ASSERT_TRUE(sink.start());
    
    // 模拟流水线执行
    for (size_t i = 0; i < 10; ++i) {
        // Source 生产数据
        WorkResult source_result = source.work();
        EXPECT_EQ(source_result, WorkResult::OK);
        
        // Sink 消费数据
        WorkResult sink_result = sink.work();
        EXPECT_EQ(sink_result, WorkResult::OK);
    }
    
    EXPECT_EQ(source.produced_count(), 10);
    EXPECT_EQ(sink.consumed_count(), 10);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

