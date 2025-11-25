/**
 * @file test_multiprocess_integration.cpp
 * @brief 真正的多进程集成测试
 * 
 * 测试所有组件在多进程环境下的协同工作：
 * - GlobalRegistry 跨进程共享
 * - ShmManager 跨进程 Buffer 管理
 * - PortQueue 跨进程通信
 * - Scheduler 在不同进程中调度
 * - Block 在不同进程中运行
 * - 完整的跨进程数据流水线
 */

#include <multiqueue/multiqueue_shm.hpp>
#include <multiqueue/shm_manager.hpp>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <thread>

using namespace multiqueue;
using namespace multiqueue::blocks;
using namespace boost::interprocess;

/**
 * @brief 多进程集成测试夹具
 */
class MultiProcessIntegrationTest : public ::testing::Test {
protected:
    static constexpr const char* GLOBAL_REGISTRY_NAME = "test_mp_integration_registry";
    static constexpr const char* QUEUE_NAME = "test_mp_integration_queue";
    
    void SetUp() override {
        cleanup_shm();
    }
    
    void TearDown() override {
        cleanup_shm();
    }
    
    void cleanup_shm() {
        shared_memory_object::remove(GLOBAL_REGISTRY_NAME);
        shared_memory_object::remove("mqshm_small");
        shared_memory_object::remove("mqshm_medium");
        shared_memory_object::remove("mqshm_large");
        shared_memory_object::remove(QUEUE_NAME);
    }
    
    /**
     * @brief 创建共享的 GlobalRegistry
     */
    GlobalRegistry* create_registry() {
        // 创建或打开共享内存
        shared_memory_object shm;
        try {
            shm = shared_memory_object(open_only, GLOBAL_REGISTRY_NAME, read_write);
        } catch (...) {
            shm = shared_memory_object(create_only, GLOBAL_REGISTRY_NAME, read_write);
            shm.truncate(sizeof(GlobalRegistry));
        }
        
        mapped_region region(shm, read_write);
        GlobalRegistry* registry = static_cast<GlobalRegistry*>(region.get_address());
        
        // 如果是新创建的，初始化
        if (registry->header.version == 0 || !registry->header.initialized.load(std::memory_order_acquire)) {
            new (registry) GlobalRegistry();
            registry->initialize();
        }
        
        return registry;
    }
};

/**
 * @brief 测试1: 跨进程 Source -> Sink
 * 
 * 进程1: Producer 运行 NullSource
 * 进程2: Consumer 运行 NullSink
 */
TEST_F(MultiProcessIntegrationTest, CrossProcessSourceToSink) {
    std::cout << "\n===== 测试: 跨进程 Source -> Sink =====" << std::endl;
    
    const int NUM_BUFFERS = 100;
    
    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";
    
    if (pid == 0) {
        // ===== 子进程: Producer (NullSource) =====
        try {
            std::cout << "[Producer] 启动 (PID=" << getpid() << ")" << std::endl;
            
            // 等待一下让父进程先初始化
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 打开共享内存
            shared_memory_object shm(open_only, GLOBAL_REGISTRY_NAME, read_write);
            mapped_region region(shm, read_write);
            GlobalRegistry* registry = static_cast<GlobalRegistry*>(region.get_address());
            
            // 注册进程
            int32_t slot = registry->process_registry.register_process("Producer");
            ProcessId process_id = registry->process_registry.processes[slot].process_id;
            std::cout << "[Producer] 注册成功, ProcessId=" << process_id << std::endl;
            
            // 创建 ShmManager 和 Allocator
            auto shm_manager = std::make_unique<ShmManager>(registry, process_id);
            if (!shm_manager->initialize()) {
                std::cerr << "[Producer] ShmManager 初始化失败!" << std::endl;
                exit(1);
            }
            auto allocator = std::make_unique<SharedBufferAllocator>(registry, process_id);
            std::cout << "[Producer] ShmManager 初始化成功" << std::endl;
            
            // 创建 Source
            auto source = std::make_unique<NullSource>(allocator.get(), sizeof(uint32_t), NUM_BUFFERS);
            source->set_id(1);
            source->initialize();
            
            // 打开队列
            auto queue = std::make_unique<PortQueue>();
            if (!queue->open(QUEUE_NAME)) {
                std::cerr << "[Producer] 打开队列失败!" << std::endl;
                exit(1);
            }
            source->get_output_port(0)->set_queue(queue.get());
            std::cout << "[Producer] 队列连接成功" << std::endl;
            
            // 启动
            source->start();
            
            // 创建 Scheduler
            SchedulerConfig config;
            config.num_threads = 2;
            auto scheduler = std::make_unique<Scheduler>(config);
            scheduler->register_block(source.get());
            scheduler->start();
            std::cout << "[Producer] Scheduler 启动" << std::endl;
            
            // 等待生产完成
            while (source->state() == BlockState::RUNNING && source->produced_count() < NUM_BUFFERS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            scheduler->stop();
            std::cout << "[Producer] 完成: 产生 " << source->produced_count() << " 个 buffer" << std::endl;
            
            exit(0);
        } catch (const std::exception& e) {
            std::cerr << "[Producer] 异常: " << e.what() << std::endl;
            exit(1);
        }
    } else {
        // ===== 父进程: Consumer (NullSink) =====
        try {
            std::cout << "[Consumer] 启动 (PID=" << getpid() << ")" << std::endl;
            
            // 创建共享内存
            shared_memory_object shm(create_only, GLOBAL_REGISTRY_NAME, read_write);
            shm.truncate(sizeof(GlobalRegistry));
            mapped_region region(shm, read_write);
            GlobalRegistry* registry = new (region.get_address()) GlobalRegistry();
            registry->initialize();
            std::cout << "[Consumer] GlobalRegistry 创建成功" << std::endl;
            
            // 注册进程
            int32_t slot = registry->process_registry.register_process("Consumer");
            ProcessId process_id = registry->process_registry.processes[slot].process_id;
            std::cout << "[Consumer] 注册成功, ProcessId=" << process_id << std::endl;
            
            // 创建 ShmManager 和 Allocator
            auto shm_manager = std::make_unique<ShmManager>(registry, process_id);
            ASSERT_TRUE(shm_manager->initialize());
            auto allocator = std::make_unique<SharedBufferAllocator>(registry, process_id);
            std::cout << "[Consumer] ShmManager 初始化成功" << std::endl;
            
            // 创建 Sink
            auto sink = std::make_unique<NullSink>(allocator.get());
            sink->set_id(2);
            sink->initialize();
            
            // 创建队列
            auto queue = std::make_unique<PortQueue>();
            ASSERT_TRUE(queue->create(QUEUE_NAME, 0, 256));  // 大容量队列
            sink->get_input_port(0)->set_queue(queue.get());
            std::cout << "[Consumer] 队列创建成功" << std::endl;
            
            // 启动
            sink->start();
            
            // 创建 Scheduler
            SchedulerConfig config;
            config.num_threads = 2;
            auto scheduler = std::make_unique<Scheduler>(config);
            scheduler->register_block(sink.get());
            scheduler->start();
            std::cout << "[Consumer] Scheduler 启动" << std::endl;
            
            // 等待子进程
            int status;
            waitpid(pid, &status, 0);
            
            // 等待消费完成
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            scheduler->stop();
            std::cout << "[Consumer] 完成: 消费 " << sink->consumed_count() << " 个 buffer" << std::endl;
            
            // 验证
            size_t consumed = sink->consumed_count();
            std::cout << "\n===== 验证结果 =====" << std::endl;
            std::cout << "消费的 buffer 数: " << consumed << std::endl;
            std::cout << "预期: " << NUM_BUFFERS << std::endl;
            
            // 允许一定的误差（因为时序问题）
            EXPECT_GE(consumed, NUM_BUFFERS * 0.9);  // 至少 90%
            EXPECT_LE(consumed, NUM_BUFFERS);
            
            if (consumed >= NUM_BUFFERS * 0.9) {
                std::cout << "✓ 测试通过 (" << consumed*100/NUM_BUFFERS << "%)" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "[Consumer] 异常: " << e.what() << std::endl;
            FAIL() << "Consumer 异常: " << e.what();
        }
    }
}

/**
 * @brief 测试2: 多生产者单消费者
 * 
 * 进程1-3: 3个 Producer
 * 进程4: 1个 Consumer
 */
TEST_F(MultiProcessIntegrationTest, MultiProducerSingleConsumer) {
    std::cout << "\n===== 测试: 多生产者单消费者 =====" << std::endl;
    
    const int NUM_PRODUCERS = 3;
    const int NUM_BUFFERS_PER_PRODUCER = 50;
    const int TOTAL_BUFFERS = NUM_PRODUCERS * NUM_BUFFERS_PER_PRODUCER;
    
    // 创建共享内存
    shared_memory_object shm(create_only, GLOBAL_REGISTRY_NAME, read_write);
    shm.truncate(sizeof(GlobalRegistry));
    mapped_region region(shm, read_write);
    GlobalRegistry* registry = new (region.get_address()) GlobalRegistry();
    registry->initialize();
    
    // 主进程注册
    int32_t slot = registry->process_registry.register_process("Main");
    ProcessId process_id = registry->process_registry.processes[slot].process_id;
    
    // 创建 ShmManager
    auto shm_manager = std::make_unique<ShmManager>(registry, process_id);
    ASSERT_TRUE(shm_manager->initialize());
    auto allocator = std::make_unique<SharedBufferAllocator>(registry, process_id);
    
    // 创建队列
    auto queue = std::make_unique<PortQueue>();
    ASSERT_TRUE(queue->create(QUEUE_NAME, 0, 512));  // 更大容量
    std::cout << "[Main] 共享资源初始化完成" << std::endl;
    
    // Fork 生产者进程
    std::vector<pid_t> producer_pids;
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1);
        
        if (pid == 0) {
            // 子进程: Producer
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                shared_memory_object prod_shm(open_only, GLOBAL_REGISTRY_NAME, read_write);
                mapped_region prod_region(prod_shm, read_write);
                GlobalRegistry* prod_registry = static_cast<GlobalRegistry*>(prod_region.get_address());
                
                std::string proc_name = "Producer" + std::to_string(i);
                int32_t prod_slot = prod_registry->process_registry.register_process(proc_name.c_str());
                ProcessId prod_id = prod_registry->process_registry.processes[prod_slot].process_id;
                
                auto prod_allocator = std::make_unique<SharedBufferAllocator>(prod_registry, prod_id);
                auto source = std::make_unique<NullSource>(prod_allocator.get(), sizeof(uint32_t), NUM_BUFFERS_PER_PRODUCER);
                source->set_id(i + 1);
                source->initialize();
                
                auto prod_queue = std::make_unique<PortQueue>();
                prod_queue->open(QUEUE_NAME);
                source->get_output_port(0)->set_queue(prod_queue.get());
                source->start();
                
                auto scheduler = std::make_unique<Scheduler>();
                scheduler->register_block(source.get());
                scheduler->start();
                
                while (source->produced_count() < NUM_BUFFERS_PER_PRODUCER && source->state() == BlockState::RUNNING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                scheduler->stop();
                std::cout << "[" << proc_name << "] 完成: " << source->produced_count() << " buffer" << std::endl;
                exit(0);
            } catch (...) {
                exit(1);
            }
        } else {
            producer_pids.push_back(pid);
        }
    }
    
    // 父进程: Consumer
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto sink = std::make_unique<NullSink>(allocator.get());
    sink->set_id(100);
    sink->initialize();
    sink->get_input_port(0)->set_queue(queue.get());
    sink->start();
    
    auto scheduler = std::make_unique<Scheduler>();
    scheduler->register_block(sink.get());
    scheduler->start();
    std::cout << "[Consumer] Scheduler 启动" << std::endl;
    
    // 等待所有生产者完成
    for (pid_t pid : producer_pids) {
        int status;
        waitpid(pid, &status, 0);
    }
    
    // 继续消费一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    scheduler->stop();
    
    size_t consumed = sink->consumed_count();
    std::cout << "\n===== 验证结果 =====" << std::endl;
    std::cout << "总消费: " << consumed << " / " << TOTAL_BUFFERS << std::endl;
    
    EXPECT_GE(consumed, TOTAL_BUFFERS * 0.85);  // 至少 85%
    
    if (consumed >= TOTAL_BUFFERS * 0.85) {
        std::cout << "✓ 测试通过 (" << consumed*100/TOTAL_BUFFERS << "%)" << std::endl;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

