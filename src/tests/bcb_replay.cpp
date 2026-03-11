#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/memory/FreePagesAllocator.h"
#include "../include/util/kout.h"

struct mem_seg
{
    uint64_t start_addr;
    uint64_t size;
};

struct test_set_record
{
    uint32_t index;
    uint64_t size;
    uint64_t initial_addr;
};

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

struct mapped_file
{
    void* data;
    size_t size;
    int fd;
};

static mapped_file map_file_ro(const char* path)
{
    mapped_file mf{MAP_FAILED, 0, -1};
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return mf;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return mf;
    }
    if (st.st_size == 0) {
        close(fd);
        return mf;
    }
    void* p = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        return mf;
    }
    mf.data = p;
    mf.size = static_cast<size_t>(st.st_size);
    mf.fd = fd;
    return mf;
}

static void unmap_file(mapped_file& mf)
{
    if (mf.data != MAP_FAILED) {
        munmap(mf.data, mf.size);
        mf.data = MAP_FAILED;
    }
    if (mf.fd >= 0) {
        close(mf.fd);
        mf.fd = -1;
    }
    mf.size = 0;
}

int main(int argc, char** argv)
{
    const char* test_set_path = (argc > 1) ? argv[1] : "test_set_A.bin";
    const char* op_log_path = (argc > 2) ? argv[2] : "operation_log_B.bin";

    kio::bsp_kout.Init();
    kio::bsp_kout.shift_dec();

    mapped_file test_set_mf = map_file_ro(test_set_path);
    if (test_set_mf.data == MAP_FAILED) {
        kio::bsp_kout << "Failed to map " << test_set_path << kio::kendl;
        return 1;
    }
    mapped_file op_log_mf = map_file_ro(op_log_path);
    if (op_log_mf.data == MAP_FAILED) {
        kio::bsp_kout << "Failed to map " << op_log_path << kio::kendl;
        unmap_file(test_set_mf);
        return 1;
    }

    if ((test_set_mf.size % sizeof(test_set_record)) != 0) {
        kio::bsp_kout << "Invalid test set size: " << test_set_mf.size << kio::kendl;
        unmap_file(op_log_mf);
        unmap_file(test_set_mf);
        return 1;
    }
    if ((op_log_mf.size % sizeof(op_record)) != 0) {
        kio::bsp_kout << "Invalid op log size: " << op_log_mf.size << kio::kendl;
        unmap_file(op_log_mf);
        unmap_file(test_set_mf);
        return 1;
    }

    const size_t test_set_count = test_set_mf.size / sizeof(test_set_record);
    const size_t op_log_count = op_log_mf.size / sizeof(op_record);
    if (test_set_count == 0) {
        kio::bsp_kout << "Empty test set" << kio::kendl;
        unmap_file(op_log_mf);
        unmap_file(test_set_mf);
        return 1;
    }

    const auto* test_set = static_cast<const test_set_record*>(test_set_mf.data);
    const auto* op_log = static_cast<const op_record*>(op_log_mf.data);

    mem_seg* allocations = new mem_seg[test_set_count];
    for (size_t i = 0; i < test_set_count; ++i) {
        allocations[i].start_addr = 1;
        allocations[i].size = 0;
    }
    for (size_t i = 0; i < test_set_count; ++i) {
        const test_set_record& rec = test_set[i];
        if (rec.index >= test_set_count) {
            kio::bsp_kout << "Invalid test_set index: " << rec.index << kio::kendl;
            delete[] allocations;
            unmap_file(op_log_mf);
            unmap_file(test_set_mf);
            return 1;
        }
        allocations[rec.index].start_addr = rec.initial_addr;
        allocations[rec.index].size = rec.size;
    }

    FreePagesAllocator::first_BCB = new FreePagesAllocator::BuddyControlBlock(
        0x100000000, 5
    );
    KURD_t init_result = FreePagesAllocator::first_BCB->second_stage_init();
    if (init_result.result != result_code::SUCCESS) {
        kio::bsp_kout << "Second stage init failed" << kio::kendl;
        delete[] allocations;
        unmap_file(op_log_mf);
        unmap_file(test_set_mf);
        return 1;
    }

    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < op_log_count; ++i) {
        const op_record& rec = op_log[i];
        if (rec.index >= test_set_count) {
            kio::bsp_kout << "Invalid op index at record " << i << ": " << rec.index << kio::kendl;
            mismatches++;
            continue;
        }

        mem_seg& seg = allocations[rec.index];
        if (rec.op == op_type::ALLOCATE) {
            KURD_t alloc_result;
            phyaddr_t addr = FreePagesAllocator::first_BCB->allocate_buddy_way(seg.size, alloc_result);
            bool ok = (alloc_result.result == result_code::SUCCESS && addr != 0);
            bool expect_ok = (rec.result_code == static_cast<uint32_t>(result_code::SUCCESS) && rec.addr != 0);
            if (ok != expect_ok || (ok && addr != rec.addr)) {
                kio::bsp_kout << "ALLOC mismatch at record " << i
                              << " cycle=" << rec.cycle
                              << " index=" << rec.index
                              << " expect_addr=" << rec.addr
                              << " got_addr=" << addr
                              << " expect_code=" << rec.result_code
                              << " got_code=" << static_cast<uint32_t>(alloc_result.result)
                              << kio::kendl;
                mismatches++;
            }
            if (ok) {
                seg.start_addr = addr;
            }
        } else if (rec.op == op_type::FREE) {
            KURD_t free_result = FreePagesAllocator::first_BCB->free_buddy_way(rec.addr, rec.request_size);
            bool ok = (free_result.result == result_code::SUCCESS);
            bool expect_ok = (rec.result_code == static_cast<uint32_t>(result_code::SUCCESS));
            if (ok != expect_ok) {
                kio::bsp_kout << "FREE mismatch at record " << i
                              << " cycle=" << rec.cycle
                              << " index=" << rec.index
                              << " expect_code=" << rec.result_code
                              << " got_code=" << static_cast<uint32_t>(free_result.result)
                              << kio::kendl;
                mismatches++;
            }
            if (ok) {
                seg.start_addr = 1;
            }
        } else {
            kio::bsp_kout << "Unknown op at record " << i << kio::kendl;
            mismatches++;
        }
    }

    kio::bsp_kout << "Replay completed. Records=" << op_log_count
                  << " mismatches=" << mismatches << kio::kendl;

    if (FreePagesAllocator::first_BCB) {
        FreePagesAllocator::first_BCB->free_pages_flush();
        delete FreePagesAllocator::first_BCB;
        FreePagesAllocator::first_BCB = nullptr;
    }
    delete[] allocations;
    unmap_file(op_log_mf);
    unmap_file(test_set_mf);
    return mismatches == 0 ? 0 : 2;
}
