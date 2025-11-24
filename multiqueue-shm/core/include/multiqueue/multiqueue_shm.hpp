/**
 * @file multiqueue_shm.hpp
 * @brief MultiQueue-SHM 主头文件
 * 
 * 包含所有必要的头文件，提供完整的API
 */

#pragma once

// 基础类型
#include "types.hpp"

// 时间戳
#include "timestamp.hpp"

// Buffer 相关
#include "buffer_metadata.hpp"
#include "buffer_pool.hpp"
#include "buffer_allocator.hpp"
#include "buffer_ptr.hpp"

// 全局注册表
#include "global_registry.hpp"

// 端口队列
#include "port_queue.hpp"

// Block 框架
#include "port.hpp"
#include "block.hpp"
#include "blocks.hpp"

// Runtime 系统
#include "message.hpp"
#include "msgbus.hpp"
#include "scheduler.hpp"
#include "runtime.hpp"

/**
 * @namespace multiqueue
 * @brief MultiQueue-SHM 命名空间
 * 
 * 提供多进程共享内存流处理框架的所有组件
 */
namespace multiqueue {

/**
 * @brief 获取框架版本
 */
inline uint32_t get_version() {
    return FRAMEWORK_VERSION;
}

/**
 * @brief 获取版本字符串
 */
inline const char* get_version_string() {
    return "2.0.0";
}

}  // namespace multiqueue

