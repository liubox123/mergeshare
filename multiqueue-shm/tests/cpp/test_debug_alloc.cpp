/**
 * @file test_debug_alloc.cpp
 * @brief 调试 Buffer 分配问题
 */

#include <gtest/gtest.h>
#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <multiqueue/buffer_pool.hpp>
#include <iostream>

using namespace multiqueue;

TEST(DebugAlloc, CheckPools) {
    const char* registry_name = "test_debug_registry";
    shared_memory_object::remove(registry_name);
    
    // 创建 GlobalRegistry
    shared_memory_object registry_shm(create_only, registry_name, read_write);
    registry_shm.truncate(sizeof(GlobalRegistry));
    mapped_region registry_region(registry_shm, read_write);
    GlobalRegistry* registry = new (registry_region.get_address()) GlobalRegistry();
    
    // 创建 ShmManager
    ProcessId process_id = 1;
    ShmConfig config;
    config.name_prefix = "mqshm_";
    config.pools = {
        PoolConfig("small", 4096, 100),
        PoolConfig("medium", 65536, 50),
        PoolConfig("large", 1048576, 20)
    };
    ShmManager manager(registry, process_id, config);
    
    std::cout << "Initializing ShmManager..." << std::endl;
    ASSERT_TRUE(manager.initialize());
    
    // 检查 GlobalRegistry 中的 pools
    std::cout << "Checking GlobalRegistry pools..." << std::endl;
    for (uint32_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
        const auto& pool_info = registry->buffer_pool_registry.pools[i];
        if (pool_info.active) {
            std::cout << "Pool " << i << ": active=true, block_size=" << pool_info.block_size 
                      << ", pool_id=" << pool_info.pool_id << std::endl;
        }
    }
    
    // 检查 allocator 中的 pools
    auto* allocator = manager.allocator();
    if (allocator) {
        std::cout << "Allocator exists" << std::endl;
        
        // 尝试直接分配，看看会发生什么
        std::cout << "Trying direct allocation via allocator..." << std::endl;
        BufferId buffer_id = allocator->allocate(64);
        if (buffer_id != INVALID_BUFFER_ID) {
            std::cout << "SUCCESS: Direct allocation returned buffer_id=" << buffer_id << std::endl;
        } else {
            std::cout << "FAILED: Direct allocation returned INVALID_BUFFER_ID" << std::endl;
        }
    } else {
        std::cout << "ERROR: Allocator is null!" << std::endl;
    }
    
    // 尝试分配
    std::cout << "Allocating buffer (size=64)..." << std::endl;
    auto buffer_ptr = manager.allocate(64);
    if (buffer_ptr.valid()) {
        std::cout << "SUCCESS: Allocated buffer_id=" << buffer_ptr.id() << std::endl;
    } else {
        std::cout << "FAILED: Buffer allocation returned invalid pointer" << std::endl;
        
        // 检查是否有合适的 pool
        bool found_pool = false;
        for (uint32_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
            const auto& pool_info = registry->buffer_pool_registry.pools[i];
            if (pool_info.active && pool_info.block_size >= 64) {
                std::cout << "Found suitable pool: pool_id=" << pool_info.pool_id 
                          << ", block_size=" << pool_info.block_size 
                          << ", shm_name=" << pool_info.shm_name << std::endl;
                found_pool = true;
                
                // 尝试手动打开这个 pool
                BufferPool test_pool;
                std::string shm_name = "mqshm_small";  // 根据配置
                if (test_pool.open(shm_name.c_str())) {
                    std::cout << "  -> Pool can be opened manually" << std::endl;
                } else {
                    std::cout << "  -> Pool CANNOT be opened manually!" << std::endl;
                }
                break;
            }
        }
        if (!found_pool) {
            std::cout << "No suitable pool found for size 64" << std::endl;
        }
    }
    
    shared_memory_object::remove(registry_name);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

