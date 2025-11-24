/**
 * @file test_timestamp.cpp
 * @brief 测试时间戳结构
 */

#include <multiqueue/timestamp.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace multiqueue;

TEST(TimestampTest, Construction) {
    Timestamp ts1;
    EXPECT_EQ(ts1.to_nanoseconds(), 0);
    EXPECT_FALSE(ts1.valid());
    
    Timestamp ts2(1000);
    EXPECT_EQ(ts2.to_nanoseconds(), 1000);
    EXPECT_TRUE(ts2.valid());
}

TEST(TimestampTest, Now) {
    Timestamp ts1 = Timestamp::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Timestamp ts2 = Timestamp::now();
    
    EXPECT_TRUE(ts1.valid());
    EXPECT_TRUE(ts2.valid());
    EXPECT_GT(ts2.to_nanoseconds(), ts1.to_nanoseconds());
}

TEST(TimestampTest, FromSeconds) {
    Timestamp ts = Timestamp::from_seconds(1.5);
    EXPECT_EQ(ts.to_nanoseconds(), 1500000000);
    EXPECT_DOUBLE_EQ(ts.to_seconds(), 1.5);
}

TEST(TimestampTest, FromMilliseconds) {
    Timestamp ts = Timestamp::from_milliseconds(1500.0);
    EXPECT_EQ(ts.to_nanoseconds(), 1500000000);
    EXPECT_DOUBLE_EQ(ts.to_milliseconds(), 1500.0);
}

TEST(TimestampTest, Comparison) {
    Timestamp ts1(1000);
    Timestamp ts2(2000);
    
    EXPECT_TRUE(ts1 < ts2);
    EXPECT_TRUE(ts1 <= ts2);
    EXPECT_TRUE(ts2 > ts1);
    EXPECT_TRUE(ts2 >= ts1);
    EXPECT_TRUE(ts1 != ts2);
    EXPECT_FALSE(ts1 == ts2);
}

TEST(TimestampTest, Arithmetic) {
    Timestamp ts1(1000);
    Timestamp ts2(500);
    
    Timestamp sum = ts1 + ts2;
    EXPECT_EQ(sum.to_nanoseconds(), 1500);
    
    Timestamp diff = ts1 - ts2;
    EXPECT_EQ(diff.to_nanoseconds(), 500);
}

TEST(TimestampTest, AbsDiff) {
    Timestamp ts1(1000);
    Timestamp ts2(2000);
    
    Timestamp diff1 = abs_diff(ts1, ts2);
    Timestamp diff2 = abs_diff(ts2, ts1);
    
    EXPECT_EQ(diff1.to_nanoseconds(), 1000);
    EXPECT_EQ(diff2.to_nanoseconds(), 1000);
}

TEST(TimeRangeTest, Construction) {
    Timestamp start(1000);
    Timestamp end(2000);
    TimeRange range(start, end);
    
    EXPECT_TRUE(range.valid());
    EXPECT_EQ(range.duration().to_nanoseconds(), 1000);
}

TEST(TimeRangeTest, Contains) {
    Timestamp start(1000);
    Timestamp end(2000);
    TimeRange range(start, end);
    
    EXPECT_TRUE(range.contains(Timestamp(1500)));
    EXPECT_FALSE(range.contains(Timestamp(500)));
    EXPECT_FALSE(range.contains(Timestamp(2500)));
}

TEST(TimeRangeTest, Overlaps) {
    TimeRange range1(Timestamp(1000), Timestamp(2000));
    TimeRange range2(Timestamp(1500), Timestamp(2500));
    TimeRange range3(Timestamp(3000), Timestamp(4000));
    
    EXPECT_TRUE(range1.overlaps(range2));
    EXPECT_TRUE(range2.overlaps(range1));
    EXPECT_FALSE(range1.overlaps(range3));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

