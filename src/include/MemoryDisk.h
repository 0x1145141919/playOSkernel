#include <stdint.h>
#include <BlockDevice.h>    
#include "memory/Memory.h"
constexpr uint32_t DEFAULT_BLK_SIZE =4096;
constexpr uint32_t DEFAULT_BLK_COUNT = DEFAULT_BLK_SIZE*8;
class MemoryDiskv1:public block_device_t_v1 //不支持交换空间，必须完全在物理内存中的一串连续物理地址，虽然1可以随意映射到不同的虚拟地址
{
private:
    uint64_t    size;
    vaddr_t vbase;
    phyaddr_t pbase;
    struct MemoryDiskMetaInfo
    {
    uint64_t block_count;
     uint32_t block_device_type;
    uint32_t blocksize;
    };
    MemoryDiskMetaInfo*meta;
public:
    MemoryDiskv1(uint32_t blocksize,uint64_t blockcount,phyaddr_t pbase);
    int clearblk(uint64_t blkindex,uint64_t blk_count);
    int write(uint64_t blkindex,uint16_t inblk_offset,void*src,uint64_t size);
    int writeblk(uint64_t blkindex,uint64_t blk_count,void*src);
    int read(uint64_t blkindex,uint16_t inblk_offset,void*dest,uint64_t size);
    int readblk(uint64_t blkindex,uint64_t blk_count,void*dest);
    int flush_cache();
    void* get_vaddr(uint64_t blkindex,uint32_t offset=0);//虚拟地址上连续，可以根据块引索信息获取块的起始地址，以及更多的偏移量地址
    //会对blkindex进行边界检查,但不会检查偏移量
    ~MemoryDiskv1();
};
extern MemoryDiskv1*initramfs_phylayer;


//