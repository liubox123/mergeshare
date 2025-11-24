/**
 * @file test_buffer_pool.cpp
 * @brief 测试 Buffer Pool
 */

#include <multiqueue/buffer_pool.hpp>
#include <gtest/gtest.h>
#include <cstring>

using namespace multiqueue;

class BufferPoolTest : public ::testing::Test {
protected:
    static constexpr const char* POOL_NAME = "test_buffer_pool";
    static constexpr size_t BLOCK_SIZE = 4096;
    static constexpr size_t BLOCK_COUNT = 16;
    
    void SetUp() override {
        // 清理旧的共享内存
        boost::interprocess::shared_memory_object::remove(POOL_NAME);
    }
    
    void TearDown() override {
        // 清理共享内存
        boost::interprocess::shared_memory_object::remove(POOL_NAME);
    }
};

TEST_F(BufferPoolTest, CreateAndOpen) {
    // 创建 Pool
    BufferPool pool1;
    ASSERT_TRUE(pool1.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
    EXPECT_TRUE(pool1.is_valid());
    EXPECT_EQ(pool1.get_block_size(), BLOCK_SIZE);
    EXPECT_EQ(pool1.get_block_count(), BLOCK_COUNT);
    EXPECT_EQ(pool1.get_free_count(), BLOCK_COUNT);
    
    // 打开 Pool
    BufferPool pool2;
    ASSERT_TRUE(pool2.open(POOL_NAME));
    EXPECT_TRUE(pool2.is_valid());
    EXPECT_EQ(pool2.get_block_size(), BLOCK_SIZE);
    EXPECT_EQ(pool2.get_block_count(), BLOCK_COUNT);
}

TEST_F(BufferPoolTest, AllocateAndFree) {
    BufferPool pool;
    ASSERT_TRUE(pool.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
    
    // 分配块
    int32_t block1 = pool.allocate_block();
    EXPECT_GE(block1, 0);
    EXPECT_EQ(pool.get_free_count(), BLOCK_COUNT - 1);
    
    int32_t block2 = pool.allocate_block();
    EXPECT_GE(block2, 0);
    EXPECT_NE(block1, block2);
    EXPECT_EQ(pool.get_free_count(), BLOCK_COUNT - 2);
    
    // 释放块
    pool.free_block(block1);
    EXPECT_EQ(pool.get_free_count(), BLOCK_COUNT - 1);
    
    pool.free_block(block2);
    EXPECT_EQ(pool.get_free_count(), BLOCK_COUNT);
}

TEST_F(BufferPoolTest, DataAccess) {
    BufferPool pool;
    ASSERT_TRUE(pool.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
    
    // 分配块
    int32_t block_index = pool.allocate_block();
    ASSERT_GE(block_index, 0);
    
    // 获取数据指针并写入数据
    void* data = pool.get_block_data(block_index);
    ASSERT_NE(data, nullptr);
    
    const char* test_string = "Hello, Buffer Pool!";
    std::memcpy(data, test_string, strlen(test_string) + 1);
    
    // 读取数据
    const char* read_data = static_cast<const char*>(data);
    EXPECT_STREQ(read_data, test_string);
    
    // 释放块
    pool.free_block(block_index);
}

TEST_F(BufferPoolTest, MultipleAllocations) {
    BufferPool pool;
    ASSERT_TRUE(pool.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
    
    std::vector<int32_t> blocks;
    
    // 分配所有块
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        int32_t block = pool.allocate_block();
        ASSERT_GE(block, 0);
        blocks.push_back(block);
    }
    
    EXPECT_EQ(pool.get_free_count(), 0);
    
    // 尝试再分配（应该失败）
    int32_t block = pool.allocate_block();
    EXPECT_EQ(block, -1);
    
    // 释放所有块
    for (int32_t b : blocks) {
        pool.free_block(b);
    }
    
    EXPECT_EQ(pool.get_free_count(), BLOCK_COUNT);
}

TEST_F(BufferPoolTest, CrossProcessAccess) {
    // 进程 1 创建并写入数据
    {
        BufferPool pool1;
        ASSERT_TRUE(pool1.create(POOL_NAME, 0, BLOCK_SIZE, BLOCK_COUNT));
        
        int32_t block = pool1.allocate_block();
        ASSERT_GE(block, 0);
        
        void* data = pool1.get_block_data(block);
        const char* test_string = "Cross-process data";
        std::memcpy(data, test_string, strlen(test_string) + 1);
    }
    
    // 进程 2 打开并读取数据
    {
        BufferPool pool2;
        ASSERT_TRUE(pool2.open(POOL_NAME));
        
        // 注意：这里假设 block 0 被分配了
        void* data = pool2.get_block_data(0);
        const char* read_data = static_cast<const char*>(data);
        EXPECT_STREQ(read_data, "Cross-process data");
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

