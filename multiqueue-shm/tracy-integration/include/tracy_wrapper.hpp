#pragma once

/**
 * @file tracy_wrapper.hpp
 * @brief Tracy Profiler 包装器
 * 
 * 提供便捷的宏定义，简化 Tracy Profiler 的使用
 * 可以通过编译选项轻松开关性能监控
 */

#ifdef TRACY_ENABLE
    // Tracy 已启用，包含 Tracy 头文件
    #include <tracy/Tracy.hpp>
    
    // 函数性能监控
    #define MQ_TRACE_FUNC              ZoneScoped
    #define MQ_TRACE_FUNC_N(name)      ZoneScopedN(name)
    
    // 作用域性能监控
    #define MQ_TRACE_SCOPE(name)       ZoneScopedN(name)
    
    // 绘制性能图表
    #define MQ_TRACE_PLOT(name, val)   TracyPlot(name, val)
    
    // 标记帧
    #define MQ_TRACE_FRAME_MARK        FrameMark
    
    // 消息日志
    #define MQ_TRACE_MESSAGE(msg, len) TracyMessage(msg, len)
    #define MQ_TRACE_MESSAGE_L(msg)    TracyMessageL(msg)
    
    // 内存分配跟踪
    #define MQ_TRACE_ALLOC(ptr, size)  TracyAlloc(ptr, size)
    #define MQ_TRACE_FREE(ptr)         TracyFree(ptr)
    
    // 锁性能监控
    #define MQ_TRACE_LOCKABLE(type, name) TracyLockable(type, name)
    #define MQ_TRACE_LOCK_MARK(lock)      LockMark(lock)
    
#else
    // Tracy 未启用，定义为空宏
    #define MQ_TRACE_FUNC
    #define MQ_TRACE_FUNC_N(name)
    #define MQ_TRACE_SCOPE(name)
    #define MQ_TRACE_PLOT(name, val)
    #define MQ_TRACE_FRAME_MARK
    #define MQ_TRACE_MESSAGE(msg, len)
    #define MQ_TRACE_MESSAGE_L(msg)
    #define MQ_TRACE_ALLOC(ptr, size)
    #define MQ_TRACE_FREE(ptr)
    #define MQ_TRACE_LOCKABLE(type, name) type name
    #define MQ_TRACE_LOCK_MARK(lock)
#endif

/**
 * @brief Tracy 性能监控包装器
 * 
 * 提供更高级的接口和便捷函数
 */
namespace multiqueue {
namespace profiler {

/**
 * @brief 性能监控器类
 */
class Profiler {
public:
    /**
     * @brief 标记帧（用于帧率分析）
     */
    static void mark_frame() {
        MQ_TRACE_FRAME_MARK;
    }
    
    /**
     * @brief 发送消息到 Tracy
     * @param message 消息内容
     */
    static void message(const char* message) {
        MQ_TRACE_MESSAGE_L(message);
    }
    
    /**
     * @brief 绘制性能图表
     * @param name 图表名称
     * @param value 数值
     */
    static void plot(const char* name, double value) {
        MQ_TRACE_PLOT(name, value);
    }
    
    /**
     * @brief 绘制整数图表
     * @param name 图表名称
     * @param value 数值
     */
    static void plot_int(const char* name, int64_t value) {
        MQ_TRACE_PLOT(name, static_cast<double>(value));
    }
    
    /**
     * @brief 标记内存分配
     * @param ptr 指针
     * @param size 大小
     */
    static void mark_alloc(void* ptr, size_t size) {
        MQ_TRACE_ALLOC(ptr, size);
    }
    
    /**
     * @brief 标记内存释放
     * @param ptr 指针
     */
    static void mark_free(void* ptr) {
        MQ_TRACE_FREE(ptr);
    }
};

/**
 * @brief RAII 作用域性能监控
 * 
 * 使用示例：
 * ```cpp
 * void my_function() {
 *     ScopedProfiler profiler("my_function");
 *     // ... 函数代码 ...
 * }
 * ```
 */
class ScopedProfiler {
public:
    /**
     * @brief 构造函数，开始监控
     * @param name 监控区域名称
     */
    explicit ScopedProfiler(const char* name) {
        MQ_TRACE_SCOPE(name);
    }
    
    /**
     * @brief 析构函数，结束监控
     */
    ~ScopedProfiler() = default;
    
private:
    // 禁止拷贝和移动
    ScopedProfiler(const ScopedProfiler&) = delete;
    ScopedProfiler& operator=(const ScopedProfiler&) = delete;
    ScopedProfiler(ScopedProfiler&&) = delete;
    ScopedProfiler& operator=(ScopedProfiler&&) = delete;
};

} // namespace profiler
} // namespace multiqueue

/**
 * @brief 使用说明
 * 
 * ## 启用 Tracy Profiler
 * 
 * 编译时添加 `-DTRACY_ENABLE` 宏定义：
 * ```bash
 * cmake .. -DENABLE_TRACY=ON
 * cmake --build .
 * ```
 * 
 * ## 基本使用
 * 
 * ### 1. 函数性能监控
 * ```cpp
 * void my_function() {
 *     MQ_TRACE_FUNC;  // 自动使用函数名
 *     // ... 函数代码 ...
 * }
 * ```
 * 
 * ### 2. 自定义名称监控
 * ```cpp
 * void complex_function() {
 *     MQ_TRACE_FUNC_N("ComplexOperation");
 *     // ... 函数代码 ...
 * }
 * ```
 * 
 * ### 3. 作用域监控
 * ```cpp
 * void my_function() {
 *     // 初始化
 *     {
 *         MQ_TRACE_SCOPE("Initialization");
 *         // 初始化代码
 *     }
 *     
 *     // 处理
 *     {
 *         MQ_TRACE_SCOPE("Processing");
 *         // 处理代码
 *     }
 * }
 * ```
 * 
 * ### 4. 性能图表
 * ```cpp
 * void update_queue() {
 *     size_t queue_size = get_queue_size();
 *     MQ_TRACE_PLOT("QueueSize", queue_size);
 * }
 * ```
 * 
 * ### 5. 使用 C++ 包装器
 * ```cpp
 * #include <tracy_wrapper.hpp>
 * 
 * void my_function() {
 *     using namespace multiqueue::profiler;
 *     
 *     ScopedProfiler profiler("MyFunction");
 *     
 *     // 绘制图表
 *     Profiler::plot("Throughput", 1000000.0);
 *     
 *     // 发送消息
 *     Profiler::message("Processing completed");
 * }
 * ```
 * 
 * ## Tracy Server 连接
 * 
 * 1. 下载 Tracy Profiler：https://github.com/wolfpld/tracy/releases
 * 2. 运行 Tracy Server（GUI 工具）
 * 3. 运行你的程序（已启用 Tracy）
 * 4. Tracy 会自动连接并显示性能数据
 * 
 * ## 性能影响
 * 
 * - Tracy 的开销非常低（通常 < 1%）
 * - 可以在生产环境中启用
 * - Release 构建中可以通过编译选项完全移除
 */


