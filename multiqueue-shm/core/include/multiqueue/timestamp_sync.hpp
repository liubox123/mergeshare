#pragma once

#include "ring_queue.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <queue>
#include <vector>

namespace multiqueue {

/**
 * @brief 同步统计信息
 */
struct SyncStats {
    uint64_t total_synced = 0;          ///< 总同步数量
    uint64_t timeout_count = 0;         ///< 超时次数
    uint64_t timestamp_rewind_count = 0; ///< 时间戳回退次数
    
    /**
     * @brief 默认构造函数
     */
    SyncStats() = default;
};

/**
 * @brief 多队列合并视图
 * 
 * 将多个队列按时间戳合并为一个逻辑队列
 * 使用多路归并排序算法，确保输出按时间戳顺序排列
 * 
 * @tparam T 元素类型
 */
template<typename T>
class MergedQueueView {
public:
    /**
     * @brief 构造函数
     * 
     * @param queues 队列指针列表
     * @param sync_timeout_ms 同步超时时间（毫秒）
     */
    MergedQueueView(
        const std::vector<std::shared_ptr<RingQueue<T>>>& queues,
        uint32_t sync_timeout_ms)
        : queues_(queues)
        , sync_timeout_ms_(sync_timeout_ms)
        , stats_()
    {
        // 初始化每个队列的缓冲区
        buffers_.resize(queues_.size());
        has_data_.resize(queues_.size(), false);
        
        // 预读取每个队列的第一个元素
        for (size_t i = 0; i < queues_.size(); ++i) {
            try_fetch_next(i);
        }
    }
    
    /**
     * @brief 获取下一个元素（按时间戳排序）
     * 
     * @param data 输出参数，存储元素数据
     * @return true 如果成功获取，false 如果所有队列都空或超时
     */
    bool next(T& data) {
        return next(data, nullptr);
    }
    
    /**
     * @brief 获取下一个元素（带时间戳）
     * 
     * @param data 输出参数
     * @param timestamp 输出参数，存储时间戳
     * @return true 如果成功获取
     */
    bool next(T& data, uint64_t* timestamp) {
        // 找到具有最小时间戳的队列
        int min_queue = -1;
        uint64_t min_timestamp = UINT64_MAX;
        
        for (size_t i = 0; i < queues_.size(); ++i) {
            if (has_data_[i]) {
                if (buffers_[i].timestamp < min_timestamp) {
                    min_timestamp = buffers_[i].timestamp;
                    min_queue = static_cast<int>(i);
                }
            }
        }
        
        // 如果没有找到有效数据，尝试从所有队列读取
        if (min_queue < 0) {
            // 尝试等待新数据
            auto start = std::chrono::steady_clock::now();
            auto timeout = std::chrono::milliseconds(sync_timeout_ms_);
            
            while (min_queue < 0) {
                // 尝试从所有队列读取
                bool any_progress = false;
                for (size_t i = 0; i < queues_.size(); ++i) {
                    if (try_fetch_next(i)) {
                        any_progress = true;
                        if (buffers_[i].timestamp < min_timestamp) {
                            min_timestamp = buffers_[i].timestamp;
                            min_queue = static_cast<int>(i);
                        }
                    }
                }
                
                // 如果找到数据，退出循环
                if (min_queue >= 0) {
                    break;
                }
                
                // 如果没有任何进展，检查超时
                if (!any_progress) {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed >= timeout) {
                        stats_.timeout_count++;
                        return false;
                    }
                    
                    // 短暂等待
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        
        // 从选定的队列中取出数据
        if (min_queue >= 0) {
            data = buffers_[min_queue].data;
            if (timestamp) {
                *timestamp = buffers_[min_queue].timestamp;
            }
            
            // 标记该队列需要重新读取
            has_data_[min_queue] = false;
            
            // 尝试读取下一个元素
            try_fetch_next(min_queue);
            
            stats_.total_synced++;
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief 检查是否还有数据
     * @return true 如果至少有一个队列还有数据
     */
    bool has_more() const {
        for (size_t i = 0; i < queues_.size(); ++i) {
            if (has_data_[i] || !queues_[i]->empty()) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief 获取同步统计信息
     * @return 同步统计信息
     */
    const SyncStats& get_sync_stats() const {
        return stats_;
    }
    
    /**
     * @brief 重置统计信息
     */
    void reset_stats() {
        stats_ = SyncStats();
    }
    
private:
    /**
     * @brief 缓冲区条目
     */
    struct BufferEntry {
        T data;
        uint64_t timestamp;
        
        BufferEntry() : data(), timestamp(0) {}
    };
    
    /**
     * @brief 尝试从指定队列读取下一个元素
     * @param queue_index 队列索引
     * @return true 如果成功读取
     */
    bool try_fetch_next(size_t queue_index) {
        if (queue_index >= queues_.size()) {
            return false;
        }
        
        T data;
        uint64_t timestamp;
        
        if (queues_[queue_index]->try_pop(data, &timestamp)) {
            buffers_[queue_index].data = data;
            buffers_[queue_index].timestamp = timestamp;
            has_data_[queue_index] = true;
            
            // 检测时间戳回退
            if (queue_index < last_timestamps_.size()) {
                if (timestamp < last_timestamps_[queue_index]) {
                    stats_.timestamp_rewind_count++;
                }
            }
            
            // 更新最后时间戳
            if (queue_index >= last_timestamps_.size()) {
                last_timestamps_.resize(queue_index + 1, 0);
            }
            last_timestamps_[queue_index] = timestamp;
            
            return true;
        }
        
        return false;
    }
    
private:
    /// 队列指针列表
    std::vector<std::shared_ptr<RingQueue<T>>> queues_;
    
    /// 同步超时时间（毫秒）
    uint32_t sync_timeout_ms_;
    
    /// 每个队列的缓冲区
    std::vector<BufferEntry> buffers_;
    
    /// 每个队列是否有数据
    std::vector<bool> has_data_;
    
    /// 每个队列的最后时间戳（用于检测时间戳回退）
    std::vector<uint64_t> last_timestamps_;
    
    /// 同步统计信息
    SyncStats stats_;
};

/**
 * @brief 时间戳同步器
 * 
 * 提供时间戳相关的工具函数
 */
class TimestampSynchronizer {
public:
    /**
     * @brief 获取当前时间戳（纳秒）
     * @return 当前时间戳
     */
    static uint64_t now() {
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        );
        return static_cast<uint64_t>(nanos.count());
    }
    
    /**
     * @brief 获取当前时间戳（微秒）
     * @return 当前时间戳
     */
    static uint64_t now_micros() {
        auto now = std::chrono::high_resolution_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        );
        return static_cast<uint64_t>(micros.count());
    }
    
    /**
     * @brief 获取当前时间戳（毫秒）
     * @return 当前时间戳
     */
    static uint64_t now_millis() {
        auto now = std::chrono::high_resolution_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        );
        return static_cast<uint64_t>(millis.count());
    }
    
    /**
     * @brief 将纳秒转换为微秒
     * @param nanos 纳秒
     * @return 微秒
     */
    static uint64_t nanos_to_micros(uint64_t nanos) {
        return nanos / 1000;
    }
    
    /**
     * @brief 将纳秒转换为毫秒
     * @param nanos 纳秒
     * @return 毫秒
     */
    static uint64_t nanos_to_millis(uint64_t nanos) {
        return nanos / 1000000;
    }
    
    /**
     * @brief 检查时间戳是否在合理范围内
     * @param timestamp 时间戳（纳秒）
     * @param tolerance_ms 容差（毫秒）
     * @return true 如果时间戳合理
     */
    static bool is_timestamp_valid(uint64_t timestamp, uint64_t tolerance_ms = 10000) {
        uint64_t current = now();
        uint64_t tolerance_nanos = tolerance_ms * 1000000;
        
        // 检查时间戳是否在当前时间的容差范围内
        if (timestamp > current + tolerance_nanos) {
            return false;  // 时间戳在未来
        }
        
        // 允许历史时间戳（不检查下限）
        return true;
    }
};

} // namespace multiqueue


