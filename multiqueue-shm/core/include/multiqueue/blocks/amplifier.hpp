/**
 * @file amplifier.hpp
 * @brief Amplifier Block - 信号放大器
 */

#pragma once

#include "../block.hpp"
#include <cstring>

namespace multiqueue {
namespace blocks {

/**
 * @brief Amplifier Block
 * 
 * 将输入数据乘以增益系数，用于演示 Processing Block
 */
class Amplifier : public ProcessingBlock {
public:
    /**
     * @brief 构造函数
     * 
     * @param allocator Buffer 分配器
     * @param gain 增益系数
     */
    Amplifier(SharedBufferAllocator* allocator, float gain = 2.0f)
        : ProcessingBlock(BlockConfig("Amplifier", BlockType::PROCESSING), allocator)
        , gain_(gain)
        , processed_count_(0)
    {
        // 添加输入和输出端口
        add_input_port(PortConfig("in", PortType::INPUT));
        add_output_port(PortConfig("out", PortType::OUTPUT));
    }
    
    bool initialize() override {
        processed_count_ = 0;
        return true;
    }
    
    WorkResult work() override {
        // 消费输入
        BufferPtr input_buffer = consume_input(0, DEFAULT_TIMEOUT_MS);
        
        if (!input_buffer.valid()) {
            return WorkResult::INSUFFICIENT_INPUT;
        }
        
        // 分配输出 Buffer
        size_t input_size = input_buffer.size();
        BufferPtr output_buffer = allocate_output_buffer(input_size);
        
        if (!output_buffer.valid()) {
            return WorkResult::ERROR;
        }
        
        // 处理数据（假设是 float 数组）
        const float* input_data = input_buffer.as<const float>();
        float* output_data = output_buffer.as<float>();
        size_t num_samples = input_size / sizeof(float);
        
        for (size_t i = 0; i < num_samples; ++i) {
            output_data[i] = input_data[i] * gain_;
        }
        
        // 传递时间戳
        output_buffer.set_timestamp(input_buffer.timestamp());
        
        // 输出
        if (!produce_output(0, output_buffer, DEFAULT_TIMEOUT_MS)) {
            return WorkResult::INSUFFICIENT_OUTPUT;
        }
        
        ++processed_count_;
        return WorkResult::OK;
    }
    
    /**
     * @brief 设置增益
     */
    void set_gain(float gain) { gain_ = gain; }
    
    /**
     * @brief 获取增益
     */
    float gain() const { return gain_; }
    
    /**
     * @brief 获取已处理的 Buffer 数量
     */
    size_t processed_count() const { return processed_count_; }
    
private:
    float gain_;               ///< 增益系数
    size_t processed_count_;   ///< 已处理数量
};

}  // namespace blocks
}  // namespace multiqueue

