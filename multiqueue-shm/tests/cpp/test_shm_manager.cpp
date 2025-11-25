/**
 * @file test_shm_manager.cpp
 * @brief 测试 ShmManager 共享内存管理器
 */

#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace multiqueue;
using namespace boost::interprocess;

/**
 * @brief ShmManager 测试夹具
 */
class ShmManagerTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_shm_manager_global_registry";
    
    GlobalRegistry* registry_;
    shared_memory_object global_shm_;
    mapped_region global_region_;
    ProcessId process_id_;
    
    void SetUp() override {
        // 清理旧的共享内存
        cleanup_shm();
        
        // 创建全局注册表
        global_shm_ = shared_memory_object(
            create_only,
            GLOBAL_REGISTRY_NAME,
            read_write
        );
        global_shm_.truncate(sizeof(GlobalRegistry));
        global_region_ = mapped_region(global_shm_, read_write);
        
        registry_ = new (global_region_.get_address()) GlobalRegistry();
        registry_->initialize();
        
        // 注册进程
        int32_t process_slot = registry_->process_registry.register_process("TestProcess");
        ASSERT_GE(process_slot, 0);
        process_id_ = registry_->process_registry.processes[process_slot].process_id;
    }
    
    void TearDown() override {
        cleanup_shm();
    }
    
    void cleanup_shm() {
        // 清理 GlobalRegistry
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        
        // 清理可能的 BufferPool
        const char* pool_names[] = {
            "mqshm_small",
            "mqshm_medium",
            "mqshm_large",
            "test_pool_4k",
            "test_pool_64k",
            "custom_pool",
            "dynamic_pool_0",
            "dynamic_pool_1",
            "dynamic_pool_2"
        };
        
        for (const char* name : pool_names) {
            shared_memory_object::remove(name);
        }
    }
};

/**
 * @brief 测试 ShmManager 构造和初始化
 */
TEST_F(ShmManagerTest, Construction) {
    ShmConfig config = ShmConfig::default_config();
    ShmManager manager(registry_, process_id_, config);
    
    EXPECT_FALSE(manager.is_initialized());
    EXPECT_TRUE(manager.initialize());
    EXPECT_TRUE(manager.is_initialized());
    
    // 不能重复初始化
    EXPECT_FALSE(manager.initialize());
}

/**
 * @brief 测试默认配置
 */
TEST_F(ShmManagerTest, DefaultConfig) {
    ShmConfig config = ShmConfig::default_config();
    
    // 默认配置应该有 3 个池
    EXPECT_EQ(config.pools.size(), 3);
    
    // 验证池配置
    EXPECT_EQ(config.pools[0].name, "small");
    EXPECT_EQ(config.pools[0].block_size, 4096);
    EXPECT_EQ(config.pools[0].block_count, 1024);
    
    EXPECT_EQ(config.pools[1].name, "medium");
    EXPECT_EQ(config.pools[1].block_size, 65536);
    EXPECT_EQ(config.pools[1].block_count, 512);
    
    EXPECT_EQ(config.pools[2].name, "large");
    EXPECT_EQ(config.pools[2].block_size, 1048576);
    EXPECT_EQ(config.pools[2].block_count, 128);
}

/**
 * @brief 测试自定义配置
 */
TEST_F(ShmManagerTest, CustomConfig) {
    ShmConfig config;
    config.name_prefix = "test_";
    config.pools = {
        PoolConfig("pool_4k", 4096, 64),
        PoolConfig("pool_64k", 65536, 32)
    };
    
    ShmManager manager(registry_, process_id_, config);
    ASSERT_TRUE(manager.initialize());
    
    // 验证池已创建
    auto pools = manager.list_pools();
    EXPECT_EQ(pools.size(), 2);
    
    EXPECT_NE(std::find(pools.begin(), pools.end(), "pool_4k"), pools.end());
    EXPECT_NE(std::find(pools.begin(), pools.end(), "pool_64k"), pools.end());
}

/**
 * @brief 测试 Buffer 分配（自动选择池）
 */
TEST_F(ShmManagerTest, AllocateBuffer) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 分配小 Buffer（应该从 small 池分配）
    BufferPtr buffer1 = manager.allocate(2048);
    ASSERT_TRUE(buffer1.valid());
    EXPECT_GE(buffer1.size(), 2048);
    
    // 分配中等 Buffer（应该从 medium 池分配）
    BufferPtr buffer2 = manager.allocate(32768);
    ASSERT_TRUE(buffer2.valid());
    EXPECT_GE(buffer2.size(), 32768);
    
    // 分配大 Buffer（应该从 large 池分配）
    BufferPtr buffer3 = manager.allocate(524288);
    ASSERT_TRUE(buffer3.valid());
    EXPECT_GE(buffer3.size(), 524288);
}

/**
 * @brief 测试从指定池分配
 */
TEST_F(ShmManagerTest, AllocateFromPool) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 从 small 池分配
    BufferPtr buffer1 = manager.allocate_from_pool("small");
    ASSERT_TRUE(buffer1.valid());
    
    // 从 medium 池分配
    BufferPtr buffer2 = manager.allocate_from_pool("medium");
    ASSERT_TRUE(buffer2.valid());
    
    // 从 large 池分配
    BufferPtr buffer3 = manager.allocate_from_pool("large");
    ASSERT_TRUE(buffer3.valid());
    
    // 从不存在的池分配
    BufferPtr buffer_invalid = manager.allocate_from_pool("nonexistent");
    EXPECT_FALSE(buffer_invalid.valid());
}

/**
 * @brief 测试动态添加池
 */
TEST_F(ShmManagerTest, AddPool) {
    ShmConfig config;
    config.name_prefix = "test_";
    config.pools.clear();  // 不使用预定义的池
    
    ShmManager manager(registry_, process_id_, config);
    ASSERT_TRUE(manager.initialize());
    
    // 初始没有池
    EXPECT_EQ(manager.list_pools().size(), 0);
    
    // 动态添加池
    PoolConfig pool_config("custom_pool", 8192, 128);
    EXPECT_TRUE(manager.add_pool(pool_config));
    
    // 验证池已添加
    auto pools = manager.list_pools();
    EXPECT_EQ(pools.size(), 1);
    EXPECT_EQ(pools[0], "custom_pool");
    
    // 可以从新添加的池分配
    BufferPtr buffer = manager.allocate_from_pool("custom_pool");
    EXPECT_TRUE(buffer.valid());
}

/**
 * @brief 测试移除池
 */
TEST_F(ShmManagerTest, RemovePool) {
    ShmConfig config;
    config.name_prefix = "test_";
    config.pools = {
        PoolConfig("dynamic_pool_0", 4096, 64),
        PoolConfig("dynamic_pool_1", 8192, 32)
    };
    
    ShmManager manager(registry_, process_id_, config);
    ASSERT_TRUE(manager.initialize());
    
    EXPECT_EQ(manager.list_pools().size(), 2);
    
    // 移除一个池
    manager.remove_pool("dynamic_pool_0");
    
    auto pools = manager.list_pools();
    EXPECT_EQ(pools.size(), 1);
    EXPECT_EQ(pools[0], "dynamic_pool_1");
    
    // 移除不存在的池（不应该崩溃）
    manager.remove_pool("nonexistent");
    EXPECT_EQ(manager.list_pools().size(), 1);
}

/**
 * @brief 测试获取池指针
 */
TEST_F(ShmManagerTest, GetPool) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 获取存在的池
    BufferPool* small_pool = manager.get_pool("small");
    ASSERT_NE(small_pool, nullptr);
    ASSERT_NE(small_pool->header(), nullptr);
    EXPECT_EQ(small_pool->header()->block_size, 4096);
    
    BufferPool* medium_pool = manager.get_pool("medium");
    ASSERT_NE(medium_pool, nullptr);
    ASSERT_NE(medium_pool->header(), nullptr);
    EXPECT_EQ(medium_pool->header()->block_size, 65536);
    
    // 获取不存在的池
    BufferPool* invalid_pool = manager.get_pool("nonexistent");
    EXPECT_EQ(invalid_pool, nullptr);
}

/**
 * @brief 测试统计信息
 */
TEST_F(ShmManagerTest, Statistics) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 初始统计
    ShmStats stats = manager.get_stats();
    EXPECT_EQ(stats.total_pools, 3);  // small, medium, large
    EXPECT_GT(stats.total_capacity, 0);
    EXPECT_EQ(stats.total_allocated, 0);  // 还没分配任何 Buffer
    EXPECT_EQ(stats.allocation_count, 0);
    
    // 分配一些 Buffer
    BufferPtr buffer1 = manager.allocate(2048);
    BufferPtr buffer2 = manager.allocate(32768);
    BufferPtr buffer3 = manager.allocate(524288);
    
    // 更新统计
    stats = manager.get_stats();
    EXPECT_EQ(stats.allocation_count, 3);
    EXPECT_GT(stats.total_allocated, 0);
    
    // 验证每个池的统计
    EXPECT_EQ(stats.pool_stats.size(), 3);
    
    for (const auto& pool_stat : stats.pool_stats) {
        EXPECT_GT(pool_stat.block_count, 0);
        EXPECT_GE(pool_stat.blocks_free, 0);
        EXPECT_GE(pool_stat.utilization, 0.0);
        EXPECT_LE(pool_stat.utilization, 1.0);
    }
}

/**
 * @brief 测试打印统计信息
 */
TEST_F(ShmManagerTest, PrintStatistics) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 分配一些 Buffer
    std::vector<BufferPtr> buffers;
    for (int i = 0; i < 10; ++i) {
        buffers.push_back(manager.allocate(2048));
    }
    
    // 打印统计信息
    std::cout << "\n";
    manager.print_stats();
    std::cout << std::endl;
    
    // 验证不崩溃即可
}

/**
 * @brief 测试池利用率
 */
TEST_F(ShmManagerTest, PoolUtilization) {
    ShmConfig config;
    config.name_prefix = "test_";
    config.pools = {
        PoolConfig("test_pool", 4096, 10)  // 小池，便于测试
    };
    
    ShmManager manager(registry_, process_id_, config);
    ASSERT_TRUE(manager.initialize());
    
    // 初始利用率应该是 0
    ShmStats stats = manager.get_stats();
    ASSERT_EQ(stats.pool_stats.size(), 1);
    EXPECT_EQ(stats.pool_stats[0].utilization, 0.0);
    
    // 分配一半的 Buffer
    std::vector<BufferPtr> buffers;
    for (int i = 0; i < 5; ++i) {
        buffers.push_back(manager.allocate_from_pool("test_pool"));
        ASSERT_TRUE(buffers.back().valid());
    }
    
    // 利用率应该是 0.5
    stats = manager.get_stats();
    EXPECT_NEAR(stats.pool_stats[0].utilization, 0.5, 0.01);
    
    // 分配所有 Buffer
    for (int i = 0; i < 5; ++i) {
        buffers.push_back(manager.allocate_from_pool("test_pool"));
        ASSERT_TRUE(buffers.back().valid());
    }
    
    // 利用率应该是 1.0
    stats = manager.get_stats();
    EXPECT_NEAR(stats.pool_stats[0].utilization, 1.0, 0.01);
    
    // 尝试分配更多应该失败
    BufferPtr buffer_overflow = manager.allocate_from_pool("test_pool");
    EXPECT_FALSE(buffer_overflow.valid());
}

/**
 * @brief 测试多线程分配
 */
TEST_F(ShmManagerTest, MultithreadedAllocation) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    constexpr size_t NUM_THREADS = 4;
    constexpr size_t ALLOCS_PER_THREAD = 50;
    
    std::atomic<size_t> success_count{0};
    std::vector<std::thread> threads;
    
    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&manager, &success_count]() {
            for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
                // 分配不同大小的 Buffer
                size_t size = (i % 3 == 0) ? 2048 : (i % 3 == 1) ? 32768 : 524288;
                BufferPtr buffer = manager.allocate(size);
                
                if (buffer.valid()) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "成功分配了 " << success_count.load() << " 个 Buffer" << std::endl;
    
    // 验证统计信息
    ShmStats stats = manager.get_stats();
    std::cout << "分配计数: " << stats.allocation_count << std::endl;
    
    EXPECT_EQ(stats.allocation_count, success_count.load());
    EXPECT_GT(success_count.load(), NUM_THREADS * ALLOCS_PER_THREAD * 0.8);  // 至少 80% 成功
}

/**
 * @brief 测试池选择策略
 */
TEST_F(ShmManagerTest, PoolSelectionStrategy) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    // 分配各种大小的 Buffer
    struct TestCase {
        size_t size;
        std::string expected_pool;  // 期望选择的池
    };
    
    std::vector<TestCase> test_cases = {
        {1024,    "small"},   // 1KB -> small (4KB)
        {4096,    "small"},   // 4KB -> small (4KB)
        {8192,    "medium"},  // 8KB -> medium (64KB)
        {65536,   "medium"},  // 64KB -> medium (64KB)
        {131072,  "large"},   // 128KB -> large (1MB)
        {1048576, "large"}    // 1MB -> large (1MB)
    };
    
    for (const auto& test_case : test_cases) {
        ShmStats stats_before = manager.get_stats();
        
        BufferPtr buffer = manager.allocate(test_case.size);
        ASSERT_TRUE(buffer.valid()) << "分配 " << test_case.size << " 字节失败";
        
        ShmStats stats_after = manager.get_stats();
        
        // 验证分配计数增加
        EXPECT_EQ(stats_after.allocation_count, stats_before.allocation_count + 1);
        
        std::cout << "分配 " << test_case.size << " 字节成功" << std::endl;
    }
}

/**
 * @brief 测试关闭和重新初始化
 */
TEST_F(ShmManagerTest, ShutdownAndReinitialize) {
    ShmManager manager(registry_, process_id_);
    
    // 第一次初始化
    ASSERT_TRUE(manager.initialize());
    BufferPtr buffer1 = manager.allocate(2048);
    EXPECT_TRUE(buffer1.valid());
    
    // 关闭
    manager.shutdown();
    EXPECT_FALSE(manager.is_initialized());
    
    // 重新初始化
    ASSERT_TRUE(manager.initialize());
    EXPECT_TRUE(manager.is_initialized());
    
    // 应该可以再次分配
    BufferPtr buffer2 = manager.allocate(2048);
    EXPECT_TRUE(buffer2.valid());
}

/**
 * @brief 压力测试
 */
TEST_F(ShmManagerTest, StressTest) {
    ShmManager manager(registry_, process_id_);
    ASSERT_TRUE(manager.initialize());
    
    auto start_time = std::chrono::steady_clock::now();
    
    constexpr size_t NUM_ALLOCATIONS = 1000;
    std::vector<BufferPtr> buffers;
    buffers.reserve(NUM_ALLOCATIONS);
    
    // 大量分配
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        size_t size = (i % 3 == 0) ? 2048 : (i % 3 == 1) ? 32768 : 524288;
        BufferPtr buffer = manager.allocate(size);
        
        if (buffer.valid()) {
            buffers.push_back(std::move(buffer));
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    );
    
    std::cout << "\n========== 压力测试结果 ==========" << std::endl;
    std::cout << "分配次数: " << NUM_ALLOCATIONS << std::endl;
    std::cout << "成功次数: " << buffers.size() << std::endl;
    std::cout << "成功率: " << (buffers.size() * 100.0 / NUM_ALLOCATIONS) << "%" << std::endl;
    std::cout << "耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均速度: " << (buffers.size() * 1000 / duration.count()) << " 次/秒" << std::endl;
    
    manager.print_stats();
    
    std::cout << "=================================" << std::endl;
    
    // 至少应该有 50% 成功率（取决于池大小）
    EXPECT_GT(buffers.size(), NUM_ALLOCATIONS * 0.5);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

