
#include "init_fs.h"
#include <cstdio>
#include <sys/mman.h>
int main(){
    phyaddr_t ramdisk_base;
    uint64_t fs_size=DEFAULT_BLK_SIZE*DEFAULT_BLK_COUNT*4;
    ramdisk_base=(phyaddr_t)mmap(NULL,fs_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((void*)ramdisk_base==MAP_FAILED){
        printf("mmap failed\n");
        return -1;
    }
    block_device_t_v1* initramdisk=new MemoryDiskv1(DEFAULT_BLK_SIZE,DEFAULT_BLK_COUNT*4,ramdisk_base);
    init_fs_t* fs=new init_fs_t(initramdisk);
    fs->Create_del_and_Inner_surface_test();
}
/**
 * 调试文件系统在mkfs中发现inode_array相应的簇我完全没有被设置
 * 簇位图数目正常，在内存盘中正常写入
 */