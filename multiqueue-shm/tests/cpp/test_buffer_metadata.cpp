/**
 * @file test_buffer_metadata.cpp
 * @brief 测试 Buffer 元数据
 */

#include <multiqueue/buffer_metadata.hpp>
#include <gtest/gtest.h>

using namespace multiqueue;

TEST(BufferMetadataTest, Construction) {
    BufferMetadata meta;
    
    EXPECT_EQ(meta.buffer_id, INVALID_BUFFER_ID);
    EXPECT_EQ(meta.pool_id, INVALID_POOL_ID);
    EXPECT_EQ(meta.get_ref_count(), 0);
    EXPECT_FALSE(meta.is_valid());
}

TEST(BufferMetadataTest, RefCount) {
    BufferMetadata meta;
    
    // 增加引用计数
    uint32_t ref1 = meta.add_ref();
    EXPECT_EQ(ref1, 1);
    EXPECT_EQ(meta.get_ref_count(), 1);
    
    uint32_t ref2 = meta.add_ref();
    EXPECT_EQ(ref2, 2);
    EXPECT_EQ(meta.get_ref_count(), 2);
    
    // 减少引用计数
    uint32_t ref3 = meta.remove_ref();
    EXPECT_EQ(ref3, 1);
    EXPECT_EQ(meta.get_ref_count(), 1);
    
    uint32_t ref4 = meta.remove_ref();
    EXPECT_EQ(ref4, 0);
    EXPECT_EQ(meta.get_ref_count(), 0);
}

TEST(BufferMetadataTest, Valid) {
    BufferMetadata meta;
    
    EXPECT_FALSE(meta.is_valid());
    
    meta.set_valid(true);
    EXPECT_TRUE(meta.is_valid());
    
    meta.set_valid(false);
    EXPECT_FALSE(meta.is_valid());
}

TEST(BufferMetadataTableTest, Initialization) {
    BufferMetadataTable table;
    table.initialize();
    
    EXPECT_EQ(table.get_allocated_count(), 0);
    EXPECT_GT(table.peek_next_buffer_id(), 0);
}

TEST(BufferMetadataTableTest, AllocateAndFree) {
    BufferMetadataTable table;
    table.initialize();
    
    // 分配槽位
    int32_t slot1 = table.allocate_slot();
    EXPECT_GE(slot1, 0);
    EXPECT_EQ(table.get_allocated_count(), 1);
    
    BufferId buffer_id1 = table.entries[slot1].buffer_id;
    EXPECT_NE(buffer_id1, INVALID_BUFFER_ID);
    
    // 再分配一个槽位
    int32_t slot2 = table.allocate_slot();
    EXPECT_GE(slot2, 0);
    EXPECT_NE(slot1, slot2);
    EXPECT_EQ(table.get_allocated_count(), 2);
    
    // 释放槽位
    table.free_slot(slot1);
    EXPECT_EQ(table.get_allocated_count(), 1);
    
    table.free_slot(slot2);
    EXPECT_EQ(table.get_allocated_count(), 0);
}

TEST(BufferMetadataTableTest, FindSlotById) {
    BufferMetadataTable table;
    table.initialize();
    
    // 分配槽位
    int32_t slot = table.allocate_slot();
    ASSERT_GE(slot, 0);
    
    table.entries[slot].set_valid(true);
    BufferId buffer_id = table.entries[slot].buffer_id;
    
    // 查找槽位
    int32_t found_slot = table.find_slot_by_id(buffer_id);
    EXPECT_EQ(found_slot, slot);
    
    // 查找不存在的 Buffer ID
    int32_t not_found = table.find_slot_by_id(999999);
    EXPECT_EQ(not_found, -1);
    
    // 清理
    table.free_slot(slot);
}

TEST(BufferMetadataTableTest, FullTable) {
    BufferMetadataTable table;
    table.initialize();
    
    // 分配所有槽位
    for (size_t i = 0; i < MAX_BUFFERS; ++i) {
        int32_t slot = table.allocate_slot();
        ASSERT_GE(slot, 0);
    }
    
    EXPECT_EQ(table.get_allocated_count(), MAX_BUFFERS);
    
    // 尝试再分配（应该失败）
    int32_t slot = table.allocate_slot();
    EXPECT_EQ(slot, -1);
    
    // 释放所有槽位
    for (int32_t i = 0; i < static_cast<int32_t>(MAX_BUFFERS); ++i) {
        table.free_slot(i);
    }
    
    EXPECT_EQ(table.get_allocated_count(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

