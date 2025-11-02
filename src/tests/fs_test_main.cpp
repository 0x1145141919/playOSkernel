
#include "init_fs.h"
#include <cstdio>
#include <sys/mman.h>
int main(){
    phyaddr_t ramdisk_base;
    ramdisk_base=(phyaddr_t)mmap(NULL,DEFAULT_BLK_SIZE*DEFAULT_BLK_COUNT,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((void*)ramdisk_base==MAP_FAILED){
        printf("mmap failed\n");
        return -1;
    }
    block_device_t_v1* initramdisk=new MemoryDiskv1(DEFAULT_BLK_SIZE,DEFAULT_BLK_COUNT,ramdisk_base);
    init_fs_t* fs=new init_fs_t(initramdisk);
    fs->Create_del_and_Inner_surface_test();
}