#include <boost/interprocess/managed_shared_memory.hpp>
#include <iostream>
namespace bip = boost::interprocess;

int main() {
    const char* SHM_NAME = "MySharedMemory";
    const std::size_t SHM_SIZE = 65536;

    // 删除旧段，防止遗留状态
    bip::shared_memory_object::remove(SHM_NAME);

    // 创建共享内存段
    bip::managed_shared_memory segment(
        bip::open_or_create,
        SHM_NAME,
        SHM_SIZE
    );

    // 在共享内存中构造一个整数对象
        std::string line;
    std::cout << "Enter a line: ";
    std::getline(std::cin, line);
    int* p = segment.construct<int>("MyInt")(42);
    std::cout << "Writer: MyInt = " << *p << "\n";

    std::cout << "You entered: " << line << "\n";
    return 0;
}