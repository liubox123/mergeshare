/**
 * @file global_registry.hpp
 * @brief 全局注册表定义
 * 
 * 全局注册表存储在共享内存中，管理所有进程、Block、连接、Buffer Pool 等
 */

#pragma once

#include "types.hpp"
#include "timestamp.hpp"
#include "buffer_metadata.hpp"
#include <atomic>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cstring>

namespace multiqueue {

using namespace boost::interprocess;

// ===== 进程注册表 =====

/**
 * @brief 进程信息（存储在共享内存）
 */
struct ProcessInfo {
    ProcessId process_id;                ///< 进程 ID
    ProcessState state;                  ///< 进程状态
    TimestampNs last_heartbeat_ns;       ///< 最后心跳时间
    TimestampNs start_time_ns;           ///< 启动时间
    char process_name[64];               ///< 进程名称
    
    /// 平台相关的进程 ID（用于调试）
#ifdef MQSHM_PLATFORM_WINDOWS
    uint32_t native_pid;
#else
    pid_t native_pid;
#endif
    
    ProcessInfo() noexcept
        : process_id(INVALID_PROCESS_ID)
        , state(ProcessState::STOPPED)
        , last_heartbeat_ns(0)
        , start_time_ns(0)
        , native_pid(0)
    {
        memset(process_name, 0, sizeof(process_name));
    }
    
    /**
     * @brief 更新心跳
     */
    void update_heartbeat() noexcept {
        last_heartbeat_ns = Timestamp::now().to_nanoseconds();
    }
    
    /**
     * @brief 检查是否死亡
     */
    bool is_dead(TimestampNs current_ns, TimestampNs timeout_ns) const noexcept {
        return (current_ns - last_heartbeat_ns) > timeout_ns;
    }
} MQSHM_PACKED;

/**
 * @brief 进程注册表
 */
struct ProcessRegistry {
    interprocess_mutex mutex;
    std::atomic<uint32_t> process_count;
    ProcessInfo processes[MAX_PROCESSES];
    
    void initialize() noexcept {
        new (&mutex) interprocess_mutex();
        process_count.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < MAX_PROCESSES; ++i) {
            new (&processes[i]) ProcessInfo();
        }
    }
    
    /**
     * @brief 注册进程
     * @return 进程槽位索引，-1 表示失败
     */
    int32_t register_process(const char* name) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 查找空闲槽位
        for (size_t i = 0; i < MAX_PROCESSES; ++i) {
            if (processes[i].process_id == INVALID_PROCESS_ID) {
                ProcessId pid = static_cast<ProcessId>(i + 1);
                processes[i].process_id = pid;
                processes[i].state = ProcessState::STARTING;
                processes[i].start_time_ns = Timestamp::now().to_nanoseconds();
                processes[i].update_heartbeat();
                
                if (name) {
                    strncpy(processes[i].process_name, name, sizeof(processes[i].process_name) - 1);
                }
                
#ifdef MQSHM_PLATFORM_WINDOWS
                processes[i].native_pid = GetCurrentProcessId();
#else
                processes[i].native_pid = getpid();
#endif
                
                process_count.fetch_add(1, std::memory_order_relaxed);
                return static_cast<int32_t>(i);
            }
        }
        
        return -1;  // 无可用槽位
    }
    
    /**
     * @brief 注销进程
     */
    void unregister_process(int32_t slot) noexcept {
        if (slot < 0 || slot >= static_cast<int32_t>(MAX_PROCESSES)) {
            return;
        }
        
        scoped_lock<interprocess_mutex> lock(mutex);
        
        if (processes[slot].process_id != INVALID_PROCESS_ID) {
            processes[slot].process_id = INVALID_PROCESS_ID;
            processes[slot].state = ProcessState::STOPPED;
            process_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    
    /**
     * @brief 获取进程数量
     */
    uint32_t get_process_count() const noexcept {
        return process_count.load(std::memory_order_acquire);
    }
};

// ===== Block 注册表 =====

/**
 * @brief Block 信息（存储在共享内存）
 */
struct BlockInfo {
    BlockId block_id;                    ///< Block ID
    BlockType type;                      ///< Block 类型
    BlockState state;                    ///< Block 状态
    ProcessId owner_process;             ///< 所属进程 ID
    char block_name[64];                 ///< Block 名称
    TimestampNs create_time_ns;          ///< 创建时间
    
    // 端口信息
    uint32_t input_port_count;           ///< 输入端口数量
    uint32_t output_port_count;          ///< 输出端口数量
    PortId input_ports[MAX_PORTS_PER_BLOCK];   ///< 输入端口 ID 数组
    PortId output_ports[MAX_PORTS_PER_BLOCK];  ///< 输出端口 ID 数组
    
    BlockInfo() noexcept
        : block_id(INVALID_BLOCK_ID)
        , type(BlockType::PROCESSING)
        , state(BlockState::CREATED)
        , owner_process(INVALID_PROCESS_ID)
        , create_time_ns(0)
        , input_port_count(0)
        , output_port_count(0)
    {
        memset(block_name, 0, sizeof(block_name));
        memset(input_ports, 0, sizeof(input_ports));
        memset(output_ports, 0, sizeof(output_ports));
    }
} MQSHM_PACKED;

/**
 * @brief Block 注册表
 */
struct BlockRegistry {
    interprocess_mutex mutex;
    std::atomic<uint32_t> block_count;
    std::atomic<uint32_t> next_block_id;
    BlockInfo blocks[MAX_BLOCKS];
    
    void initialize() noexcept {
        new (&mutex) interprocess_mutex();
        block_count.store(0, std::memory_order_relaxed);
        next_block_id.store(1, std::memory_order_relaxed);
        for (size_t i = 0; i < MAX_BLOCKS; ++i) {
            new (&blocks[i]) BlockInfo();
        }
    }
    
    /**
     * @brief 注册 Block
     * @return Block ID，0 表示失败
     */
    BlockId register_block(const char* name, BlockType type, ProcessId process) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 查找空闲槽位
        for (size_t i = 0; i < MAX_BLOCKS; ++i) {
            if (blocks[i].block_id == INVALID_BLOCK_ID) {
                BlockId bid = next_block_id.fetch_add(1, std::memory_order_relaxed);
                blocks[i].block_id = bid;
                blocks[i].type = type;
                blocks[i].state = BlockState::REGISTERED;
                blocks[i].owner_process = process;
                blocks[i].create_time_ns = Timestamp::now().to_nanoseconds();
                
                if (name) {
                    strncpy(blocks[i].block_name, name, sizeof(blocks[i].block_name) - 1);
                }
                
                block_count.fetch_add(1, std::memory_order_relaxed);
                return bid;
            }
        }
        
        return INVALID_BLOCK_ID;
    }
    
    /**
     * @brief 注销 Block
     */
    void unregister_block(BlockId block_id) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        for (size_t i = 0; i < MAX_BLOCKS; ++i) {
            if (blocks[i].block_id == block_id) {
                blocks[i].block_id = INVALID_BLOCK_ID;
                blocks[i].state = BlockState::STOPPED;
                block_count.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
        }
    }
    
    /**
     * @brief 根据 Block ID 查找槽位
     */
    int32_t find_slot_by_id(BlockId block_id) const noexcept {
        for (size_t i = 0; i < MAX_BLOCKS; ++i) {
            if (blocks[i].block_id == block_id) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }
};

// ===== 连接注册表 =====

/**
 * @brief 连接信息（存储在共享内存）
 */
struct ConnectionInfo {
    ConnectionId connection_id;          ///< 连接 ID
    BlockId src_block;                   ///< 源 Block ID
    PortId src_port;                     ///< 源端口 ID
    BlockId dst_block;                   ///< 目标 Block ID
    PortId dst_port;                     ///< 目标端口 ID
    bool active;                         ///< 是否激活
    TimestampNs create_time_ns;          ///< 创建时间
    
    ConnectionInfo() noexcept
        : connection_id(INVALID_CONNECTION_ID)
        , src_block(INVALID_BLOCK_ID)
        , src_port(INVALID_PORT_ID)
        , dst_block(INVALID_BLOCK_ID)
        , dst_port(INVALID_PORT_ID)
        , active(false)
        , create_time_ns(0)
    {}
} MQSHM_PACKED;

/**
 * @brief 连接注册表
 */
struct ConnectionRegistry {
    interprocess_mutex mutex;
    std::atomic<uint32_t> connection_count;
    std::atomic<uint64_t> next_connection_id;
    ConnectionInfo connections[MAX_CONNECTIONS];
    
    void initialize() noexcept {
        new (&mutex) interprocess_mutex();
        connection_count.store(0, std::memory_order_relaxed);
        next_connection_id.store(1, std::memory_order_relaxed);
        for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
            new (&connections[i]) ConnectionInfo();
        }
    }
    
    /**
     * @brief 创建连接
     */
    ConnectionId create_connection(BlockId src_block, PortId src_port,
                                    BlockId dst_block, PortId dst_port) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 查找空闲槽位
        for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
            if (connections[i].connection_id == INVALID_CONNECTION_ID) {
                ConnectionId cid = next_connection_id.fetch_add(1, std::memory_order_relaxed);
                connections[i].connection_id = cid;
                connections[i].src_block = src_block;
                connections[i].src_port = src_port;
                connections[i].dst_block = dst_block;
                connections[i].dst_port = dst_port;
                connections[i].active = true;
                connections[i].create_time_ns = Timestamp::now().to_nanoseconds();
                
                connection_count.fetch_add(1, std::memory_order_relaxed);
                return cid;
            }
        }
        
        return INVALID_CONNECTION_ID;
    }
    
    /**
     * @brief 删除连接
     */
    void delete_connection(ConnectionId connection_id) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
            if (connections[i].connection_id == connection_id) {
                connections[i].connection_id = INVALID_CONNECTION_ID;
                connections[i].active = false;
                connection_count.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
        }
    }
};

// ===== Buffer Pool 注册表 =====

/**
 * @brief Buffer Pool 信息（存储在共享内存）
 */
struct BufferPoolInfo {
    PoolId pool_id;                      ///< Pool ID
    size_t block_size;                   ///< 每个块的大小
    size_t block_count;                  ///< 块数量
    char shm_name[64];                   ///< 共享内存名称
    bool active;                         ///< 是否激活
    
    BufferPoolInfo() noexcept
        : pool_id(INVALID_POOL_ID)
        , block_size(0)
        , block_count(0)
        , active(false)
    {
        memset(shm_name, 0, sizeof(shm_name));
    }
} MQSHM_PACKED;

/**
 * @brief Buffer Pool 注册表
 */
struct BufferPoolRegistry {
    interprocess_mutex mutex;
    std::atomic<uint32_t> pool_count;
    BufferPoolInfo pools[MAX_BUFFER_POOLS];
    
    void initialize() noexcept {
        new (&mutex) interprocess_mutex();
        pool_count.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
            new (&pools[i]) BufferPoolInfo();
        }
    }
    
    /**
     * @brief 注册 Buffer Pool
     */
    PoolId register_pool(size_t block_size, size_t block_count, const char* shm_name) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        // 查找空闲槽位
        for (size_t i = 0; i < MAX_BUFFER_POOLS; ++i) {
            if (pools[i].pool_id == INVALID_POOL_ID) {
                pools[i].pool_id = static_cast<PoolId>(i);
                pools[i].block_size = block_size;
                pools[i].block_count = block_count;
                pools[i].active = true;
                
                if (shm_name) {
                    strncpy(pools[i].shm_name, shm_name, sizeof(pools[i].shm_name) - 1);
                    pools[i].shm_name[sizeof(pools[i].shm_name) - 1] = '\0';  // 确保 null terminator
                }
                
                pool_count.fetch_add(1, std::memory_order_relaxed);
                return pools[i].pool_id;
            }
        }
        
        return INVALID_POOL_ID;
    }
    
    /**
     * @brief 注销 Buffer Pool
     */
    void unregister_pool(PoolId pool_id) noexcept {
        scoped_lock<interprocess_mutex> lock(mutex);
        
        if (pool_id < MAX_BUFFER_POOLS && pools[pool_id].pool_id == pool_id) {
            pools[pool_id].pool_id = INVALID_POOL_ID;
            pools[pool_id].active = false;
            pool_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }
};

// ===== 全局注册表 =====

/**
 * @brief 全局注册表头部
 */
struct GlobalRegistryHeader {
    uint32_t magic_number;               ///< 魔数（用于验证）
    uint32_t version;                    ///< 版本号
    size_t total_size;                   ///< 总大小
    TimestampNs create_time_ns;          ///< 创建时间
    std::atomic<bool> initialized;       ///< 是否已初始化
    
    GlobalRegistryHeader() noexcept
        : magic_number(SHM_MAGIC_NUMBER)
        , version(FRAMEWORK_VERSION)
        , total_size(0)
        , create_time_ns(0)
        , initialized(false)
    {}
    
    bool is_valid() const noexcept {
        return magic_number == SHM_MAGIC_NUMBER &&
               version == FRAMEWORK_VERSION &&
               initialized.load(std::memory_order_acquire);
    }
} MQSHM_PACKED;

/**
 * @brief 全局注册表（存储在共享内存）
 */
struct GlobalRegistry {
    GlobalRegistryHeader header;
    ProcessRegistry process_registry;
    BlockRegistry block_registry;
    ConnectionRegistry connection_registry;
    BufferPoolRegistry buffer_pool_registry;
    BufferMetadataTable buffer_metadata_table;
    
    /**
     * @brief 初始化全局注册表（只由第一个进程调用）
     */
    void initialize() noexcept {
        new (&header) GlobalRegistryHeader();
        header.total_size = sizeof(GlobalRegistry);
        header.create_time_ns = Timestamp::now().to_nanoseconds();
        
        process_registry.initialize();
        block_registry.initialize();
        connection_registry.initialize();
        buffer_pool_registry.initialize();
        buffer_metadata_table.initialize();
        
        header.initialized.store(true, std::memory_order_release);
    }
    
    /**
     * @brief 检查是否有效
     */
    bool is_valid() const noexcept {
        return header.is_valid();
    }
};

}  // namespace multiqueue
