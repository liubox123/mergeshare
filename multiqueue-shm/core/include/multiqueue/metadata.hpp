#pragma once

#include "config.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <chrono>
#include <iostream>

// ========== 平台检测和适配 ==========
#ifdef _WIN32
    #define MULTIQUEUE_PLATFORM_WINDOWS
    // Windows 平台将使用 windows_shared_memory
#else
    #define MULTIQUEUE_PLATFORM_POSIX  
    // POSIX 平台使用 shared_memory_object
#endif

namespace multiqueue {

// 魔数：用于验证共享内存段的有效性
// "MQSHMEM\0" 的十六进制表示
constexpr uint64_t QUEUE_MAGIC_NUMBER = 0x4D5153484D454D00ULL;

// 当前版本号
constexpr uint32_t QUEUE_VERSION_MAJOR = 0;
constexpr uint32_t QUEUE_VERSION_MINOR = 1;
constexpr uint32_t QUEUE_VERSION_PATCH = 0;

// 版本号打包为 uint32_t: [major(8bit)][minor(8bit)][patch(16bit)]
constexpr uint32_t QUEUE_VERSION = 
    (QUEUE_VERSION_MAJOR << 24) | (QUEUE_VERSION_MINOR << 16) | QUEUE_VERSION_PATCH;

/**
 * @brief 队列元数据结构
 * 
 * 存储在共享内存段的头部，包含队列的配置信息和元数据
 * 该结构体会被映射到共享内存的开始位置
 * 
 * 内存布局：
 * - 标识信息（魔数、版本号）
 * - 队列配置（容量、元素大小、阻塞模式等）
 * - 队列信息（名称、关联队列、用户元数据）
 * - 时间戳信息
 */
struct alignas(64) QueueMetadata {
    // ========== 标识信息（验证） ==========
    
    /// 魔数，用于验证共享内存段的有效性
    uint64_t magic_number;
    
    /// 版本号：[major(8bit)][minor(8bit)][patch(16bit)]
    uint32_t version;
    
    /// 校验和（可选，用于检测数据损坏）
    uint32_t checksum;
    
    // ========== 队列配置 ==========
    
    /// 单个元素的大小（字节）
    size_t element_size;
    
    /// 队列容量（元素数量）
    size_t capacity;
    
    /// 是否启用时间戳
    bool has_timestamp;
    
    /// 阻塞模式
    BlockingMode blocking_mode;
    
    /// 超时时间（毫秒）
    uint32_t timeout_ms;
    
    /// 是否启用异步线程
    bool enable_async;
    
    // ========== 队列信息 ==========
    
    /// 队列名称（最大 64 字节）
    char queue_name[64];
    
    /// 额外关联的队列名称（最大 256 字节，逗号分隔）
    char extra_queue_names[256];
    
    /// 用户自定义元数据（最大 512 字节）
    char user_metadata[512];
    
    // ========== 时间信息 ==========
    
    /// 队列创建时间戳（Unix 时间戳，秒）
    uint64_t created_at;
    
    /// 队列最后修改时间戳
    uint64_t last_modified_at;
    
    // ========== 填充到缓存行边界 ==========
    
    /// 填充字节，确保结构体大小是 64 字节的倍数
    char padding[64 - ((sizeof(uint64_t) * 4 + sizeof(size_t) * 2 + 
                        sizeof(uint32_t) * 2 + sizeof(bool) * 2 + 
                        sizeof(BlockingMode) + 64 + 256 + 512) % 64)];
    
    /**
     * @brief 初始化元数据
     * @param config 队列配置
     * @param elem_size 元素大小
     */
    void initialize(const QueueConfig& config, size_t elem_size) {
        // 标识信息
        magic_number = QUEUE_MAGIC_NUMBER;
        version = QUEUE_VERSION;
        checksum = 0;  // 暂时不使用校验和
        
        // 队列配置
        element_size = elem_size;
        capacity = config.capacity;
        has_timestamp = config.has_timestamp;
        blocking_mode = config.blocking_mode;
        timeout_ms = config.timeout_ms;
        enable_async = config.enable_async;
        
        // 队列名称
        std::strncpy(queue_name, config.queue_name.c_str(), sizeof(queue_name) - 1);
        queue_name[sizeof(queue_name) - 1] = '\0';
        
        // 额外队列名称
        if (!config.extra_queue_names.empty()) {
            std::string joined;
            for (size_t i = 0; i < config.extra_queue_names.size(); ++i) {
                if (i > 0) joined += ",";
                joined += config.extra_queue_names[i];
            }
            std::strncpy(extra_queue_names, joined.c_str(), sizeof(extra_queue_names) - 1);
            extra_queue_names[sizeof(extra_queue_names) - 1] = '\0';
        } else {
            extra_queue_names[0] = '\0';
        }
        
        // 用户元数据
        if (!config.user_metadata.empty()) {
            std::strncpy(user_metadata, config.user_metadata.c_str(), sizeof(user_metadata) - 1);
            user_metadata[sizeof(user_metadata) - 1] = '\0';
        } else {
            user_metadata[0] = '\0';
        }
        
        // 时间信息
        created_at = static_cast<uint64_t>(std::time(nullptr));
        last_modified_at = created_at;
    }
    
    /**
     * @brief 验证元数据是否有效
     * @return true 如果元数据有效
     */
    bool is_valid() const {
        // 检查魔数
        if (magic_number != QUEUE_MAGIC_NUMBER) {
            return false;
        }
        
        // 检查版本号（主版本号必须匹配）
        uint32_t major = (version >> 24) & 0xFF;
        if (major != QUEUE_VERSION_MAJOR) {
            return false;
        }
        
        // 检查容量和元素大小
        if (capacity == 0 || element_size == 0) {
            return false;
        }
        
        return true;
    }
    
    /**
     * @brief 获取版本号字符串
     * @return 版本号字符串，格式为 "major.minor.patch"
     */
    std::string get_version_string() const {
        uint32_t major = (version >> 24) & 0xFF;
        uint32_t minor = (version >> 16) & 0xFF;
        uint32_t patch = version & 0xFFFF;
        
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
        return std::string(buf);
    }
};

// ========== 多消费者支持 ==========

/// 最大消费者数量（可配置）
constexpr size_t MAX_CONSUMERS = 32;

/**
 * @brief 消费者槽位
 * 
 * 每个消费者占用一个槽位，记录其独立的读取位置
 * 缓存行对齐，避免伪共享
 */
struct alignas(64) ConsumerSlot {
    /// 消费者的读取偏移量（单调递增）
    std::atomic<uint64_t> read_offset;
    
    /// 是否活跃（true = 活跃, false = 空闲）
    std::atomic<bool> active;
    
    /// 消费者标识（最多 31 字节 + '\0'）
    char consumer_id[32];
    
    /// 最后访问时间戳（纳秒，用于超时清理）
    std::atomic<uint64_t> last_access_time;
    
    /// 填充到64字节
    char padding[64 - sizeof(std::atomic<uint64_t>) * 2 - 
                 sizeof(std::atomic<bool>) - 32];
    
    /**
     * @brief 初始化槽位
     */
    void initialize() {
        read_offset.store(0, std::memory_order_relaxed);
        active.store(false, std::memory_order_relaxed);
        consumer_id[0] = '\0';
        last_access_time.store(0, std::memory_order_relaxed);
        // std::cerr << "[ConsumerSlot] initialized" << std::endl;
    }
    
    /**
     * @brief 注册消费者
     * @param id 消费者标识
     * @param start_offset 起始读取位置
     */
    bool register_consumer(const char* id, uint64_t start_offset) {
        bool expected = false;
        if (active.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
            std::strncpy(consumer_id, id, sizeof(consumer_id) - 1);
            consumer_id[sizeof(consumer_id) - 1] = '\0';
            read_offset.store(start_offset, std::memory_order_release);
            // 使用简单的时间戳（避免依赖 TimestampSynchronizer）
            auto now = std::chrono::high_resolution_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
            last_access_time.store(ns.count(), std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    
    /**
     * @brief 注销消费者
     */
    void unregister_consumer() {
        active.store(false, std::memory_order_release);
        consumer_id[0] = '\0';
    }
    
    /**
     * @brief 更新最后访问时间
     */
    void update_access_time() {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        last_access_time.store(ns.count(), std::memory_order_relaxed);
    }
};

/**
 * @brief 消费者注册表
 * 
 * 管理所有消费者的槽位
 */
struct ConsumerRegistry {
    /// 所有消费者槽位
    ConsumerSlot slots[MAX_CONSUMERS];
    
    /// 活跃消费者数量
    std::atomic<uint32_t> active_count;
    
    /// 填充
    char padding[64 - sizeof(std::atomic<uint32_t>)];
    
    /**
     * @brief 初始化注册表
     */
    void initialize() {
        std::cerr << "[ConsumerRegistry] Initializing " << MAX_CONSUMERS << " slots..." << std::endl;
        for (size_t i = 0; i < MAX_CONSUMERS; ++i) {
            slots[i].initialize();
            if (i % 8 == 0) {
                std::cerr << "[ConsumerRegistry] Initialized slot " << i << "/" << MAX_CONSUMERS << std::endl;
            }
        }
        active_count.store(0, std::memory_order_relaxed);
        std::cerr << "[ConsumerRegistry] Initialization complete" << std::endl;
    }
    
    /**
     * @brief 注册新消费者
     * @param consumer_id 消费者标识
     * @param start_offset 起始读取位置
     * @return 槽位索引，-1 表示失败
     */
    int register_consumer(const char* consumer_id, uint64_t start_offset) {
        for (size_t i = 0; i < MAX_CONSUMERS; ++i) {
            if (slots[i].register_consumer(consumer_id, start_offset)) {
                active_count.fetch_add(1, std::memory_order_relaxed);
                return static_cast<int>(i);
            }
        }
        return -1;  // 无可用槽位
    }
    
    /**
     * @brief 注销消费者
     * @param slot_id 槽位索引
     */
    void unregister_consumer(int slot_id) {
        if (slot_id >= 0 && slot_id < static_cast<int>(MAX_CONSUMERS)) {
            if (slots[slot_id].active.load(std::memory_order_acquire)) {
                slots[slot_id].unregister_consumer();
                active_count.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }
    
    /**
     * @brief 获取最慢的消费者偏移量
     * @return 最小的读取偏移量
     */
    uint64_t get_slowest_offset() const {
        uint64_t slowest = UINT64_MAX;
        for (size_t i = 0; i < MAX_CONSUMERS; ++i) {
            if (slots[i].active.load(std::memory_order_acquire)) {
                uint64_t offset = slots[i].read_offset.load(std::memory_order_acquire);
                if (offset < slowest) {
                    slowest = offset;
                }
            }
        }
        return (slowest == UINT64_MAX) ? 0 : slowest;
    }
};

/**
 * @brief 控制块结构
 * 
 * 包含队列的控制变量（原子操作）
 * 为了避免伪共享（false sharing），关键的原子变量按缓存行对齐
 */
struct alignas(64) ControlBlock {
    // ========== 写入控制（生产者） ==========
    
    /// 写入偏移量（单调递增）
    std::atomic<uint64_t> write_offset;
    
    /// 填充，避免与消费者注册表在同一缓存行
    char padding1[64 - sizeof(std::atomic<uint64_t>)];
    
    // ========== 多消费者注册表 ==========
    
    /// 消费者注册表（支持最多 MAX_CONSUMERS 个消费者）
    ConsumerRegistry consumers;
    
    // ========== 生产者/消费者计数 ==========
    
    /// 当前生产者数量
    std::atomic<uint32_t> producer_count;
    
    /// 填充
    char padding3[64 - sizeof(std::atomic<uint32_t>)];
    
    // ========== 统计信息 ==========
    
    /// 总写入次数
    std::atomic<uint64_t> total_pushed;
    
    /// 总读取次数
    std::atomic<uint64_t> total_popped;
    
    /// 覆盖次数（非阻塞模式）
    std::atomic<uint64_t> overwrite_count;
    
    /// 状态标志位（bit 0: 队列是否已关闭, bit 1-31: 保留）
    std::atomic<uint32_t> status_flags;
    
    /// 填充
    char padding4[64 - 3 * sizeof(std::atomic<uint64_t>) - sizeof(std::atomic<uint32_t>)];
    
    // ========== 时间戳信息 ==========
    
    /// 最后写入时间戳（纳秒）
    std::atomic<uint64_t> last_write_time;
    
    /// 最后读取时间戳（纳秒）
    std::atomic<uint64_t> last_read_time;
    
    /// 填充
    char padding5[64 - 2 * sizeof(std::atomic<uint64_t>)];
    
    /**
     * @brief 初始化控制块
     */
    void initialize() {
        write_offset.store(0, std::memory_order_relaxed);
        
        // 初始化消费者注册表
        consumers.initialize();
        
        producer_count.store(0, std::memory_order_relaxed);
        total_pushed.store(0, std::memory_order_relaxed);
        total_popped.store(0, std::memory_order_relaxed);
        overwrite_count.store(0, std::memory_order_relaxed);
        status_flags.store(0, std::memory_order_relaxed);
        last_write_time.store(0, std::memory_order_relaxed);
        last_read_time.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief 检查队列是否已关闭
     * @return true 如果队列已关闭
     */
    bool is_closed() const {
        return (status_flags.load(std::memory_order_acquire) & 0x1) != 0;
    }
    
    /**
     * @brief 关闭队列
     */
    void close() {
        status_flags.fetch_or(0x1, std::memory_order_release);
    }
};

/**
 * @brief 元素头部结构
 * 
 * 每个队列元素都包含一个头部，存储元数据
 */
struct ElementHeader {
    /// 时间戳（纳秒，如果队列启用时间戳）
    uint64_t timestamp;
    
    /// 序列号（用于检测数据完整性）
    uint64_t sequence_id;
    
    /// 数据大小（实际数据的字节数）
    uint32_t data_size;
    
    /// 标志位（bit 0: 数据有效, bit 1: 数据已读, bit 2-31: 保留）
    std::atomic<uint32_t> flags;
    
    /// 校验和（可选）
    uint32_t checksum;
    
    /// 保留字段
    uint32_t reserved;
    
    // 标志位定义
    static constexpr uint32_t FLAG_VALID = 0x1;      // 数据有效
    static constexpr uint32_t FLAG_READ = 0x2;       // 数据已读
    static constexpr uint32_t FLAG_CORRUPTED = 0x4;  // 数据损坏
    
    /**
     * @brief 初始化元素头部
     * @param seq 序列号
     * @param ts 时间戳
     * @param size 数据大小
     */
    void initialize(uint64_t seq, uint64_t ts, uint32_t size) {
        sequence_id = seq;
        timestamp = ts;
        data_size = size;
        flags.store(0, std::memory_order_relaxed);
        checksum = 0;
        reserved = 0;
    }
    
    /**
     * @brief 标记数据有效
     */
    void mark_valid() {
        flags.fetch_or(FLAG_VALID, std::memory_order_release);
    }
    
    /**
     * @brief 检查数据是否有效
     * @return true 如果数据有效
     */
    bool is_valid() const {
        return (flags.load(std::memory_order_acquire) & FLAG_VALID) != 0;
    }
    
    /**
     * @brief 标记数据已读
     */
    void mark_read() {
        flags.fetch_or(FLAG_READ, std::memory_order_release);
    }
    
    /**
     * @brief 检查数据是否已读
     * @return true 如果数据已读
     */
    bool is_read() const {
        return (flags.load(std::memory_order_acquire) & FLAG_READ) != 0;
    }
    
    /**
     * @brief 清除所有标志
     */
    void clear_flags() {
        flags.store(0, std::memory_order_release);
    }
};

/**
 * @brief 队列统计信息
 */
struct QueueStats {
    uint64_t total_pushed;       ///< 总写入数量
    uint64_t total_popped;       ///< 总读取数量
    uint64_t overwrite_count;    ///< 覆盖次数（非阻塞模式）
    uint32_t producer_count;     ///< 当前生产者数量
    uint32_t consumer_count;     ///< 当前消费者数量
    size_t current_size;         ///< 当前队列大小
    size_t capacity;             ///< 队列容量
    uint64_t created_at;         ///< 创建时间
    uint64_t last_write_time;    ///< 最后写入时间
    uint64_t last_read_time;     ///< 最后读取时间
    bool is_closed;              ///< 队列是否已关闭
    
    /**
     * @brief 默认构造函数
     */
    QueueStats() 
        : total_pushed(0), total_popped(0), overwrite_count(0),
          producer_count(0), consumer_count(0), current_size(0),
          capacity(0), created_at(0), last_write_time(0),
          last_read_time(0), is_closed(false) {}
};

} // namespace multiqueue


