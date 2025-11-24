/**
 * @file timestamp.hpp
 * @brief 时间戳结构定义
 * 
 * 提供高精度时间戳支持，用于多流同步
 */

#pragma once

#include "types.hpp"
#include <chrono>
#include <cstdint>
#include <string>

namespace multiqueue {

/**
 * @brief 时间戳结构
 * 
 * 存储纳秒精度的时间戳，用于数据同步
 * 注意：此结构必须是 POD 类型，可以存储在共享内存中
 */
struct Timestamp {
    /// 时间戳（纳秒，自 epoch 起）
    TimestampNs nanoseconds;
    
    /**
     * @brief 默认构造函数（时间戳为 0）
     */
    constexpr Timestamp() noexcept : nanoseconds(0) {}
    
    /**
     * @brief 从纳秒值构造
     */
    explicit constexpr Timestamp(TimestampNs ns) noexcept : nanoseconds(ns) {}
    
    /**
     * @brief 获取当前时间戳
     */
    static Timestamp now() noexcept {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
        return Timestamp(static_cast<TimestampNs>(ns));
    }
    
    /**
     * @brief 从秒数构造时间戳
     */
    static constexpr Timestamp from_seconds(double seconds) noexcept {
        return Timestamp(static_cast<TimestampNs>(seconds * 1e9));
    }
    
    /**
     * @brief 从毫秒数构造时间戳
     */
    static constexpr Timestamp from_milliseconds(double ms) noexcept {
        return Timestamp(static_cast<TimestampNs>(ms * 1e6));
    }
    
    /**
     * @brief 从微秒数构造时间戳
     */
    static constexpr Timestamp from_microseconds(double us) noexcept {
        return Timestamp(static_cast<TimestampNs>(us * 1e3));
    }
    
    /**
     * @brief 转换为秒
     */
    constexpr double to_seconds() const noexcept {
        return nanoseconds / 1e9;
    }
    
    /**
     * @brief 转换为毫秒
     */
    constexpr double to_milliseconds() const noexcept {
        return nanoseconds / 1e6;
    }
    
    /**
     * @brief 转换为微秒
     */
    constexpr double to_microseconds() const noexcept {
        return nanoseconds / 1e3;
    }
    
    /**
     * @brief 转换为纳秒
     */
    constexpr TimestampNs to_nanoseconds() const noexcept {
        return nanoseconds;
    }
    
    /**
     * @brief 是否有效（非零）
     */
    constexpr bool valid() const noexcept {
        return nanoseconds > 0;
    }
    
    // ===== 运算符重载 =====
    
    constexpr bool operator==(const Timestamp& other) const noexcept {
        return nanoseconds == other.nanoseconds;
    }
    
    constexpr bool operator!=(const Timestamp& other) const noexcept {
        return nanoseconds != other.nanoseconds;
    }
    
    constexpr bool operator<(const Timestamp& other) const noexcept {
        return nanoseconds < other.nanoseconds;
    }
    
    constexpr bool operator<=(const Timestamp& other) const noexcept {
        return nanoseconds <= other.nanoseconds;
    }
    
    constexpr bool operator>(const Timestamp& other) const noexcept {
        return nanoseconds > other.nanoseconds;
    }
    
    constexpr bool operator>=(const Timestamp& other) const noexcept {
        return nanoseconds >= other.nanoseconds;
    }
    
    constexpr Timestamp operator+(const Timestamp& other) const noexcept {
        return Timestamp(nanoseconds + other.nanoseconds);
    }
    
    constexpr Timestamp operator-(const Timestamp& other) const noexcept {
        return Timestamp(nanoseconds - other.nanoseconds);
    }
    
    Timestamp& operator+=(const Timestamp& other) noexcept {
        nanoseconds += other.nanoseconds;
        return *this;
    }
    
    Timestamp& operator-=(const Timestamp& other) noexcept {
        nanoseconds -= other.nanoseconds;
        return *this;
    }
    
    /**
     * @brief 转换为字符串（格式化输出）
     */
    std::string to_string() const {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%.9f", to_seconds());
        return std::string(buffer) + "s";
    }
} MQSHM_PACKED;

// 静态断言：确保 Timestamp 是 POD 类型
static_assert(std::is_trivially_copyable<Timestamp>::value,
              "Timestamp must be trivially copyable for shared memory");
static_assert(sizeof(Timestamp) == sizeof(TimestampNs),
              "Timestamp size must match TimestampNs");

/**
 * @brief 时间范围结构
 * 
 * 用于表示数据的有效时间范围（例如音频帧的起止时间）
 */
struct TimeRange {
    Timestamp start;  ///< 起始时间
    Timestamp end;    ///< 结束时间
    
    /**
     * @brief 默认构造函数
     */
    constexpr TimeRange() noexcept : start(), end() {}
    
    /**
     * @brief 从起止时间构造
     */
    constexpr TimeRange(Timestamp s, Timestamp e) noexcept
        : start(s), end(e) {}
    
    /**
     * @brief 是否有效
     */
    constexpr bool valid() const noexcept {
        return start.valid() && end.valid() && start < end;
    }
    
    /**
     * @brief 持续时间
     */
    constexpr Timestamp duration() const noexcept {
        return end - start;
    }
    
    /**
     * @brief 检查时间戳是否在范围内
     */
    constexpr bool contains(const Timestamp& ts) const noexcept {
        return ts >= start && ts <= end;
    }
    
    /**
     * @brief 检查两个时间范围是否重叠
     */
    constexpr bool overlaps(const TimeRange& other) const noexcept {
        return !(end < other.start || other.end < start);
    }
    
    /**
     * @brief 转换为字符串
     */
    std::string to_string() const {
        return "[" + start.to_string() + " - " + end.to_string() + "]";
    }
} MQSHM_PACKED;

// 静态断言：确保 TimeRange 是 POD 类型
static_assert(std::is_trivially_copyable<TimeRange>::value,
              "TimeRange must be trivially copyable for shared memory");

/**
 * @brief 时间戳差值的绝对值
 */
inline Timestamp abs_diff(const Timestamp& a, const Timestamp& b) noexcept {
    if (a > b) {
        return a - b;
    } else {
        return b - a;
    }
}

/**
 * @brief 时间戳线性插值
 * 
 * @param t0 时间 0
 * @param t1 时间 1
 * @param alpha 插值因子 [0, 1]
 * @return 插值后的时间戳
 */
inline Timestamp lerp_timestamp(const Timestamp& t0, const Timestamp& t1, double alpha) noexcept {
    TimestampNs ns0 = t0.to_nanoseconds();
    TimestampNs ns1 = t1.to_nanoseconds();
    TimestampNs result = ns0 + static_cast<TimestampNs>((ns1 - ns0) * alpha);
    return Timestamp(result);
}

}  // namespace multiqueue
