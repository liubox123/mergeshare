/**
 * @file test_types.cpp
 * @brief 测试基础类型定义
 */

#include <multiqueue/types.hpp>
#include <gtest/gtest.h>

using namespace multiqueue;

TEST(TypesTest, InvalidIds) {
    EXPECT_EQ(INVALID_PROCESS_ID, 0);
    EXPECT_EQ(INVALID_BLOCK_ID, 0);
    EXPECT_EQ(INVALID_PORT_ID, 0);
    EXPECT_EQ(INVALID_BUFFER_ID, 0);
}

TEST(TypesTest, Constants) {
    EXPECT_GT(MAX_PROCESSES, 0);
    EXPECT_GT(MAX_BLOCKS, 0);
    EXPECT_GT(MAX_BUFFERS, 0);
    EXPECT_EQ(CACHE_LINE_SIZE, 64);
}

TEST(TypesTest, MagicNumber) {
    EXPECT_EQ(SHM_MAGIC_NUMBER, 0x4D515348);  // "MQSH"
}

TEST(TypesTest, EnumTypes) {
    // 测试枚举类型可以正常使用
    BlockType bt = BlockType::SOURCE;
    EXPECT_EQ(bt, BlockType::SOURCE);
    
    PortType pt = PortType::INPUT;
    EXPECT_EQ(pt, PortType::INPUT);
    
    SyncMode sm = SyncMode::SYNC;
    EXPECT_EQ(sm, SyncMode::SYNC);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

