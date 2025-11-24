# 多流同步设计

> **核心需求**：
> - ✅ 支持时间戳同步模式
> - ✅ 支持异步（自由流）模式
> - ✅ 灵活的流组合策略
> - ✅ 处理流速不匹配

---

## 目录

1. [时间戳设计](#1-时间戳设计)
2. [同步策略](#2-同步策略)
3. [Buffer 节点设计](#3-buffer-节点设计)
4. [同步 Block 设计](#4-同步-block-设计)
5. [多流合并示例](#5-多流合并示例)
6. [时间戳对齐算法](#6-时间戳对齐算法)

---

## 1. 时间戳设计

### 1.1 时间戳类型

```cpp
/**
 * @brief 时间戳类型
 */
enum class TimestampType : uint8_t {
    NONE = 0,           // 无时间戳（异步模式）
    ABSOLUTE,           // 绝对时间戳（Unix 纳秒）
    RELATIVE,           // 相对时间戳（相对于起始时间）
    SAMPLE_INDEX        // 采样索引（适用于固定采样率信号）
};

/**
 * @brief 时间戳信息
 */
struct Timestamp {
    TimestampType type;
    uint64_t value;             // 时间戳值
    
    // 采样率信息（用于 SAMPLE_INDEX 类型）
    double sample_rate;         // 采样率（Hz）
    uint64_t sample_index;      // 采样索引
    
    /**
     * @brief 转换为纳秒
     */
    uint64_t to_nanoseconds() const {
        switch (type) {
            case TimestampType::ABSOLUTE:
            case TimestampType::RELATIVE:
                return value;
            
            case TimestampType::SAMPLE_INDEX:
                // 采样索引 → 纳秒
                return static_cast<uint64_t>(
                    (sample_index * 1e9) / sample_rate
                );
            
            case TimestampType::NONE:
            default:
                return 0;
        }
    }
    
    /**
     * @brief 从纳秒设置
     */
    void from_nanoseconds(uint64_t ns, TimestampType ts_type) {
        type = ts_type;
        value = ns;
    }
    
    /**
     * @brief 比较运算符
     */
    bool operator<(const Timestamp& other) const {
        return to_nanoseconds() < other.to_nanoseconds();
    }
    
    bool operator==(const Timestamp& other) const {
        return to_nanoseconds() == other.to_nanoseconds();
    }
};
```

### 1.2 Buffer 时间戳扩展

```cpp
/**
 * @brief Buffer 元数据（更新版，支持时间戳）
 */
struct alignas(64) BufferMetadata {
    uint64_t buffer_id;
    
    // 内存信息
    uint32_t pool_id;
    uint32_t offset_in_pool;
    size_t size;
    
    // 引用计数
    std::atomic<uint32_t> ref_count;
    
    // ===== 时间戳信息 =====
    Timestamp timestamp;            // 数据的时间戳
    
    // 时间范围（用于表示一段数据）
    Timestamp start_time;           // 起始时间
    Timestamp end_time;             // 结束时间
    bool has_time_range;            // 是否有时间范围
    
    // 数据指针
    uint64_t data_offset;
    
    // 状态
    std::atomic<bool> valid;
    
    char padding[/* ... */];
};
```

---

## 2. 同步策略

### 2.1 Block 工作模式

```cpp
/**
 * @brief Block 工作模式
 */
enum class BlockWorkMode {
    /**
     * @brief 异步模式（自由流）
     * - 不关心时间戳
     * - 按数据到达顺序处理
     * - 高吞吐量
     */
    ASYNC,
    
    /**
     * @brief 同步模式（时间戳对齐）
     * - 等待所有输入流的时间戳对齐
     * - 确保输出数据的时间一致性
     * - 可能有延迟
     */
    SYNC,
    
    /**
     * @brief 混合模式
     * - 部分输入需要同步，部分异步
     */
    HYBRID
};

/**
 * @brief 同步策略
 */
enum class SyncPolicy {
    /**
     * @brief 精确匹配
     * - 等待所有输入流的时间戳完全一致
     * - 适用于采样率相同的信号
     */
    EXACT_MATCH,
    
    /**
     * @brief 最近邻
     * - 选择时间戳最接近的数据
     * - 适用于采样率不同的信号
     */
    NEAREST_NEIGHBOR,
    
    /**
     * @brief 线性插值
     * - 对时间戳不对齐的数据进行插值
     * - 适用于连续信号
     */
    LINEAR_INTERPOLATION,
    
    /**
     * @brief 窗口聚合
     * - 在时间窗口内聚合多个数据
     * - 适用于统计类操作
     */
    WINDOW_AGGREGATION
};
```

### 2.2 端口同步配置

```cpp
/**
 * @brief 端口配置（扩展）
 */
struct PortConfig {
    // 基础配置
    size_t buffer_size = 0;
    size_t queue_capacity = 256;
    bool optional = false;
    
    // ===== 同步配置 =====
    
    /**
     * @brief 是否需要时间戳
     */
    bool require_timestamp = false;
    
    /**
     * @brief 时间戳类型
     */
    TimestampType timestamp_type = TimestampType::ABSOLUTE;
    
    /**
     * @brief 同步策略
     */
    SyncPolicy sync_policy = SyncPolicy::EXACT_MATCH;
    
    /**
     * @brief 同步容差（纳秒）
     * - 对于 EXACT_MATCH：时间戳差异在此范围内认为匹配
     * - 对于 NEAREST_NEIGHBOR：最大允许的时间差
     */
    uint64_t sync_tolerance_ns = 1000000;  // 1ms
    
    /**
     * @brief 超时时间（纳秒）
     * - 等待同步的最大时间
     * - 超时后根据策略决定如何处理
     */
    uint64_t sync_timeout_ns = 100000000;  // 100ms
    
    /**
     * @brief 缓冲深度
     * - 为了同步，每个输入端口可以缓存多少个 Buffer
     */
    size_t sync_buffer_depth = 10;
};
```

---

## 3. Buffer 节点设计

### 3.1 时间戳携带

```cpp
/**
 * @brief 创建带时间戳的 Buffer
 */
BufferPtr create_timestamped_buffer(
    size_t size,
    const Timestamp& timestamp
) {
    // 分配 Buffer
    BufferPtr buffer = allocate_buffer(size);
    
    // 设置时间戳
    BufferMetadata* meta = get_buffer_metadata(buffer->id());
    meta->timestamp = timestamp;
    meta->has_time_range = false;
    
    return buffer;
}

/**
 * @brief 创建带时间范围的 Buffer
 */
BufferPtr create_buffer_with_time_range(
    size_t size,
    const Timestamp& start_time,
    const Timestamp& end_time
) {
    BufferPtr buffer = allocate_buffer(size);
    
    BufferMetadata* meta = get_buffer_metadata(buffer->id());
    meta->start_time = start_time;
    meta->end_time = end_time;
    meta->has_time_range = true;
    
    // 中间时间戳
    meta->timestamp.from_nanoseconds(
        (start_time.to_nanoseconds() + end_time.to_nanoseconds()) / 2,
        start_time.type
    );
    
    return buffer;
}
```

### 3.2 时间戳传播

```cpp
/**
 * @brief 时间戳传播策略
 */
enum class TimestampPropagation {
    /**
     * @brief 保持输入时间戳
     * - 输出的时间戳 = 输入的时间戳
     */
    PRESERVE,
    
    /**
     * @brief 添加延迟
     * - 输出的时间戳 = 输入的时间戳 + 处理延迟
     */
    ADD_DELAY,
    
    /**
     * @brief 重新生成
     * - 输出使用新的时间戳（当前时间）
     */
    REGENERATE,
    
    /**
     * @brief 合并多个输入
     * - 输出的时间戳 = 多个输入时间戳的某种组合
     */
    MERGE
};
```

---

## 4. 同步 Block 设计

### 4.1 SyncBlock 基类

```cpp
/**
 * @brief 同步 Block 基类
 * 
 * 用于需要多流时间同步的 Block
 */
class SyncBlock : public Block {
public:
    SyncBlock(const std::string& name, BlockWorkMode mode = BlockWorkMode::SYNC)
        : Block(name, BlockType::PROCESSING)
        , work_mode_(mode)
    {}
    
    // ===== 配置 =====
    
    void set_sync_policy(SyncPolicy policy) {
        sync_policy_ = policy;
    }
    
    void set_sync_tolerance(uint64_t tolerance_ns) {
        sync_tolerance_ns_ = tolerance_ns;
    }
    
    // ===== 工作函数 =====
    
    WorkResult work() override {
        if (work_mode_ == BlockWorkMode::ASYNC) {
            return work_async();
        } else {
            return work_sync();
        }
    }
    
protected:
    /**
     * @brief 异步工作（子类可选实现）
     */
    virtual WorkResult work_async() {
        // 默认按到达顺序处理第一个可用输入
        for (const auto& port_name : input_port_names_) {
            if (has_input(port_name)) {
                auto input = get_input(port_name, 10);
                if (input) {
                    return process_async({input});
                }
            }
        }
        return WorkResult::NEED_MORE_INPUT;
    }
    
    /**
     * @brief 同步工作
     */
    virtual WorkResult work_sync() {
        // 1. 从所有输入端口读取数据到缓冲区
        std::map<std::string, std::deque<BufferPtr>> input_buffers;
        
        for (const auto& port_name : input_port_names_) {
            // 尝试读取数据
            auto buffer = get_input(port_name, 0);  // 非阻塞
            if (buffer) {
                input_buffers[port_name].push_back(buffer);
            }
        }
        
        // 2. 检查是否所有必需的输入都有数据
        for (const auto& port_name : input_port_names_) {
            if (!is_port_optional(port_name) && input_buffers[port_name].empty()) {
                // 必需的输入端口无数据，等待
                return WorkResult::NEED_MORE_INPUT;
            }
        }
        
        // 3. 根据同步策略对齐数据
        std::vector<BufferPtr> aligned_buffers;
        bool success = align_buffers(input_buffers, aligned_buffers);
        
        if (!success) {
            // 对齐失败，可能需要更多数据
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 4. 处理对齐后的数据
        return process_sync(aligned_buffers);
    }
    
    /**
     * @brief 对齐多个输入流的 Buffer（根据时间戳）
     */
    bool align_buffers(
        const std::map<std::string, std::deque<BufferPtr>>& input_buffers,
        std::vector<BufferPtr>& aligned_buffers
    ) {
        switch (sync_policy_) {
            case SyncPolicy::EXACT_MATCH:
                return align_exact_match(input_buffers, aligned_buffers);
            
            case SyncPolicy::NEAREST_NEIGHBOR:
                return align_nearest_neighbor(input_buffers, aligned_buffers);
            
            case SyncPolicy::LINEAR_INTERPOLATION:
                return align_with_interpolation(input_buffers, aligned_buffers);
            
            default:
                return false;
        }
    }
    
    /**
     * @brief 精确匹配对齐
     */
    bool align_exact_match(
        const std::map<std::string, std::deque<BufferPtr>>& input_buffers,
        std::vector<BufferPtr>& aligned_buffers
    ) {
        // 找到所有流中最小的最大时间戳
        uint64_t target_timestamp = UINT64_MAX;
        
        for (const auto& [port_name, buffers] : input_buffers) {
            if (buffers.empty()) continue;
            
            uint64_t max_ts = get_timestamp(buffers.back());
            target_timestamp = std::min(target_timestamp, max_ts);
        }
        
        // 对每个输入流，找到时间戳最接近 target_timestamp 的 Buffer
        for (const auto& [port_name, buffers] : input_buffers) {
            BufferPtr best_match = nullptr;
            uint64_t best_diff = UINT64_MAX;
            
            for (const auto& buffer : buffers) {
                uint64_t ts = get_timestamp(buffer);
                uint64_t diff = (ts > target_timestamp) ? 
                               (ts - target_timestamp) : 
                               (target_timestamp - ts);
                
                if (diff < best_diff) {
                    best_diff = diff;
                    best_match = buffer;
                }
            }
            
            // 检查是否在容差范围内
            if (best_diff > sync_tolerance_ns_) {
                return false;  // 对齐失败
            }
            
            aligned_buffers.push_back(best_match);
        }
        
        return true;
    }
    
    /**
     * @brief 最近邻对齐
     */
    bool align_nearest_neighbor(
        const std::map<std::string, std::deque<BufferPtr>>& input_buffers,
        std::vector<BufferPtr>& aligned_buffers
    ) {
        // 选择第一个输入流的第一个 Buffer 的时间戳作为参考
        auto first_port = input_buffers.begin();
        if (first_port->second.empty()) {
            return false;
        }
        
        uint64_t reference_ts = get_timestamp(first_port->second.front());
        
        // 对每个输入流，找到时间戳最接近参考时间戳的 Buffer
        for (const auto& [port_name, buffers] : input_buffers) {
            if (buffers.empty()) {
                return false;
            }
            
            BufferPtr nearest = buffers.front();
            uint64_t min_diff = UINT64_MAX;
            
            for (const auto& buffer : buffers) {
                uint64_t ts = get_timestamp(buffer);
                uint64_t diff = (ts > reference_ts) ? 
                               (ts - reference_ts) : 
                               (reference_ts - ts);
                
                if (diff < min_diff) {
                    min_diff = diff;
                    nearest = buffer;
                }
            }
            
            aligned_buffers.push_back(nearest);
        }
        
        return true;
    }
    
    /**
     * @brief 线性插值对齐
     */
    bool align_with_interpolation(
        const std::map<std::string, std::deque<BufferPtr>>& input_buffers,
        std::vector<BufferPtr>& aligned_buffers
    ) {
        // TODO: 实现插值逻辑
        // 对于每个输入流，如果时间戳不对齐，使用相邻的两个 Buffer 进行插值
        return false;
    }
    
    /**
     * @brief 获取 Buffer 的时间戳
     */
    uint64_t get_timestamp(const BufferPtr& buffer) const {
        BufferMetadata* meta = get_buffer_metadata(buffer->id());
        return meta->timestamp.to_nanoseconds();
    }
    
    /**
     * @brief 处理异步数据（子类实现）
     */
    virtual WorkResult process_async(const std::vector<BufferPtr>& inputs) = 0;
    
    /**
     * @brief 处理同步数据（子类实现）
     */
    virtual WorkResult process_sync(const std::vector<BufferPtr>& inputs) = 0;
    
private:
    BlockWorkMode work_mode_;
    SyncPolicy sync_policy_ = SyncPolicy::EXACT_MATCH;
    uint64_t sync_tolerance_ns_ = 1000000;  // 1ms
    
    std::vector<std::string> input_port_names_;
};
```

---

## 5. 多流合并示例

### 5.1 同步合并器

```cpp
/**
 * @brief 同步合并器（时间戳对齐）
 * 
 * 合并多个输入流，确保输出的数据来自相同时刻
 */
class SyncMergerBlock : public SyncBlock {
public:
    SyncMergerBlock(size_t input_count)
        : SyncBlock("SyncMerger", BlockWorkMode::SYNC)
    {
        // 添加多个输入端口
        for (size_t i = 0; i < input_count; ++i) {
            PortConfig config;
            config.require_timestamp = true;  // 要求时间戳
            config.sync_policy = SyncPolicy::EXACT_MATCH;
            config.sync_tolerance_ns = 1000000;  // 1ms
            
            add_input_port("in" + std::to_string(i), config);
        }
        
        // 添加输出端口
        add_output_port("out");
        
        set_sync_policy(SyncPolicy::EXACT_MATCH);
    }
    
protected:
    WorkResult process_async(const std::vector<BufferPtr>& inputs) override {
        // 异步模式：直接合并
        return merge_and_output(inputs);
    }
    
    WorkResult process_sync(const std::vector<BufferPtr>& inputs) override {
        // 同步模式：inputs 已经由基类对齐
        
        // 验证时间戳一致性
        if (inputs.size() < 2) {
            return WorkResult::OK;
        }
        
        uint64_t ref_ts = get_timestamp(inputs[0]);
        for (size_t i = 1; i < inputs.size(); ++i) {
            uint64_t ts = get_timestamp(inputs[i]);
            if (std::abs((int64_t)(ts - ref_ts)) > 1000000) {  // 1ms
                LOG_WARN("Timestamp mismatch: {} vs {}", ref_ts, ts);
            }
        }
        
        // 合并数据
        return merge_and_output(inputs);
    }
    
private:
    WorkResult merge_and_output(const std::vector<BufferPtr>& inputs) {
        // 计算输出大小
        size_t total_size = 0;
        for (const auto& input : inputs) {
            total_size += input->size();
        }
        
        // 分配输出 Buffer
        auto output = allocate_buffer(total_size);
        
        // 复制数据
        size_t offset = 0;
        for (const auto& input : inputs) {
            std::memcpy(
                static_cast<char*>(output->data()) + offset,
                input->data(),
                input->size()
            );
            offset += input->size();
        }
        
        // 设置输出时间戳（使用第一个输入的时间戳）
        if (!inputs.empty()) {
            BufferMetadata* out_meta = get_buffer_metadata(output->id());
            BufferMetadata* in_meta = get_buffer_metadata(inputs[0]->id());
            out_meta->timestamp = in_meta->timestamp;
        }
        
        // 发布输出
        produce_output("out", output);
        
        return WorkResult::OK;
    }
};
```

### 5.2 异步合并器

```cpp
/**
 * @brief 异步合并器（自由流）
 * 
 * 按数据到达顺序合并，不关心时间戳
 */
class AsyncMergerBlock : public SyncBlock {
public:
    AsyncMergerBlock(size_t input_count)
        : SyncBlock("AsyncMerger", BlockWorkMode::ASYNC)
    {
        for (size_t i = 0; i < input_count; ++i) {
            PortConfig config;
            config.require_timestamp = false;  // 不要求时间戳
            add_input_port("in" + std::to_string(i), config);
        }
        
        add_output_port("out");
    }
    
protected:
    WorkResult process_async(const std::vector<BufferPtr>& inputs) override {
        // 简单转发第一个可用输入
        if (!inputs.empty()) {
            produce_output("out", inputs[0]);
            return WorkResult::OK;
        }
        return WorkResult::NEED_MORE_INPUT;
    }
    
    WorkResult process_sync(const std::vector<BufferPtr>& inputs) override {
        // 不使用同步模式
        return WorkResult::ERROR;
    }
};
```

### 5.3 时间戳对齐的信号处理

```cpp
/**
 * @brief 信号相关器（需要时间同步）
 * 
 * 计算两个信号的相关性，要求信号时间对齐
 */
class SignalCorrelatorBlock : public SyncBlock {
public:
    SignalCorrelatorBlock()
        : SyncBlock("SignalCorrelator", BlockWorkMode::SYNC)
    {
        PortConfig config;
        config.require_timestamp = true;
        config.timestamp_type = TimestampType::SAMPLE_INDEX;
        config.sync_policy = SyncPolicy::EXACT_MATCH;
        config.sync_tolerance_ns = 0;  // 必须精确匹配
        
        add_input_port("signal1", config);
        add_input_port("signal2", config);
        add_output_port("correlation");
        
        set_sync_policy(SyncPolicy::EXACT_MATCH);
    }
    
protected:
    WorkResult process_sync(const std::vector<BufferPtr>& inputs) override {
        if (inputs.size() != 2) {
            return WorkResult::ERROR;
        }
        
        // 两个输入信号的时间戳已经对齐
        const float* signal1 = inputs[0]->as<float>();
        const float* signal2 = inputs[1]->as<float>();
        
        size_t len1 = inputs[0]->size() / sizeof(float);
        size_t len2 = inputs[1]->size() / sizeof(float);
        size_t len = std::min(len1, len2);
        
        // 计算相关性
        float correlation = 0.0f;
        for (size_t i = 0; i < len; ++i) {
            correlation += signal1[i] * signal2[i];
        }
        correlation /= len;
        
        // 输出结果
        auto output = allocate_buffer(sizeof(float));
        *output->as<float>() = correlation;
        
        // 保持输入的时间戳
        BufferMetadata* out_meta = get_buffer_metadata(output->id());
        BufferMetadata* in_meta = get_buffer_metadata(inputs[0]->id());
        out_meta->timestamp = in_meta->timestamp;
        
        produce_output("correlation", output);
        
        return WorkResult::OK;
    }
    
    WorkResult process_async(const std::vector<BufferPtr>& inputs) override {
        // 不支持异步模式
        return WorkResult::ERROR;
    }
};
```

---

## 6. 时间戳对齐算法

### 6.1 采样率转换

```cpp
/**
 * @brief 采样率转换器
 * 
 * 将输入信号重采样到目标采样率
 */
class ResamplerBlock : public Block {
public:
    ResamplerBlock(double input_rate, double output_rate)
        : Block("Resampler", BlockType::PROCESSING)
        , input_rate_(input_rate)
        , output_rate_(output_rate)
        , ratio_(output_rate / input_rate)
    {
        PortConfig config;
        config.require_timestamp = true;
        config.timestamp_type = TimestampType::SAMPLE_INDEX;
        
        add_input_port("in", config);
        add_output_port("out");
    }
    
    WorkResult work() override {
        auto input = get_input("in", 100);
        if (!input) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        const float* in_samples = input->as<float>();
        size_t in_count = input->size() / sizeof(float);
        
        // 计算输出采样数
        size_t out_count = static_cast<size_t>(in_count * ratio_);
        
        // 分配输出
        auto output = allocate_buffer(out_count * sizeof(float));
        float* out_samples = output->as<float>();
        
        // 线性插值重采样
        for (size_t i = 0; i < out_count; ++i) {
            double in_pos = i / ratio_;
            size_t in_idx = static_cast<size_t>(in_pos);
            double frac = in_pos - in_idx;
            
            if (in_idx + 1 < in_count) {
                out_samples[i] = in_samples[in_idx] * (1.0 - frac) +
                                in_samples[in_idx + 1] * frac;
            } else {
                out_samples[i] = in_samples[in_idx];
            }
        }
        
        // 更新时间戳
        BufferMetadata* in_meta = get_buffer_metadata(input->id());
        BufferMetadata* out_meta = get_buffer_metadata(output->id());
        
        out_meta->timestamp = in_meta->timestamp;
        out_meta->timestamp.sample_rate = output_rate_;
        
        produce_output("out", output);
        
        return WorkResult::OK;
    }
    
private:
    double input_rate_;
    double output_rate_;
    double ratio_;
};
```

### 6.2 时间窗口聚合

```cpp
/**
 * @brief 时间窗口聚合器
 * 
 * 在时间窗口内收集多个输入的数据并聚合
 */
class WindowAggregatorBlock : public SyncBlock {
public:
    WindowAggregatorBlock(uint64_t window_size_ns)
        : SyncBlock("WindowAggregator", BlockWorkMode::SYNC)
        , window_size_ns_(window_size_ns)
        , window_start_ts_(0)
    {
        add_output_port("out");
    }
    
    void add_input_stream(const std::string& name) {
        PortConfig config;
        config.require_timestamp = true;
        config.sync_policy = SyncPolicy::WINDOW_AGGREGATION;
        add_input_port(name, config);
    }
    
protected:
    WorkResult process_sync(const std::vector<BufferPtr>& inputs) override {
        if (inputs.empty()) {
            return WorkResult::NEED_MORE_INPUT;
        }
        
        // 获取第一个输入的时间戳作为窗口起始
        uint64_t first_ts = get_timestamp(inputs[0]);
        
        if (window_start_ts_ == 0) {
            window_start_ts_ = first_ts;
        }
        
        // 收集窗口内的所有数据
        for (const auto& input : inputs) {
            uint64_t ts = get_timestamp(input);
            
            if (ts >= window_start_ts_ && ts < window_start_ts_ + window_size_ns_) {
                // 在窗口内
                window_buffers_.push_back(input);
            }
        }
        
        // 检查窗口是否结束
        if (first_ts >= window_start_ts_ + window_size_ns_) {
            // 窗口结束，聚合数据
            aggregate_and_output();
            
            // 开始新窗口
            window_start_ts_ += window_size_ns_;
            window_buffers_.clear();
        }
        
        return WorkResult::OK;
    }
    
    WorkResult process_async(const std::vector<BufferPtr>& inputs) override {
        return WorkResult::ERROR;
    }
    
private:
    void aggregate_and_output() {
        if (window_buffers_.empty()) {
            return;
        }
        
        // 简单聚合：计算平均值
        std::vector<float> sum;
        size_t count = 0;
        
        for (const auto& buffer : window_buffers_) {
            const float* data = buffer->as<float>();
            size_t len = buffer->size() / sizeof(float);
            
            if (sum.empty()) {
                sum.resize(len, 0.0f);
            }
            
            for (size_t i = 0; i < len && i < sum.size(); ++i) {
                sum[i] += data[i];
            }
            count++;
        }
        
        // 计算平均
        for (auto& val : sum) {
            val /= count;
        }
        
        // 输出
        auto output = allocate_buffer(sum.size() * sizeof(float));
        std::memcpy(output->data(), sum.data(), sum.size() * sizeof(float));
        
        // 设置时间戳为窗口中心
        BufferMetadata* meta = get_buffer_metadata(output->id());
        meta->timestamp.from_nanoseconds(
            window_start_ts_ + window_size_ns_ / 2,
            TimestampType::ABSOLUTE
        );
        
        produce_output("out", output);
    }
    
    uint64_t window_size_ns_;
    uint64_t window_start_ts_;
    std::vector<BufferPtr> window_buffers_;
};
```

---

## 7. 配置示例

### 7.1 同步模式配置

```cpp
// 创建同步合并器
auto merger = std::make_unique<SyncMergerBlock>(3);

// 配置输入端口
merger->configure_input_port("in0", {
    .require_timestamp = true,
    .timestamp_type = TimestampType::ABSOLUTE,
    .sync_policy = SyncPolicy::EXACT_MATCH,
    .sync_tolerance_ns = 1000000  // 1ms
});

merger->configure_input_port("in1", {
    .require_timestamp = true,
    .timestamp_type = TimestampType::ABSOLUTE,
    .sync_policy = SyncPolicy::EXACT_MATCH,
    .sync_tolerance_ns = 1000000
});

merger->configure_input_port("in2", {
    .require_timestamp = true,
    .timestamp_type = TimestampType::ABSOLUTE,
    .sync_policy = SyncPolicy::EXACT_MATCH,
    .sync_tolerance_ns = 1000000
});
```

### 7.2 异步模式配置

```cpp
// 创建异步合并器
auto merger = std::make_unique<AsyncMergerBlock>(3);

// 配置输入端口（不要求时间戳）
merger->configure_input_port("in0", {
    .require_timestamp = false
});

merger->configure_input_port("in1", {
    .require_timestamp = false
});

merger->configure_input_port("in2", {
    .require_timestamp = false
});
```

### 7.3 混合模式配置

```cpp
// 创建混合模式处理器
auto processor = std::make_unique<HybridProcessorBlock>();

// 某些输入需要同步
processor->configure_input_port("sync_in", {
    .require_timestamp = true,
    .sync_policy = SyncPolicy::EXACT_MATCH
});

// 某些输入不需要同步（控制信号）
processor->configure_input_port("control_in", {
    .require_timestamp = false,
    .optional = true
});
```

---

## 总结

### 核心特性

✅ **时间戳支持**
- 绝对时间戳、相对时间戳、采样索引
- 时间范围表示

✅ **多种同步策略**
- 精确匹配
- 最近邻
- 线性插值
- 窗口聚合

✅ **灵活的工作模式**
- 异步模式（自由流）
- 同步模式（时间对齐）
- 混合模式

✅ **易于扩展**
- SyncBlock 基类封装同步逻辑
- 子类只需实现 process_sync/process_async

---

**准备好集成到多进程架构中了吗？** 🚀

