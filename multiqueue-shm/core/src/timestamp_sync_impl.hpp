/**
 * @file timestamp_sync_impl.hpp
 * @brief TimestampSynchronizer 实现
 * 
 * 多队列时间戳同步器，用于按时间戳顺序合并多个队列的数据
 */

#pragma once

#include <multiqueue/timestamp_sync.hpp>
#include <multiqueue/ring_queue.hpp>
#include <multiqueue/mp_logger.hpp>
#include <algorithm>
#include <chrono>

namespace multiqueue {

/**
 * @brief 时间戳同步器实现
 */
template<typename T>
class TimestampSynchronizer<T>::Impl {
public:
    /**
     * @brief 构造函数
     */
    Impl(uint32_t max_time_diff_ms)
        : max_time_diff_ms_(max_time_diff_ms)
        , is_closed_(false)
    {
    }
    
    /**
     * @brief 添加队列
     */
    void add_queue(std::shared_ptr<RingQueue<T>> queue, const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        QueueEntry entry;
        entry.queue = queue;
        entry.name = name;
        entry.has_pending = false;
        entry.pending_timestamp = 0;
        
        queues_.push_back(entry);
        
        LOG_INFO_FMT("Added queue to synchronizer: " << name);
    }
    
    /**
     * @brief 移除队列
     */
    void remove_queue(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::remove_if(queues_.begin(), queues_.end(),
            [&name](const QueueEntry& entry) {
                return entry.name == name;
            });
        
        if (it != queues_.end()) {
            queues_.erase(it, queues_.end());
            LOG_INFO_FMT("Removed queue from synchronizer: " << name);
        }
    }
    
    /**
     * @brief 获取下一个按时间戳排序的数据
     */
    bool get_next(T& data, uint64_t* timestamp, std::string* queue_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (is_closed_ && all_queues_empty()) {
            return false;
        }
        
        // 确保所有队列都有待处理的数据
        if (!fetch_pending_from_all_queues()) {
            return false;
        }
        
        // 找到时间戳最小的队列
        auto min_it = std::min_element(queues_.begin(), queues_.end(),
            [](const QueueEntry& a, const QueueEntry& b) {
                if (!a.has_pending) return false;
                if (!b.has_pending) return true;
                return a.pending_timestamp < b.pending_timestamp;
            });
        
        if (min_it == queues_.end() || !min_it->has_pending) {
            return false;
        }
        
        // 检查时间戳差距
        uint64_t min_ts = min_it->pending_timestamp;
        uint64_t max_ts = get_max_pending_timestamp();
        
        if (max_ts - min_ts > max_time_diff_ms_ * 1000000ULL) {
            // 时间戳差距过大，等待
            LOG_WARN_FMT("Timestamp difference too large: " << (max_ts - min_ts) / 1000000ULL << " ms");
            return false;
        }
        
        // 返回数据
        data = min_it->pending_data;
        if (timestamp) {
            *timestamp = min_it->pending_timestamp;
        }
        if (queue_name) {
            *queue_name = min_it->name;
        }
        
        // 标记为已消费
        min_it->has_pending = false;
        
        return true;
    }
    
    /**
     * @brief 带超时的获取
     */
    bool get_next_with_timeout(T& data, uint32_t timeout_ms, 
                               uint64_t* timestamp, std::string* queue_name) {
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeout_ms);
        
        while (true) {
            if (get_next(data, timestamp, queue_name)) {
                return true;
            }
            
            if (is_closed_ && all_queues_empty()) {
                return false;
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= timeout) {
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    /**
     * @brief 获取队列数量
     */
    size_t queue_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queues_.size();
    }
    
    /**
     * @brief 关闭同步器
     */
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        is_closed_ = true;
    }
    
    /**
     * @brief 检查是否已关闭
     */
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_closed_;
    }
    
private:
    /**
     * @brief 队列条目
     */
    struct QueueEntry {
        std::shared_ptr<RingQueue<T>> queue;
        std::string name;
        bool has_pending;
        uint64_t pending_timestamp;
        T pending_data;
    };
    
    /**
     * @brief 从所有队列获取待处理数据
     */
    bool fetch_pending_from_all_queues() {
        bool all_have_data = true;
        
        for (auto& entry : queues_) {
            if (!entry.has_pending) {
                // 尝试从队列获取数据
                if (!entry.queue->pop(entry.pending_data, &entry.pending_timestamp)) {
                    all_have_data = false;
                } else {
                    entry.has_pending = true;
                }
            }
        }
        
        // 至少有一个队列有数据就返回 true
        return std::any_of(queues_.begin(), queues_.end(),
            [](const QueueEntry& entry) { return entry.has_pending; });
    }
    
    /**
     * @brief 获取最大待处理时间戳
     */
    uint64_t get_max_pending_timestamp() const {
        uint64_t max_ts = 0;
        
        for (const auto& entry : queues_) {
            if (entry.has_pending && entry.pending_timestamp > max_ts) {
                max_ts = entry.pending_timestamp;
            }
        }
        
        return max_ts;
    }
    
    /**
     * @brief 检查所有队列是否为空
     */
    bool all_queues_empty() const {
        for (const auto& entry : queues_) {
            if (entry.has_pending || !entry.queue->empty()) {
                return false;
            }
        }
        return true;
    }
    
private:
    std::vector<QueueEntry> queues_;
    uint32_t max_time_diff_ms_;
    bool is_closed_;
    mutable std::mutex mutex_;
};

// TimestampSynchronizer 实现

template<typename T>
TimestampSynchronizer<T>::TimestampSynchronizer(uint32_t max_time_diff_ms)
    : impl_(std::make_unique<Impl>(max_time_diff_ms))
{
}

template<typename T>
TimestampSynchronizer<T>::~TimestampSynchronizer() = default;

template<typename T>
void TimestampSynchronizer<T>::add_queue(std::shared_ptr<RingQueue<T>> queue, const std::string& name) {
    impl_->add_queue(queue, name);
}

template<typename T>
void TimestampSynchronizer<T>::remove_queue(const std::string& name) {
    impl_->remove_queue(name);
}

template<typename T>
bool TimestampSynchronizer<T>::get_next(T& data, uint64_t* timestamp, std::string* queue_name) {
    return impl_->get_next(data, timestamp, queue_name);
}

template<typename T>
bool TimestampSynchronizer<T>::get_next_with_timeout(T& data, uint32_t timeout_ms,
                                                     uint64_t* timestamp, std::string* queue_name) {
    return impl_->get_next_with_timeout(data, timeout_ms, timestamp, queue_name);
}

template<typename T>
size_t TimestampSynchronizer<T>::queue_count() const {
    return impl_->queue_count();
}

template<typename T>
void TimestampSynchronizer<T>::close() {
    impl_->close();
}

template<typename T>
bool TimestampSynchronizer<T>::is_closed() const {
    return impl_->is_closed();
}

} // namespace multiqueue

