#pragma once
#include <stdint.h>
#include "blockdeviceids.h"
class block_device_t_v1 {
protected:
    void* phylayer;
public: 
    uint32_t blkdevice_type;
    uint64_t blk_size;
    uint64_t blk_count;
    virtual int clearblk(uint64_t blkindex, uint64_t blk_count) = 0;
    virtual int writeblk(uint64_t blkindex, uint64_t blk_count, void* src) = 0;
    virtual int readblk(uint64_t blkindex, uint64_t blk_count, void* dest) = 0;
    virtual int write(uint64_t blkindex, uint16_t inblk_offset, void* src, uint64_t size) = 0;
    virtual int read(uint64_t blkindex, uint16_t inblk_offset, void* dest, uint64_t size) = 0;
    virtual int flush_cache() = 0;
    virtual ~block_device_t_v1() {};  // 析构函数也可以是纯虚的
};