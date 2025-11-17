#include "shared_ring_queue.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include <iostream>
// #include <fstream>
namespace py = pybind11;

PYBIND11_MODULE(shared_ring_queue, m) {
    py::class_<SharedRingQueueProducer>(m, "SharedRingQueueProducer")
        .def(py::init<const std::string&, uint32_t, uint32_t, uint32_t, const std::string&>(),
             py::arg("shm_name"), py::arg("queue_len"), py::arg("data_block_size"), py::arg("total_refs"), py::arg("metadata"))
        .def("push", [](SharedRingQueueProducer& self, py::bytes data) {
            std::string s = data;
            return self.push(s.data(), s.size());
        })
        .def_property_readonly("metadata", &SharedRingQueueProducer::metadata)
        .def_property_readonly("node_count", &SharedRingQueueProducer::node_count)
        .def_property_readonly("node_size", &SharedRingQueueProducer::node_size)
        .def_property_readonly("shm_size", &SharedRingQueueProducer::shm_size);

    py::class_<SharedRingQueueConsumer>(m, "SharedRingQueueConsumer")
        .def(py::init<const std::string&, uint32_t, uint32_t>(),
             py::arg("shm_name"), py::arg("queue_len"), py::arg("data_block_size"))
        .def("pop", [](SharedRingQueueConsumer& self) {
            std::vector<char> buf(self.node_size());
            uint32_t sz = 0;
            int ret = self.pop(buf.data(), sz);
            if (ret && sz > 0) 
            {
                std::cout <<"sz :" <<sz<<std::endl;
                return py::bytes(buf.data(), sz);
            }
            std::cout <<"sz :" <<sz<<std::endl;
            return py::bytes();
        })
        .def_property_readonly("metadata", &SharedRingQueueConsumer::metadata)
        .def_property_readonly("node_count", &SharedRingQueueConsumer::node_count)
        .def_property_readonly("node_size", &SharedRingQueueConsumer::node_size)
        .def_property_readonly("shm_size", &SharedRingQueueConsumer::shm_size)
        .def_property_readonly("consumer_id", &SharedRingQueueConsumer::consumer_id);

    py::class_<SharedMemProcessor>(m, "SharedMemProcessor")
        .def(py::init<const std::string&, uint32_t, uint32_t,
                      const std::string&, uint32_t, uint32_t,
                      uint32_t, const std::string&, size_t, int>(),
             py::arg("in_shm"), py::arg("in_queue_len"), py::arg("in_block_size"),
             py::arg("out_shm"), py::arg("out_queue_len"), py::arg("out_block_size"),
             py::arg("total_refs"), py::arg("metadata"),
             py::arg("batch_size") = 1, py::arg("timeout_ms") = 10)
        .def("register_callback", &SharedMemProcessor::register_callback)
        .def("start", &SharedMemProcessor::start)
        .def("stop", &SharedMemProcessor::stop)
        .def("push_to_output", &SharedMemProcessor::push_to_output);
} 