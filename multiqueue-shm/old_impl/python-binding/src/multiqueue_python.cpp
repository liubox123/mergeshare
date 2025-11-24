/**
 * @file multiqueue_python.cpp
 * @brief Python 绑定实现
 * 
 * 使用 pybind11 将 C++ RingQueue 暴露给 Python
 */

#include <multiqueue/ring_queue.hpp>
#include <multiqueue/timestamp_sync.hpp>
#include <multiqueue/config.hpp>
#include <multiqueue/metadata.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

namespace py = pybind11;
using namespace multiqueue;

/**
 * @brief Python 模块定义
 */
PYBIND11_MODULE(multiqueue_shm, m) {
    m.doc() = "MultiQueue-SHM: High-performance shared memory queue library";
    
    // BlockingMode 枚举
    py::enum_<BlockingMode>(m, "BlockingMode")
        .value("BLOCKING", BlockingMode::BLOCKING)
        .value("NON_BLOCKING", BlockingMode::NON_BLOCKING)
        .export_values();
    
    // LogLevel 枚举
    py::enum_<LogLevel>(m, "LogLevel")
        .value("TRACE", LogLevel::TRACE)
        .value("DEBUG", LogLevel::DEBUG)
        .value("INFO", LogLevel::INFO)
        .value("WARN", LogLevel::WARN)
        .value("ERROR", LogLevel::ERROR)
        .value("FATAL", LogLevel::FATAL)
        .export_values();
    
    // QueueConfig 类
    py::class_<QueueConfig>(m, "QueueConfig")
        .def(py::init<size_t>(), py::arg("capacity") = 1024)
        .def_readwrite("capacity", &QueueConfig::capacity)
        .def_readwrite("queue_name", &QueueConfig::queue_name)
        .def_readwrite("blocking_mode", &QueueConfig::blocking_mode)
        .def_readwrite("timeout_ms", &QueueConfig::timeout_ms)
        .def_readwrite("has_timestamp", &QueueConfig::has_timestamp)
        .def_readwrite("user_metadata", &QueueConfig::user_metadata)
        .def_readwrite("enable_async", &QueueConfig::enable_async)
        .def("is_valid", &QueueConfig::is_valid)
        .def("is_power_of_two", &QueueConfig::is_power_of_two)
        .def("round_up_capacity_to_power_of_two", &QueueConfig::round_up_capacity_to_power_of_two);
    
    // QueueStats 类
    py::class_<QueueStats>(m, "QueueStats")
        .def(py::init<>())
        .def_readonly("total_pushed", &QueueStats::total_pushed)
        .def_readonly("total_popped", &QueueStats::total_popped)
        .def_readonly("overwrite_count", &QueueStats::overwrite_count)
        .def_readonly("producer_count", &QueueStats::producer_count)
        .def_readonly("consumer_count", &QueueStats::consumer_count)
        .def_readonly("current_size", &QueueStats::current_size)
        .def_readonly("capacity", &QueueStats::capacity)
        .def_readonly("is_closed", &QueueStats::is_closed);
    
    // RingQueue<int> 特化
    py::class_<RingQueue<int>>(m, "RingQueueInt")
        .def(py::init<const std::string&, const QueueConfig&>(),
             py::arg("name"), py::arg("config"))
        .def("push", 
             [](RingQueue<int>& self, int data, uint64_t timestamp) {
                 return self.try_push(data, timestamp);  // 使用 try_push 避免阻塞
             },
             py::arg("data"), py::arg("timestamp") = 0)
        .def("pop",
             [](RingQueue<int>& self) -> py::tuple {
                 int data;
                 uint64_t timestamp;
                 bool success = self.try_pop(data, &timestamp);  // 使用 try_pop 避免阻塞
                 if (success) {
                     return py::make_tuple(data, timestamp);
                 }
                 return py::make_tuple(py::none(), py::none());
             })
        .def("push_blocking",
             [](RingQueue<int>& self, int data, uint64_t timestamp, uint32_t timeout_ms) {
                 return self.push_with_timeout(data, timeout_ms, timestamp);
             },
             py::arg("data"), py::arg("timestamp") = 0, py::arg("timeout_ms") = 1000,
             "Blocking push with timeout")
        .def("pop_blocking",
             [](RingQueue<int>& self, uint32_t timeout_ms) -> py::tuple {
                 int data;
                 uint64_t timestamp;
                 bool success = self.pop_with_timeout(data, timeout_ms, &timestamp);
                 if (success) {
                     return py::make_tuple(data, timestamp);
                 }
                 return py::make_tuple(py::none(), py::none());
             },
             py::arg("timeout_ms") = 1000,
             "Blocking pop with timeout")
        .def("try_push", &RingQueue<int>::try_push,
             py::arg("data"), py::arg("timestamp") = 0)
        .def("try_pop",
             [](RingQueue<int>& self) -> py::tuple {
                 int data;
                 uint64_t timestamp;
                 bool success = self.try_pop(data, &timestamp);
                 if (success) {
                     return py::make_tuple(data, timestamp);
                 }
                 return py::make_tuple(py::none(), py::none());
             })
        .def("size", &RingQueue<int>::size)
        .def("empty", &RingQueue<int>::empty)
        .def("full", &RingQueue<int>::full)
        .def("capacity", &RingQueue<int>::capacity)
        .def("get_stats", &RingQueue<int>::get_stats)
        .def("close", &RingQueue<int>::close)
        .def("is_closed", &RingQueue<int>::is_closed)
        .def("__len__", &RingQueue<int>::size)
        .def("__bool__", [](const RingQueue<int>& self) { return !self.empty(); });
    
    // RingQueue<double> 特化
    py::class_<RingQueue<double>>(m, "RingQueueDouble")
        .def(py::init<const std::string&, const QueueConfig&>(),
             py::arg("name"), py::arg("config"))
        .def("push", 
             [](RingQueue<double>& self, double data, uint64_t timestamp) {
                 return self.try_push(data, timestamp);  // 使用 try_push 避免阻塞
             },
             py::arg("data"), py::arg("timestamp") = 0)
        .def("pop",
             [](RingQueue<double>& self) -> py::tuple {
                 double data;
                 uint64_t timestamp;
                 bool success = self.try_pop(data, &timestamp);  // 使用 try_pop 避免阻塞
                 if (success) {
                     return py::make_tuple(data, timestamp);
                 }
                 return py::make_tuple(py::none(), py::none());
             })
        .def("push_blocking",
             [](RingQueue<double>& self, double data, uint64_t timestamp, uint32_t timeout_ms) {
                 return self.push_with_timeout(data, timeout_ms, timestamp);
             },
             py::arg("data"), py::arg("timestamp") = 0, py::arg("timeout_ms") = 1000)
        .def("pop_blocking",
             [](RingQueue<double>& self, uint32_t timeout_ms) -> py::tuple {
                 double data;
                 uint64_t timestamp;
                 bool success = self.pop_with_timeout(data, timeout_ms, &timestamp);
                 if (success) {
                     return py::make_tuple(data, timestamp);
                 }
                 return py::make_tuple(py::none(), py::none());
             },
             py::arg("timeout_ms") = 1000)
        .def("size", &RingQueue<double>::size)
        .def("empty", &RingQueue<double>::empty)
        .def("capacity", &RingQueue<double>::capacity)
        .def("get_stats", &RingQueue<double>::get_stats)
        .def("close", &RingQueue<double>::close)
        .def("__len__", &RingQueue<double>::size);
    
    // TimestampSynchronizer 工具类
    m.def("timestamp_now", &TimestampSynchronizer::now,
          "Get current timestamp in nanoseconds");
    m.def("timestamp_now_micros", &TimestampSynchronizer::now_micros,
          "Get current timestamp in microseconds");
    m.def("timestamp_now_millis", &TimestampSynchronizer::now_millis,
          "Get current timestamp in milliseconds");
    
    // 版本信息
    m.attr("__version__") = "0.1.0";
    m.attr("QUEUE_VERSION") = QUEUE_VERSION;
}

