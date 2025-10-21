#pragma once
#include <stdint.h>
#include "blockdeviceids.h"
class block_device_t_v1 {
protected:

    void*phylayer;
public: 
   uint32_t blkdevice_type;
    uint64_t blk_size;
    uint64_t blk_count;
    virtual int clearblk(uint64_t blkindex,uint64_t blk_count);
    virtual int writeblk(uint64_t blkindex,uint64_t blk_count,void*src);
    virtual int readblk(uint64_t blkindex,uint64_t blk_count,void*dest);
    virtual int write(uint64_t blkindex,uint16_t inblk_offset,void*src,uint64_t size);//校验写入块的有效性，但是偏移量以及字符流结尾不超过整个块设备即可
    virtual int read(uint64_t blkindex,uint16_t inblk_offset,void*dest,uint64_t size);
    virtual ~block_device_t_v1();
};