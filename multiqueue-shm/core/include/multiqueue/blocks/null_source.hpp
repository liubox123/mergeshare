/**
 * @file null_source.hpp
 * @brief Null Source Block - 生成空数据用于测试
 */

#pragma once

#include "../block.hpp"
#include <cstring>

namespace multiqueue {
namespace blocks {

/**
 * @brief Null Source Block
 * 
 * 生成指定大小的空 Buffer，用于测试和基准测试
 */
class NullSource : public SourceBlock {
public:
    /**
     * @brief 构造函数
     * 
     * @param allocator Buffer 分配器
     * @param buffer_size 生成的 Buffer 大小
     * @param num_buffers 生成的 Buffer 数量（0 表示无限）
     */
    NullSource(SharedBufferAllocator* allocator,
               size_t buffer_size = 1024,
               size_t num_buffers = 0)
        : SourceBlock(BlockConfig("NullSource", BlockType::SOURCE), allocator)
        , buffer_size_(buffer_size)
        , num_buffers_(num_buffers)
        , produced_count_(0)
    {
        // 添加输出端口
        add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    bool initialize() override {
        produced_count_ = 0;
        return true;
    }
    
    WorkResult work() override {
        // 检查是否达到限制
        if (num_buffers_ > 0 && produced_count_ >= num_buffers_) {
            return WorkResult::DONE;
        }
        
        // 分配 Buffer
        BufferPtr buffer = allocate_output_buffer(buffer_size_);
        if (!buffer.valid()) {
            return WorkResult::ERROR;
        }
        
        // 填充数据（全零）
        void* data = buffer.data();
        std::memset(data, 0, buffer_size_);
        
        // 设置时间戳
        buffer.set_timestamp(Timestamp::now());
        
        // 输出
        if (!produce_output(0, buffer, DEFAULT_TIMEOUT_MS)) {
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        ++produced_count_;
        return WorkResult::OK;
    }
    
    /**
     * @brief 获取已生成的 Buffer 数量
     */
    size_t produced_count() const { return produced_count_; }
    
private:
    size_t buffer_size_;       ///< Buffer 大小
    size_t num_buffers_;       ///< 总数量（0 表示无限）
    size_t produced_count_;    ///< 已生成数量
};

}  // namespace blocks
}  // namespace multiqueue

