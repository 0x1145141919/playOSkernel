#include "MemoryDisk.h"
#ifdef KERNEL_MODE
#include "VideoDriver.h"
#endif
#include "../memory/includes/phygpsmemmgr.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "blockdeviceids.h"
#include "OS_utils.h"
#ifdef USER_MODE
#include <cstdio>
#endif
#include "os_error_definitions.h"
MemoryDiskv1*initramfs_phylayer;
MemoryDiskv1::MemoryDiskv1(uint32_t blocksize, uint64_t blockcount, phyaddr_t pbase)
{
    this->blk_count = blockcount;
    this->blk_size = blocksize;
    this->blkdevice_type=blockdevice_id::MEMDISK_V1;
    if(blocksize&4096){
        #ifdef KERNEL_MODE
        kputsSecure("MemoryDiskv1: blocksize not aligned to 4096");
        #endif
        #ifdef USER_MODE
        printf("MemoryDiskv1: blocksize not aligned to 4096");
        #endif
    }
    this->pbase = pbase;
    size = blockcount * blocksize;
#ifdef KERNEL_MODE
    vbase=(vaddr_t)gKspacePgsMemMgr.pgs_allocate_remapped(size,gKspacePgsMemMgr.kspace_data_flags);
#endif
#ifdef USER_MODE
    vbase=pbase;
#endif
    if(vbase==0){
        #ifdef KERNEL_MODE
        kputsSecure("MemoryDiskv1: remap failed");
        #endif
        #ifdef USER_MODE
        printf("MemoryDiskv1: remap failed");
        #endif
        return;
    }
    meta=(MemoryDiskMetaInfo*)vbase;
    meta->block_count = blockcount;
    meta->blocksize = blocksize;
    meta->block_device_type=blockdevice_id::MEMDISK_V1;
}

int MemoryDiskv1::clearblk(uint64_t blkindex, uint64_t blk_count)
{
    setmem((void*)(vbase+blkindex*this->blk_size),blk_count*this->blk_size,0);
    return OS_SUCCESS;
}

int MemoryDiskv1::write(uint64_t blkindex, uint16_t inblk_offset, void *src, uint64_t size)
{
    vaddr_t dest=vbase+blkindex*this->blk_size+inblk_offset;
    ksystemramcpy(src,(void*)dest,size);
    return OS_SUCCESS;
}

int MemoryDiskv1::writeblk(uint64_t blkindex, uint64_t blk_count, void *src)
{
    vaddr_t dest=vbase+blkindex*this->blk_size;
    ksystemramcpy(src,(void*)dest,blk_count*this->blk_size);
    return OS_SUCCESS;
}

int MemoryDiskv1::read(uint64_t blkindex, uint16_t inblk_offset, void *dest, uint64_t size)
{
    vaddr_t src=vbase+blkindex*this->blk_size+inblk_offset;
    ksystemramcpy((void*)src,dest,size);
    return OS_SUCCESS;
}

int MemoryDiskv1::readblk(uint64_t blkindex, uint64_t blk_count, void *dest)
{
    vaddr_t src=vbase+blkindex*this->blk_size;
    ksystemramcpy((void*)src,dest,blk_count*this->blk_size);
    return 0;
}

int MemoryDiskv1::flush_cache()
{
    return 0;
}

void *MemoryDiskv1::get_vaddr(uint64_t blkindex, uint32_t offset)
{
    if(blkindex>=this->blk_count)return nullptr;
    return (void*)(vbase+blkindex*this->blk_size+offset);
}

MemoryDiskv1::~MemoryDiskv1()
{
#ifdef KERNEL_MODE
gKspacePgsMemMgr.pgs_remapped_free(vbase);
#endif
}