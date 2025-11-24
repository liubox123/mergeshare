/**
 * @file test_buffer_allocator.cpp
 * @brief 测试 SharedBufferAllocator
 */

#include <multiqueue/buffer_allocator.hpp>
#include <gtest/gtest.h>

using namespace multiqueue;
using namespace boost::interprocess;

class BufferAllocatorTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_global_registry";
    static constexpr const char* POOL_NAME = "test_pool_allocator";
    
    GlobalRegistry* registry_;
    shared_memory_object global_shm_;
    mapped_region global_region_;
    
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
        
        // 创建 Buffer Pool
        BufferPool pool;
        ASSERT_TRUE(pool.create(POOL_NAME, 0, 4096, 16));
        
        // 注册 Pool
        registry_->buffer_pool_registry.register_pool(4096, 16, POOL_NAME);
    }
    
    void TearDown() override {
        // 清理共享内存
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove(POOL_NAME);
    }
};

TEST_F(BufferAllocatorTest, Construction) {
    SharedBufferAllocator allocator(registry_, 1);
    
    // 注册 Pool
    ASSERT_TRUE(allocator.register_pool(0, POOL_NAME));
}

TEST_F(BufferAllocatorTest, AllocateAndDeallocate) {
    SharedBufferAllocator allocator(registry_, 1);
    ASSERT_TRUE(allocator.register_pool(0, POOL_NAME));
    
    // 分配 Buffer
    BufferId buffer_id = allocator.allocate(1024);
    EXPECT_NE(buffer_id, INVALID_BUFFER_ID);
    
    // 获取数据指针
    void* data = allocator.get_buffer_data(buffer_id);
    EXPECT_NE(data, nullptr);
    
    // 获取 Buffer 大小
    size_t size = allocator.get_buffer_size(buffer_id);
    EXPECT_EQ(size, 1024);
    
    // 获取引用计数（初始为 1）
    uint32_t ref_count = allocator.get_ref_count(buffer_id);
    EXPECT_EQ(ref_count, 1);
    
    // 手动释放
    bool should_free = allocator.remove_ref(buffer_id);
    EXPECT_TRUE(should_free);
    
    allocator.deallocate(buffer_id);
}

TEST_F(BufferAllocatorTest, RefCounting) {
    SharedBufferAllocator allocator(registry_, 1);
    ASSERT_TRUE(allocator.register_pool(0, POOL_NAME));
    
    // 分配 Buffer
    BufferId buffer_id = allocator.allocate(1024);
    ASSERT_NE(buffer_id, INVALID_BUFFER_ID);
    
    // 初始引用计数为 1
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 1);
    
    // 增加引用计数
    allocator.add_ref(buffer_id);
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 2);
    
    allocator.add_ref(buffer_id);
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 3);
    
    // 减少引用计数
    bool should_free = allocator.remove_ref(buffer_id);
    EXPECT_FALSE(should_free);
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 2);
    
    should_free = allocator.remove_ref(buffer_id);
    EXPECT_FALSE(should_free);
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 1);
    
    should_free = allocator.remove_ref(buffer_id);
    EXPECT_TRUE(should_free);
    EXPECT_EQ(allocator.get_ref_count(buffer_id), 0);
    
    // 释放
    allocator.deallocate(buffer_id);
}

TEST_F(BufferAllocatorTest, MultipleBuffers) {
    SharedBufferAllocator allocator(registry_, 1);
    ASSERT_TRUE(allocator.register_pool(0, POOL_NAME));
    
    std::vector<BufferId> buffers;
    
    // 分配多个 Buffer
    for (int i = 0; i < 10; ++i) {
        BufferId buffer_id = allocator.allocate(1024);
        ASSERT_NE(buffer_id, INVALID_BUFFER_ID);
        buffers.push_back(buffer_id);
    }
    
    // 检查所有 Buffer 都不同
    std::set<BufferId> unique_buffers(buffers.begin(), buffers.end());
    EXPECT_EQ(unique_buffers.size(), buffers.size());
    
    // 释放所有 Buffer
    for (BufferId buffer_id : buffers) {
        bool should_free = allocator.remove_ref(buffer_id);
        EXPECT_TRUE(should_free);
        allocator.deallocate(buffer_id);
    }
}

TEST_F(BufferAllocatorTest, Timestamp) {
    SharedBufferAllocator allocator(registry_, 1);
    ASSERT_TRUE(allocator.register_pool(0, POOL_NAME));
    
    // 分配 Buffer
    BufferId buffer_id = allocator.allocate(1024);
    ASSERT_NE(buffer_id, INVALID_BUFFER_ID);
    
    // 设置时间戳
    Timestamp ts = Timestamp::now();
    allocator.set_timestamp(buffer_id, ts);
    
    // 获取时间戳
    Timestamp retrieved_ts = allocator.get_timestamp(buffer_id);
    EXPECT_EQ(retrieved_ts.to_nanoseconds(), ts.to_nanoseconds());
    
    // 清理
    allocator.remove_ref(buffer_id);
    allocator.deallocate(buffer_id);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

