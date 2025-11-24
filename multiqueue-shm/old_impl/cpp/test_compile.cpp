/**
 * @file test_compile.cpp
 * @brief 编译测试：验证所有头文件可以正确编译
 * 
 * 这个文件不需要运行，只需要能够编译通过即可
 * 用于验证接口定义的正确性
 */

#include <multiqueue/multiqueue_shm.hpp>
#include <iostream>
#include <vector>

using namespace multiqueue;

// 测试基本类型
struct TestData {
    int id;
    double value;
    char name[32];
};

// 验证所有接口都能编译
int main() {
    std::cout << "Testing MultiQueue-SHM interface compilation..." << std::endl;
    
    // 1. 测试配置类
    {
        QueueConfig config;
        config.capacity = 1024;
        config.blocking_mode = BlockingMode::BLOCKING;
        config.timeout_ms = 1000;
        config.has_timestamp = true;
        config.queue_name = "test_queue";
        
        std::cout << "✓ QueueConfig: OK" << std::endl;
        std::cout << "  - Capacity: " << config.capacity << std::endl;
        std::cout << "  - Blocking mode: " << static_cast<int>(config.blocking_mode) << std::endl;
        std::cout << "  - Valid: " << (config.is_valid() ? "yes" : "no") << std::endl;
    }
    
    // 2. 测试元数据结构
    {
        QueueMetadata metadata;
        QueueConfig config;
        config.capacity = 1024;
        config.queue_name = "test";
        metadata.initialize(config, sizeof(int));
        
        std::cout << "✓ QueueMetadata: OK" << std::endl;
        std::cout << "  - Version: " << metadata.get_version_string() << std::endl;
        std::cout << "  - Valid: " << (metadata.is_valid() ? "yes" : "no") << std::endl;
    }
    
    // 3. 测试控制块
    {
        ControlBlock control;
        control.initialize();
        
        std::cout << "✓ ControlBlock: OK" << std::endl;
        std::cout << "  - Write offset: " << control.write_offset.load() << std::endl;
        std::cout << "  - Active consumers: " << control.consumers.active_count.load() << std::endl;
    }
    
    // 4. 测试元素头部
    {
        ElementHeader header;
        header.initialize(0, 12345, sizeof(int));
        header.mark_valid();
        
        std::cout << "✓ ElementHeader: OK" << std::endl;
        std::cout << "  - Valid: " << (header.is_valid() ? "yes" : "no") << std::endl;
    }
    
    // 5. 测试统计信息
    {
        QueueStats stats;
        stats.total_pushed = 100;
        stats.total_popped = 50;
        
        std::cout << "✓ QueueStats: OK" << std::endl;
    }
    
    // 6. 测试 RingQueue 接口（仅声明，不创建实例）
    {
        // 这里只测试类型是否可以声明
        // 实际创建需要 Boost.Interprocess，我们稍后测试
        using IntQueue = RingQueue<int>;
        using DoubleQueue = RingQueue<double>;
        using StructQueue = RingQueue<TestData>;
        
        // 避免未使用警告
        (void)sizeof(IntQueue);
        (void)sizeof(DoubleQueue);
        (void)sizeof(StructQueue);
        
        std::cout << "✓ RingQueue<T>: Interface OK" << std::endl;
        std::cout << "  - RingQueue<int>: type OK" << std::endl;
        std::cout << "  - RingQueue<double>: type OK" << std::endl;
        std::cout << "  - RingQueue<TestData>: type OK" << std::endl;
    }
    
    // 7. 测试 QueueManager 接口
    {
        // 类型声明测试
        QueueManager* manager = nullptr;
        (void)manager;  // 避免未使用警告
        
        std::cout << "✓ QueueManager: Interface OK" << std::endl;
    }
    
    // 8. 测试 TimestampSynchronizer
    {
        uint64_t ts = TimestampSynchronizer::now();
        uint64_t ts_micros = TimestampSynchronizer::now_micros();
        uint64_t ts_millis = TimestampSynchronizer::now_millis();
        
        std::cout << "✓ TimestampSynchronizer: OK" << std::endl;
        std::cout << "  - Timestamp (nanos): " << ts << std::endl;
        std::cout << "  - Timestamp (micros): " << ts_micros << std::endl;
        std::cout << "  - Timestamp (millis): " << ts_millis << std::endl;
    }
    
    // 9. 测试版本信息
    {
        std::string version = get_version_string();
        std::string full_version = get_full_version_string();
        
        std::cout << "✓ Version Info: OK" << std::endl;
        std::cout << "  - Version: " << version << std::endl;
        std::cout << "  - Full version: " << full_version << std::endl;
    }
    
    std::cout << "\n==================================" << std::endl;
    std::cout << "All interface compilation tests PASSED! ✅" << std::endl;
    std::cout << "==================================" << std::endl;
    
    return 0;
}

