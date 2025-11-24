/**
 * @file null_sink.hpp
 * @brief Null Sink Block - 丢弃数据用于测试
 */

#pragma once

#include "../block.hpp"

namespace multiqueue {
namespace blocks {

/**
 * @brief Null Sink Block
 * 
 * 消费输入数据但不做任何处理，用于测试和基准测试
 */
class NullSink : public SinkBlock {
public:
    /**
     * @brief 构造函数
     * 
     * @param allocator Buffer 分配器
     */
    explicit NullSink(SharedBufferAllocator* allocator)
        : SinkBlock(BlockConfig("NullSink", BlockType::SINK), allocator)
        , consumed_count_(0)
    {
        // 添加输入端口
        add_input_port(PortConfig("in", PortType::INPUT));
    }
    
    bool initialize() override {
        consumed_count_ = 0;
        return true;
    }
    
    WorkResult work() override {
        // 消费输入
        BufferPtr buffer = consume_input(0, DEFAULT_TIMEOUT_MS);
        
        if (!buffer.valid()) {
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        // 不做任何处理，只增加计数
        ++consumed_count_;
        
        // BufferPtr 析构时自动减少引用计数
        return WorkResult::OK;
    }
    
    /**
     * @brief 获取已消费的 Buffer 数量
     */
    size_t consumed_count() const { return consumed_count_; }
    
private:
    size_t consumed_count_;    ///< 已消费数量
};

}  // namespace blocks
}  // namespace multiqueue

