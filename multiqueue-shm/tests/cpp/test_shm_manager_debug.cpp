/**
 * @file test_shm_manager_debug.cpp
 * @brief 调试版本的 ShmManager 测试
 */

#include <multiqueue/shm_manager.hpp>
#include <multiqueue/global_registry.hpp>
#include <gtest/gtest.h>
#include <iostream>

using namespace multiqueue;
using namespace boost::interprocess;

int main() {
    std::cout << "========== ShmManager 调试测试 ==========" << std::endl;
    
    // 清理旧的共享内存
    shared_memory_object::remove("test_debug_global_registry");
    shared_memory_object::remove("mqshm_small");
    shared_memory_object::remove("mqshm_medium");
    shared_memory_object::remove("mqshm_large");
    
    std::cout << "1. 创建 GlobalRegistry..." << std::endl;
    
    // 创建全局注册表
    shared_memory_object global_shm(
        create_only,
        "test_debug_global_registry",
        read_write
    );
    global_shm.truncate(sizeof(GlobalRegistry));
    mapped_region global_region(global_shm, read_write);
    
    GlobalRegistry* registry = new (global_region.get_address()) GlobalRegistry();
    registry->initialize();
    
    std::cout << "2. GlobalRegistry 创建成功" << std::endl;
    
    // 注册进程
    int32_t process_slot = registry->process_registry.register_process("DebugProcess");
    if (process_slot < 0) {
        std::cout << "ERROR: 进程注册失败" << std::endl;
        return 1;
    }
    
    ProcessId process_id = registry->process_registry.processes[process_slot].process_id;
    std::cout << "3. 进程注册成功, ProcessId = " << process_id << std::endl;
    
    // 创建 ShmManager
    ShmConfig config = ShmConfig::default_config();
    std::cout << "4. 默认配置包含 " << config.pools.size() << " 个池:" << std::endl;
    for (const auto& pool_config : config.pools) {
        std::cout << "   - " << pool_config.name 
                  << ": " << pool_config.block_size << " bytes × " 
                  << pool_config.block_count << std::endl;
    }
    
    std::cout << "5. 创建 ShmManager..." << std::endl;
    ShmManager manager(registry, process_id, config);
    
    std::cout << "6. 初始化 ShmManager..." << std::endl;
    
    // 手动测试 add_pool
    std::cout << "7. 测试单独添加一个池..." << std::endl;
    
    PoolConfig test_pool("test_pool", 4096, 64);
    
    std::cout << "   7.1 检查 registry 指针..." << std::endl;
    if (!registry) {
        std::cout << "   ERROR: registry 是 nullptr" << std::endl;
        return 1;
    }
    std::cout << "   7.1 OK" << std::endl;
    
    std::cout << "   7.2 创建 BufferPool..." << std::endl;
    auto pool = std::make_shared<BufferPool>();
    std::string shm_name = config.name_prefix + test_pool.name;
    std::cout << "   共享内存名称: " << shm_name << std::endl;
    
    bool create_result = pool->create(shm_name.c_str(), 0, test_pool.block_size, test_pool.block_count);
    if (!create_result) {
        std::cout << "   ERROR: BufferPool::create() 失败" << std::endl;
        return 1;
    }
    std::cout << "   7.2 OK - BufferPool 创建成功" << std::endl;
    
    std::cout << "   7.3 在 GlobalRegistry 中注册..." << std::endl;
    bool register_result = registry->buffer_pool_registry.register_pool(
        test_pool.block_size,
        test_pool.block_count,
        shm_name.c_str()
    );
    if (!register_result) {
        std::cout << "   ERROR: GlobalRegistry::register_pool() 失败" << std::endl;
        return 1;
    }
    std::cout << "   7.3 OK - GlobalRegistry 注册成功" << std::endl;
    
    std::cout << "   7.4 创建 SharedBufferAllocator..." << std::endl;
    SharedBufferAllocator allocator(registry, process_id);
    std::cout << "   7.4 OK" << std::endl;
    
    std::cout << "   7.5 在 Allocator 中注册池..." << std::endl;
    bool allocator_register = allocator.register_pool(0, shm_name.c_str());
    if (!allocator_register) {
        std::cout << "   ERROR: SharedBufferAllocator::register_pool() 失败" << std::endl;
        return 1;
    }
    std::cout << "   7.5 OK - Allocator 注册成功" << std::endl;
    
    std::cout << "\n8. 现在测试 ShmManager::initialize()..." << std::endl;
    bool init_result = manager.initialize();
    if (!init_result) {
        std::cout << "ERROR: ShmManager::initialize() 失败" << std::endl;
        return 1;
    }
    
    std::cout << "SUCCESS: ShmManager 初始化成功！" << std::endl;
    
    // 测试分配
    std::cout << "\n9. 测试 Buffer 分配..." << std::endl;
    BufferPtr buffer = manager.allocate(2048);
    if (!buffer.valid()) {
        std::cout << "ERROR: Buffer 分配失败" << std::endl;
        return 1;
    }
    std::cout << "SUCCESS: Buffer 分配成功！" << std::endl;
    
    // 打印统计信息
    std::cout << "\n10. 统计信息:" << std::endl;
    manager.print_stats();
    
    // 清理
    shared_memory_object::remove("test_debug_global_registry");
    shared_memory_object::remove("mqshm_small");
    shared_memory_object::remove("mqshm_medium");
    shared_memory_object::remove("mqshm_large");
    shared_memory_object::remove(shm_name.c_str());
    
    std::cout << "\n========== 所有测试通过！ ==========" << std::endl;
    
    return 0;
}



