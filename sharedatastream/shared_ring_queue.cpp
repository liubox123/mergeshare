#include "shared_ring_queue.hpp"
#include <thread>
#include <cstring>
#include <algorithm>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <process.h> // For _getpid
#else
#include <sys/types.h>
#include <unistd.h> // For getpid
#include <signal.h>
#include <cerrno>
#endif

namespace {
    // 辅助函数：获取当前进程ID
    uint64_t get_current_pid() {
#ifdef _WIN32
        return static_cast<uint64_t>(GetCurrentProcessId());
#else
        return static_cast<uint64_t>(getpid());
#endif
    }

    // 辅助函数：检查进程是否仍在运行
    bool is_process_running(uint64_t pid) {
        if (pid == 0) return false;
#ifdef _WIN32
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (process == NULL) {
            return false;
        }
        DWORD ret = WaitForSingleObject(process, 0);
        CloseHandle(process);
        return ret == WAIT_TIMEOUT;
#else
        // kill(pid, 0) 是检查进程是否存在的标准 POSIX 方法。
        // 它不发送信号，只进行错误检查。
        // 如果返回0，说明进程存在。
        // 如果返回-1且errno为EPERM，说明进程存在但我们没有权限发送信号（这也意味着它存在）。
        // 如果返回-1且errno为ESRCH，说明进程不存在。
        if (kill(pid, 0) == 0) {
            return true;
        } else {
            return errno == EPERM;
        }
#endif
    }
}

using namespace boost::interprocess;
// 检查字符串是否为有效的 UTF-8
inline bool is_valid_utf8(const std::string& str) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str.c_str());
    size_t len = str.length();

    for (size_t i = 0; i < len; ) {
        unsigned char byte = bytes[i];

        if (byte < 0x80) {
            // ASCII 字符
            i++;
        }
        else if ((byte & 0xE0) == 0xC0) {
            // 2字节 UTF-8 字符
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        }
        else if ((byte & 0xF0) == 0xE0) {
            // 3字节 UTF-8 字符
            if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        }
        else if ((byte & 0xF8) == 0xF0) {
            // 4字节 UTF-8 字符
            if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        }
        else {
            return false;
        }
    }
    return true;
}

// 尝试从常见编码转换为 UTF-8
inline std::string convert_to_utf8(const std::string& str) {
    // 这里可以实现从常见编码（如 GBK、GB2312、Latin-1 等）转换为 UTF-8
    // 由于 C++ 标准库不直接支持编码转换，这里提供一个基础实现

    // 检查是否可能是 Latin-1 编码
    bool is_latin1 = true;
    for (unsigned char c : str) {
        if (c > 0x7F && c < 0xA0) {
            is_latin1 = false;
            break;
        }
    }

    if (is_latin1) {
        // 简单的 Latin-1 到 UTF-8 转换
        std::string utf8;
        for (unsigned char c : str) {
            if (c < 0x80) {
                utf8 += c;
            }
            else {
                // 转换为 UTF-8 编码
                utf8 += static_cast<char>(0xC0 | (c >> 6));
                utf8 += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return utf8;
    }

    // 如果无法识别编码，返回空字符串
    return "";
}
// 辅助函数：检测并确保字符串使用 UTF-8 编码
inline std::string ensure_utf8_string(const pybind11::bytes& data) {
    std::string s = data;
    
    // 检查是否已经是有效的 UTF-8
    if (is_valid_utf8(s)) {
        return s;
    }
    
    // 如果不是有效的 UTF-8，尝试从常见编码转换
    std::string utf8_str = convert_to_utf8(s);
    if (!utf8_str.empty()) {
        return utf8_str;
    }
    
    // 如果转换失败，返回原始字符串并记录警告
    std::cerr << "Warning: Failed to convert string to UTF-8, using original data" << std::endl;
    return s;
}



// SharedRingQueueRaw
SharedRingQueueRaw::SharedRingQueueRaw(void* shm_base, size_t shm_size, bool owner,
                       uint32_t node_count, uint32_t node_size, uint32_t total_refs, const std::string& metadata)
    : base_(reinterpret_cast<uint8_t*>(shm_base)), shm_size_(shm_size), node_count_(node_count), node_size_(node_size) {
    header_ = reinterpret_cast<RingQueueHeader*>(base_);
    if (owner) {
        std::memset(base_, 0, shm_size_);
        new (header_) RingQueueHeader();
        header_->node_count = node_count_;
        header_->node_size = node_size_;
        header_->total_refs = total_refs;
        header_->shm_size = shm_size_;
        header_->consumer_count = 0;
        header_->producer_pid.store(0);
        std::memset(header_->metadata, 0, METADATA_SIZE);
        // 确保 metadata 是有效的 UTF-8 编码
        try {
            if (!metadata.empty()) {
                std::string utf8_metadata = ensure_utf8_string(pybind11::bytes(metadata));
            
                #ifdef _WIN32
                std::memcpy(header_->metadata, utf8_metadata.data(), min(utf8_metadata.size(), (size_t)METADATA_SIZE));
                #else
                std::memcpy(header_->metadata, utf8_metadata.data(), std::min(utf8_metadata.size(), (size_t)METADATA_SIZE));
                #endif
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in SharedRingQueueRaw constructor: " << e.what() << std::endl;
            std::cerr << "metadata length: " << metadata.length() << std::endl;
            std::cerr << "metadata content (hex): ";
    #ifdef _WIN32
            for (size_t i = 0; i < min(metadata.length(), size_t(20)); ++i) {
#else
            for (size_t i = 0; i < std::min(metadata.length(), size_t(20)); ++i) {
#endif
                std::cerr << std::hex << (unsigned char)metadata[i] << " ";
            }
            std::cerr << std::endl;
            // Use empty string as fallback
            #ifdef _WIN32
            std::memcpy(header_->metadata, "", min(1, (size_t)METADATA_SIZE));
            #else
            std::memcpy(header_->metadata, "", std::min(1, (int)(size_t)METADATA_SIZE));
            #endif
        }
        for (uint32_t i = 0; i < node_count_; ++i) {
            Node* node = node_at(i);
            // new (&node->node_mutex) interprocess_mutex();
            // node->ref_count = 0;
            // node->total_refs = total_refs;
            node->next_offset = offset_of((i + 1) % node_count_);
        }
        header_->tail_offset.store(offset_of(0));
        for (size_t i = 0; i < MAX_CONSUMER; ++i) header_->consumer_heads[i].store(offset_of(0));
        header_->next_consumer_id.store(0);
    }
}

uint32_t SharedRingQueueRaw::offset_of(uint32_t idx) const { return sizeof(RingQueueHeader) + idx * node_size_; }
Node* SharedRingQueueRaw::node_at(uint32_t idx) const {
    return reinterpret_cast<Node*>(base_ + sizeof(RingQueueHeader) + idx * node_size_);
}
Node* SharedRingQueueRaw::node_at_offset(uint32_t offset) const {
    if (offset < sizeof(RingQueueHeader) || offset >= shm_size_) return nullptr;
    return reinterpret_cast<Node*>(base_ + offset);
}
uint32_t SharedRingQueueRaw::index_of(uint32_t offset) const { return (offset - sizeof(RingQueueHeader)) / node_size_; }
RingQueueHeader* SharedRingQueueRaw::header() const { return header_; }
uint32_t SharedRingQueueRaw::node_count() const { return node_count_; }
uint32_t SharedRingQueueRaw::node_size() const { return node_size_; }
size_t SharedRingQueueRaw::shm_size() const { return shm_size_; }
std::string SharedRingQueueRaw::metadata() const { return std::string(header_->metadata, METADATA_SIZE); }

namespace {
    SharedRingQueueConsumer* g_consumer_for_signal = nullptr;
    SharedRingQueueProducer* g_producer_for_signal = nullptr;
    void handle_signal(int sig) {
        if (g_consumer_for_signal) {
            g_consumer_for_signal->unregister();
        }
        if (g_producer_for_signal) {
            delete g_producer_for_signal;
            g_producer_for_signal = nullptr;
        }
        std::_Exit(128 + sig);
    }
    void atexit_handler() {
        if (g_consumer_for_signal) {
            g_consumer_for_signal->unregister();
        }
        if (g_producer_for_signal) {
            delete g_producer_for_signal;
            g_producer_for_signal = nullptr;
        }
    }
    void thisterminate_handler() {
        if (g_consumer_for_signal) {
            g_consumer_for_signal->unregister();
        }
        if (g_producer_for_signal) {
            delete g_producer_for_signal;
            g_producer_for_signal = nullptr;
        }
        std::_Exit(1);
    }
}

// Producer
SharedRingQueueProducer::SharedRingQueueProducer(
    const std::string& shm_name, uint32_t node_count, uint32_t data_block_size, uint32_t total_refs, const std::string& metadata)
    : shm_name_(shm_name)
{
    bool shm_exists = true;
	size_t shm_size = sizeof(RingQueueHeader) + node_count * (sizeof(Node) + data_block_size);

    try {
        // 尝试只读打开以检查是否存在和所有权
#ifdef _WIN32
		// shm_name_ = std::string("Global\\") + shm_name_;
		windows_shared_memory test_shm(open_only, shm_name_.c_str(), read_write);
#else 
        shared_memory_object test_shm(open_only, shm_name_.c_str(), read_only);
#endif

        mapped_region test_region(test_shm, read_only);
        RingQueueHeader* header = static_cast<RingQueueHeader*>(test_region.get_address());
        uint64_t old_pid = header->producer_pid.load();

        if (is_process_running(old_pid)) {
            throw std::runtime_error("Shared memory '" + shm_name_ + "' is in use by producer PID " + std::to_string(old_pid));
        } else {
            // 旧的生产者进程已死，这是一个僵尸共享内存
            std::cerr << "Warning: Stale shared memory '" << shm_name_ << "' from dead producer PID " << old_pid << " found. Cleaning up." << std::endl;
        }
    } catch(const interprocess_exception&) {
        // 共享内存不存在，这是正常情况
        shm_exists = false;
    }

    // 如果是僵尸或不存在，都先执行删除，确保一个干净的开始
	permissions perms;
	perms.set_unrestricted();
#ifdef _WIN32
	shm_obj_ = std::make_unique<windows_shared_memory>(create_only, shm_name_.c_str(), read_write, shm_size);
#else
    shared_memory_object::remove(shm_name_.c_str());


    shm_obj_ = std::make_unique<shared_memory_object>(create_only, shm_name_.c_str(), read_write);
    shm_obj_->truncate(shm_size);
#endif

    region_ = std::make_unique<mapped_region>(*shm_obj_, read_write);
    void* base = region_->get_address();
    queue_ = std::make_unique<SharedRingQueueRaw>(
		base, shm_size, true, node_count, sizeof(Node) + data_block_size, total_refs, metadata
    );

    // 声明所有权
    queue_->header()->producer_pid.store(get_current_pid());
	std::cout << "set shared mem:" << shm_name_.c_str() << std::endl;
    g_producer_for_signal = this;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    #ifdef __linux__
    std::signal(SIGQUIT, handle_signal);
    #endif
    std::signal(SIGABRT, handle_signal);
    std::signal(SIGSEGV, handle_signal);
    std::atexit(atexit_handler);
    std::set_terminate(thisterminate_handler);
}

bool SharedRingQueueProducer::push(const void* data, uint32_t size) {
    if (size > queue_->node_size() - sizeof(Node) + 1) return false;
    auto* header = queue_->header();
    uint32_t tail = header->tail_offset.load(std::memory_order_acquire);
    Node* node = queue_->node_at_offset(tail);
    uint32_t next_tail = node->next_offset;
    // 检查所有active consumer_heads是否都等于next_tail，等于则不能推进tail_offset
    for (size_t i = 0; i < MAX_CONSUMER; ++i) {
        if (header->consumer_active[i].load(std::memory_order_acquire) && header->consumer_heads[i].load(std::memory_order_acquire) == next_tail) {
            // 推进tail_offset会追上消费者，不能写入
            printf("tail_offset: %d, next_tail: %d, consumer_id: %d\n", tail, next_tail, i);
            return false;
        }
    }
    node->data_size = size;
    std::memcpy(node->data, data, size);
    header->tail_offset.store(next_tail, std::memory_order_release);
    return true;
}
std::string SharedRingQueueProducer::metadata() const { return queue_->metadata(); }
uint32_t SharedRingQueueProducer::node_count() const { return queue_->node_count(); }
uint32_t SharedRingQueueProducer::node_size() const { return queue_->node_size(); }
size_t SharedRingQueueProducer::shm_size() const { return queue_->shm_size(); }

// Consumer
SharedRingQueueConsumer::SharedRingQueueConsumer(const std::string& shm_name, uint32_t node_count, uint32_t data_block_size, int consumer_id)
    : shm_name_(shm_name)
{
    bool is_producer_running_before = true;
    size_t shm_size = sizeof(RingQueueHeader) + node_count * (sizeof(Node) + data_block_size);
	// shm_name_ = std::string("Global\\") + shm_name_;
    while (true) {
        try {
			std::cout << "xxxxxx 1" << std::endl;
#ifdef _WIN32

			std::cout << "xxxxxx 2" << std::endl;
			shm_obj_ = std::make_unique<windows_shared_memory>(open_only, shm_name_.c_str(), read_write);
#else
            shm_obj_ = std::make_unique<shared_memory_object>(open_only, shm_name_.c_str(), read_write);
#endif
			std::cout << "xxxxxx 3" << std::endl;
            // 检查生产者进程是否存在
			std::cout << "xxxxxx 4" << std::endl;
			mapped_region region(*shm_obj_, read_write);
			std::cout << "xxxxxx 5" << std::endl;
            auto* header = static_cast<const RingQueueHeader*>(region.get_address());
			std::cout << "xxxxxx 6" << std::endl;
            uint64_t pid = header->producer_pid.load(std::memory_order_acquire);
			std::cout << "xxxxxx 7" << std::endl;
            if (pid != 0 && !is_process_running(pid)) {
                std::cerr << "[Consumer] Warning: Found stale shared memory from dead producer PID "
                          << pid << ". Waiting for it to be cleaned up..." << std::endl;
                shm_obj_.reset(); // 释放句柄
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                is_producer_running_before = false;
                continue; // 重新循环等待
            }
            // 共享内存有效，退出循环
            break;
		}
		catch (const interprocess_exception& ex) {
            // 共享内存不存在，是正常情况，等待生产者创建
			std::cout << "[Consumer] Shared memory '" << shm_name_ << "' not found. Waiting for producer...:error: " << ex.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            is_producer_running_before = false;
        }
    }
    // if (!is_producer_running_before) {
        // std::this_thread::sleep_for(std::chrono::seconds(10));
    // }
    // std::cout << "open shared memory: " << shm_name_ << std::endl;
    region_ = std::make_unique<mapped_region>(*shm_obj_, read_write);
    void* base = region_->get_address();
     std::cout << "open shared memory2: " << shm_name_ << std::endl;
    queue_ = std::make_unique<SharedRingQueueRaw>(
		base, shm_size, false, node_count, sizeof(Node) + data_block_size, 0, ""
    );
     std::cout << "open shared memory3: " << shm_name_ << std::endl;
    auto* header = queue_->header();
       std::cout << "open shared memory4: " << shm_name_ << std::endl;
    queue_->node_count_ = header->node_count;
    queue_->node_size_ = header->node_size;
    queue_->shm_size_ = header->shm_size;
           std::cout << "open shared memory5: " << shm_name_ << std::endl;
    if (consumer_id >= 0 && consumer_id < (int)MAX_CONSUMER) {
        consumer_id_ = consumer_id;
        header->consumer_active[consumer_id_].store(1);
        std::cout << "open shared memory6: " << shm_name_ << std::endl;
    } else {
        scoped_lock<interprocess_mutex> global_lock(header->global_mutex);
        int consumer_count = -1;
        for (size_t i = 0; i < MAX_CONSUMER; ++i) {
            if (!header->consumer_active[i].load()) {
                consumer_id_ = i;
                header->consumer_active[i].store(1);
                consumer_count = i;
                header->consumer_heads[consumer_id_].store(header->tail_offset.load());
                break;
            }
        }
        uint32_t tail_offset = header->tail_offset.load();
        uint32_t index = queue_->index_of(tail_offset);
        uint32_t prev_index = (index + queue_->node_count() - 1) % queue_->node_count();
        uint32_t prev_offset = queue_->offset_of(prev_index);
        header->consumer_heads[consumer_count].store(prev_offset);
    }
    registered_ = true;
    g_consumer_for_signal = this;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    #ifdef __linux__
    std::signal(SIGQUIT, handle_signal);
    #endif
    std::signal(SIGABRT, handle_signal);
    std::signal(SIGSEGV, handle_signal);
    std::atexit(atexit_handler);
    std::set_terminate(thisterminate_handler);
}

int SharedRingQueueConsumer::pop(void* data_buf, uint32_t& out_size) {
    auto* header = queue_->header();
    while (true) {
        uint32_t head = header->consumer_heads[consumer_id_].load(std::memory_order_acquire);
        uint32_t tail = header->tail_offset.load(std::memory_order_acquire);
        Node* node = queue_->node_at_offset(head);
        uint32_t next = node->next_offset;
        // 如果上一次读取偏移和当前一致，且下一个等于tail，说明没有新数据，阻塞
        if (last_head_ == head && next == tail) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }
        if (next != tail) {
            Node* next_node = queue_->node_at_offset(next);
            out_size = next_node->data_size;
            std::memcpy(data_buf, next_node->data, out_size);
            // 只有下一个节点不等于tail时推进consumer_heads
            header->consumer_heads[consumer_id_].store(next, std::memory_order_release);
            last_head_ = next;
            return 1;
        } else {
            // 下一个节点等于tail，说明没有新数据，不推进consumer_heads
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }
    }
}
std::string SharedRingQueueConsumer::metadata() const { return queue_->metadata(); }
uint32_t SharedRingQueueConsumer::node_count() const { return queue_->node_count(); }
uint32_t SharedRingQueueConsumer::node_size() const { return queue_->node_size(); }
size_t SharedRingQueueConsumer::shm_size() const { return queue_->shm_size(); }
uint32_t SharedRingQueueConsumer::consumer_id() const { return consumer_id_; }

void SharedRingQueueConsumer::unregister() {
    if (!registered_) return;
    auto* header = queue_->header();
    // 注销时将active置0，游标置为tail_offset
    header->consumer_active[consumer_id_].store(0);
    header->consumer_heads[consumer_id_].store(header->tail_offset.load());
    registered_ = false;
}

SharedRingQueueConsumer::~SharedRingQueueConsumer() {
    unregister();
    if (g_consumer_for_signal == this) g_consumer_for_signal = nullptr;
}

SharedMemProcessor::SharedMemProcessor(const std::string& in_shm, uint32_t in_queue_len, uint32_t in_block_size,
                                       const std::string& out_shm, uint32_t out_queue_len, uint32_t out_block_size,
                                       uint32_t total_refs, const std::string& metadata,
                                       size_t batch_size, int timeout_ms)
    : batch_size_(batch_size), timeout_ms_(timeout_ms), running_(false)
{
    has_input_ = !in_shm.empty();
    has_output_ = !out_shm.empty();
    if (has_input_) {
        in_queue_ = std::make_unique<SharedRingQueueConsumer>(in_shm, in_queue_len, in_block_size);
    }
    if (has_output_) {
        out_queue_ = std::make_unique<SharedRingQueueProducer>(out_shm, out_queue_len, out_block_size, total_refs, metadata);
    }
}

SharedMemProcessor::~SharedMemProcessor() {
    stop();
}

void SharedMemProcessor::register_callback(pybind11::function cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    py_callback_ = cb;
}

void SharedMemProcessor::start() {
    running_ = true;
    if (has_input_) input_thread_ = std::thread(&SharedMemProcessor::input_thread_func, this);
    callback_thread_ = std::thread(&SharedMemProcessor::callback_thread_func, this);
}

void SharedMemProcessor::stop() {
    running_ = false;
    cache_cv_.notify_all();
    if (has_input_ && input_thread_.joinable()) input_thread_.join();
    if (callback_thread_.joinable()) callback_thread_.join();
}

void SharedMemProcessor::input_thread_func() {
    while (running_) {
        std::vector<char> buf(in_queue_->node_size());
        uint32_t sz = 0;
        if (in_queue_->pop(buf.data(), sz) && sz > 0) {
            buf.resize(sz);
            {
                std::lock_guard<std::mutex> lk(cache_mutex_);
                cache_.push(std::move(buf));
            }
            cache_cv_.notify_one();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void SharedMemProcessor::callback_thread_func() {
    std::vector<std::vector<char>> batch;
    while (running_) {
        std::unique_lock<std::mutex> lk(cache_mutex_);
        if (cache_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms_), [&]{ return !cache_.empty() || !running_; })) {
            while (!cache_.empty() && batch.size() < batch_size_) {
                batch.push_back(std::move(cache_.front()));
                cache_.pop();
            }
        }
        lk.unlock();
        if (!batch.empty() || !has_input_) { // 没有输入时也要触发回调
            pybind11::gil_scoped_acquire gil;
            pybind11::function cb;
            {
                std::lock_guard<std::mutex> cb_lk(cb_mutex_);
                cb = py_callback_;
            }
            if (cb && !cb.is_none()) {
                try {
                    // 转换为Python list[bytes]
                    pybind11::list py_batch;
                    for (auto& v : batch) {
                        py_batch.append(pybind11::bytes(v.data(), v.size()));
                    }
                    auto py_result = cb(py_batch);
                    if (has_output_ && out_queue_) {
                        for (auto item : py_result) {
                            pybind11::bytes pybytes = item.cast<pybind11::bytes>();
                            std::string s = pybytes;
                            out_queue_->push(s.data(), s.size());
                        }
                    }
                } catch (const std::exception& e) {
                    pybind11::print("[SharedMemProcessor] Python callback exception:", e.what());
                }
            }
            batch.clear();
        }
    }
}

bool SharedMemProcessor::push_to_output(const pybind11::bytes& data) {
    if (has_output_ && out_queue_) {
        std::string s = data;
        return out_queue_->push(s.data(), s.size());
    }
    return false;
}

SharedRingQueueProducer::~SharedRingQueueProducer() {
    if (queue_) {
        // 释放所有权
        queue_->header()->producer_pid.store(0);
        
        auto* header = queue_->header();
        const int max_wait_ms = 5000;
        int waited = 0;
        while (waited < max_wait_ms) {
            bool all_inactive = true;
            for (size_t i = 0; i < MAX_CONSUMER; ++i) {
                if (header->consumer_active[i].load(std::memory_order_acquire)) {
                    all_inactive = false;
                    break;
                }
            }
            if (all_inactive) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waited += 100;
        }
        bool all_inactive = true;
        for (size_t i = 0; i < MAX_CONSUMER; ++i) {
            if (header->consumer_active[i].load(std::memory_order_acquire)) {
                all_inactive = false;
                break;
            }
        }
        if (!all_inactive) {
            fprintf(stderr, "[Producer] Warning: Some consumers are still active when removing shared memory!\n");
        }
    }
#ifdef _WIN32
#else
    shared_memory_object::remove(shm_name_.c_str());
#endif

    if (g_producer_for_signal == this) g_producer_for_signal = nullptr;
}

