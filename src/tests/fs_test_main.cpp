
#include "init_fs.h"
#include <cstdio>
#include <sys/mman.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
int main(int argc, char *argv[]){
    phyaddr_t ramdisk_base;
    uint64_t fs_size=DEFAULT_BLK_SIZE*DEFAULT_BLK_COUNT*4;
    ramdisk_base=(phyaddr_t)mmap(NULL,fs_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((void*)ramdisk_base==MAP_FAILED){
        printf("mmap failed\n");
        return -1;
    }
    block_device_t_v1* initramdisk=new MemoryDiskv1(DEFAULT_BLK_SIZE,DEFAULT_BLK_COUNT*4,ramdisk_base);
    init_fs_t* fs=new init_fs_t(initramdisk);
    int status=fs->CreateDir({0,0},"/tmp");
    status=fs->CreateDir({0,0},"/init_resources/");
    status=fs->CreateFile({0,0},"/init_resources/init.elf");
    status=fs->CreateDir({0,0},"/etc");
    status=fs->CreateFile({0,0},"/etc/setup.txt");
    //接着就是构建初始文件系统的相关操作，我自己到时候根据情况处理
    //接着就是初始映像写回到映像文件中

    // 获取可执行文件所在目录
    char img_path[256];
    char exec_path[256];
    ssize_t len = readlink("/proc/self/exe", exec_path, sizeof(exec_path)-1);
    if (len != -1) {
        exec_path[len] = '\0';
        char *exec_dir = dirname(exec_path);
        snprintf(img_path, sizeof(img_path), "%s/initfamdisk.img", exec_dir);
    } else {
        // 备用方案：使用当前工作目录
        snprintf(img_path, sizeof(img_path), "./initfamdisk.img");
    }

    // 写入内存磁盘内容到映像文件
    int img_fd = open(img_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (img_fd < 0) {
        printf("Failed to open img file: %s\n", img_path);
        return -1;
    }
    if (write(img_fd, (void*)ramdisk_base, fs_size) != (ssize_t)fs_size) {
        printf("Failed to write img file\n");
        close(img_fd);
        return -1;
    }
    close(img_fd);

    printf("Successfully wrote initfamdisk.img to %s\n", img_path);

    // 清理资源
    delete fs;
    delete initramdisk;
    munmap((void*)ramdisk_base, fs_size);
    return 0;
}
/**
 * 调试文件系统在mkfs中发现inode_array相应的簇我完全没有被设置
 * 簇位图数目正常，在内存盘中正常写入
 */