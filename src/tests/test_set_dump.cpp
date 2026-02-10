#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

struct test_set_record
{
    uint32_t index;
    uint64_t size;
    uint64_t initial_addr;
};

static void dump_test_set(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << path << "\n";
        std::exit(1);
    }

    in.seekg(0, std::ios::end);
    std::streamoff file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (file_size < 0 || (file_size % static_cast<std::streamoff>(sizeof(test_set_record))) != 0) {
        std::cerr << "Invalid file size: " << file_size << "\n";
        std::exit(1);
    }

    const std::size_t count = static_cast<std::size_t>(file_size) / sizeof(test_set_record);
    std::cout << "records=" << count << "\n";

    test_set_record rec{};
    for (std::size_t i = 0; i < count; ++i) {
        in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!in) {
            std::cerr << "Read failed at record " << i << "\n";
            std::exit(1);
        }
        std::cout << "Index: " << rec.index
                  << ", Size: " << rec.size
                  << ", InitialAddr: " << rec.initial_addr
                  << "\n";
    }
}

int main(int argc, char** argv)
{
    const std::string path = (argc > 1) ? argv[1] : "test_set_A.bin";
    dump_test_set(path);
    return 0;
}
