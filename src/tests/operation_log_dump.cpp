#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

enum class op_type : uint8_t
{
    ALLOCATE = 0,
    FREE = 1,
};

struct op_record
{
    uint32_t cycle;
    op_type op;
    uint32_t index;
    uint64_t request_size;
    uint64_t addr;
    uint32_t result_code;
};

static const char* op_to_str(op_type op)
{
    switch (op) {
    case op_type::ALLOCATE:
        return "ALLOCATE";
    case op_type::FREE:
        return "FREE";
    default:
        return "UNKNOWN";
    }
}

static void dump_operation_log(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << path << "\n";
        std::exit(1);
    }

    in.seekg(0, std::ios::end);
    std::streamoff file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (file_size < 0 || (file_size % static_cast<std::streamoff>(sizeof(op_record))) != 0) {
        std::cerr << "Invalid file size: " << file_size << "\n";
        std::exit(1);
    }

    const std::size_t count = static_cast<std::size_t>(file_size) / sizeof(op_record);
    std::cout << "records=" << count << "\n";

    op_record rec{};
    for (std::size_t i = 0; i < count; ++i) {
        in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!in) {
            std::cerr << "Read failed at record " << i << "\n";
            std::exit(1);
        }
        std::cout << "Cycle: " << rec.cycle
                  << ", Operation: " << op_to_str(rec.op)
                  << ", Index: " << rec.index
                  << ", RequestSize: " << rec.request_size
                  << ", Addr: " << rec.addr
                  << ", ResultCode: " << rec.result_code
                  << "\n";
    }
}

int main(int argc, char** argv)
{
    const std::string path = (argc > 1) ? argv[1] : "operation_log_B.bin";
    dump_operation_log(path);
    return 0;
}
