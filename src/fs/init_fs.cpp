#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"

// 确保结构体类型在使用前已定义

/*
加载文件系统，
若有效则加载每个块组的Supercluster缓存到数组中
不论设备种类统一使用phylayer的read接口
*/
init_fs_t::init_fs_t(block_device_t_v1 *phylayer)
{    
    this->phylayer = phylayer;
     if(phylayer->blkdevice_type==blockdevice_id::MEMDISK_V1)
    {
        is_memdiskv1 =true;
        memdiskv1_blockdevice = (MemoryDiskv1*)phylayer;
        fs_metainf = (HyperCluster*)memdiskv1_blockdevice->get_vaddr(HYPER_CLUSTER_INDEX);
        is_valid=(fs_metainf->magic==HYPER_CLUSTER_MAGIC);
    }else
    {
    is_memdiskv1 =false; int status;
    fs_metainf =new HyperCluster();
    // 将簇索引转换为块索引
    status =phylayer->read(0,0,fs_metainf,sizeof(HyperCluster));
    is_valid =(status==0 && fs_metainf->magic==HYPER_CLUSTER_MAGIC );
    }
    if(is_valid){
        // 加载每个块组的Supercluster缓存到数组中
        SuperClusterArray = new SuperCluster[fs_metainf->total_blocks_group_valid];
        
        // 计算块组描述符数组的大小和需要的块数
        size_t bgd_array_size = fs_metainf->total_blocks_group_valid * sizeof(BlocksgroupDescriptor);
        
        // 计算块组描述符数组的起始块
        uint64_t base_block = fs_metainf->block_group_descriptor_first_cluster * fs_metainf->cluster_block_count;
        
        BlocksgroupDescriptor* bgd_array = nullptr;
        
        if(is_memdiskv1) {
            // 内存盘模式：直接通过虚拟地址访问整个块组描述符数组
            uint8_t* base_addr = reinterpret_cast<uint8_t*>(fs_metainf);
            bgd_array = reinterpret_cast<BlocksgroupDescriptor*>(
                base_addr + fs_metainf->block_group_descriptor_first_cluster * fs_metainf->cluster_size
            );
        } else {
            // 普通块设备：一次性读取整个块组描述符数组
            bgd_array = new BlocksgroupDescriptor[fs_metainf->total_blocks_group_valid];
            if (!bgd_array) {
                is_valid = false;
            } else {
                int status = phylayer->read(base_block, 0, bgd_array, bgd_array_size);
                if (status != 0) {
                    delete[] reinterpret_cast<uint8_t*>(bgd_array);
                    bgd_array = nullptr;
                    is_valid = false;
                }
            }
        }

        if (is_valid && bgd_array) {
            for(uint64_t i = 0; i < fs_metainf->total_blocks_group_valid; i++) {
                // 从块组描述符数组中获取当前块组的描述符
                BlocksgroupDescriptor& bgd = bgd_array[i];
                
                // 获取SuperCluster所在簇
                uint64_t supercluster_cluster = bgd.first_cluster_index;
                uint64_t block_idx_sc = supercluster_cluster * fs_metainf->cluster_block_count;

                // 读取SuperCluster
                if(is_memdiskv1) {
                    // 内存盘模式：直接通过虚拟地址访问
                    uint8_t* base_addr = reinterpret_cast<uint8_t*>(fs_metainf);
                    SuperCluster* sc = reinterpret_cast<SuperCluster*>(base_addr + supercluster_cluster * fs_metainf->cluster_size);
                    if(sc->magic == SUPER_CLUSTER_MAGIT) {
                        SuperClusterArray[i] = *sc;
                    } else {
                        is_valid = false;
                        break;
                    }
                } else {
                    // 普通块设备：通过read接口读取
                    SuperCluster* sc = new SuperCluster();
                    if (!sc) {
                        is_valid = false;
                        break;
                    }
                    int status = phylayer->read(block_idx_sc, 0, sc, sizeof(SuperCluster));
                    if (status != 0 || sc->magic != SUPER_CLUSTER_MAGIT) {
                        delete sc;
                        is_valid = false;
                        break;
                    }
                    SuperClusterArray[i] = *sc;
                    delete sc;
                }
            }
        }

        // 释放临时分配的内存（仅对普通块设备）
        if(!is_memdiskv1 && bgd_array) {
            delete[] reinterpret_cast<uint8_t*>(bgd_array);
        }
        
        // 如果出现错误，清理已分配的资源
        if(!is_valid) {
            delete[] SuperClusterArray;
            SuperClusterArray = nullptr;
        }
    }

}


/*
这个函数原理后面会改成会从文件系统类的超级块数组数组中取，但也这要求这个构造函数把所有的超级块放进数组中
*/
init_fs_t::SuperCluster* init_fs_t::get_supercluster(uint32_t block_group_index) {
    // 检查索引是否有效
    if(block_group_index >= fs_metainf->total_blocks_group_valid)
        return nullptr;
    
    // 直接从缓存数组中返回SuperCluster
    // 注意：这里返回的是缓存数组中元素的地址，调用者不应释放该内存
    return &SuperClusterArray[block_group_index];
}

/**
 * 函数大体流程：
 * 1. 根据物理块信息创建超级块
 * 2.根据分区大小创建块组以及相应表项
 * 3.给每个块组进行相应初始化
 * 4.创建根目录的inode
 * */
int init_fs_t::Mkfs()
{
    fs_metainf->magic = HYPER_CLUSTER_MAGIC;
    fs_metainf->total_blocks = phylayer->blk_count;
    fs_metainf->block_size = phylayer->blk_size;
    fs_metainf->cluster_size = CLUSTER_DEFAULT_SIZE;
    fs_metainf->cluster_block_count = fs_metainf->cluster_size / fs_metainf->block_size;   
    fs_metainf->total_clusters = fs_metainf->total_blocks / fs_metainf->cluster_block_count;
    fs_metainf->max_block_group_size_in_clusters = DEFAULT_BLOCKS_GROUP_MAX_CLUSTER;
    fs_metainf->block_group_descriptor_first_cluster = 1;
    fs_metainf->total_blocks_group_valid = (fs_metainf->total_clusters + DEFAULT_BLOCKS_GROUP_MAX_CLUSTER - 1) / DEFAULT_BLOCKS_GROUP_MAX_CLUSTER;
    fs_metainf->block_group_descriptor_size = sizeof(BlocksgroupDescriptor);
    fs_metainf->block_groups_array_size_in_clusters = (fs_metainf->total_blocks_group_valid * fs_metainf->block_group_descriptor_size + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
    
    uint64_t scanner_index = fs_metainf->block_group_descriptor_first_cluster + fs_metainf->block_groups_array_size_in_clusters;
    
    for(uint64_t i = 0; i < fs_metainf->total_blocks_group_valid; i++) { 
        SuperCluster* sc = new SuperCluster();
        uint64_t last_cluster = scanner_index + fs_metainf->max_block_group_size_in_clusters - 1;
        if(last_cluster > fs_metainf->total_clusters) last_cluster = fs_metainf->total_clusters - 1;
        
        sc->magic = SUPER_CLUSTER_MAGIT;
        sc->first_cluster_index = scanner_index;
        sc->cluster_count = last_cluster - scanner_index + 1;
        sc->inodes_count_max=sc->cluster_count>>3;
        sc->inodes_array_first_cluster = scanner_index + 1;
        sc->inodes_array_cluster_count = (sc->inodes_count_max * fs_metainf->inode_size + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
        sc->inodes_bitmap_first_cluster = scanner_index + 1 + sc->inodes_array_cluster_count;
        uint64_t bitmap_bits = sc->inodes_count_max;
        uint64_t bitmap_bytes = (bitmap_bits + 7) / 8;  // 向上取整到字节
        // 计算需要的簇数
        sc->inodes_bitmap_cluster_count = (bitmap_bytes + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
        sc->clusters_bitmap_first_cluster = sc->inodes_bitmap_first_cluster + sc->inodes_bitmap_cluster_count;
        uint64_t clusters_bits = sc->cluster_count;
        uint64_t clusters_bytes = (clusters_bits + 7) / 8;
        sc->clusters_bitmap_cluster_count = (clusters_bytes + fs_metainf->cluster_size-1) / fs_metainf->cluster_size;
        uint64_t block_index = scanner_index * fs_metainf->cluster_block_count;
        phylayer->write(block_index, 0, sc, sizeof(SuperCluster));
        uint64_t inodes_bitmap_block = sc->inodes_bitmap_first_cluster * fs_metainf->cluster_block_count;
        phylayer->clearblk(inodes_bitmap_block, sc->inodes_bitmap_cluster_count * fs_metainf->cluster_block_count);
        uint64_t clusters_bitmap_block = sc->clusters_bitmap_first_cluster * fs_metainf->cluster_block_count;
        phylayer->clearblk(clusters_bitmap_block, sc->clusters_bitmap_cluster_count * fs_metainf->cluster_block_count);
        set_cluster_bitmap_bits(i,0,sc->inodes_array_cluster_count+sc->clusters_bitmap_cluster_count+sc->inodes_bitmap_cluster_count+1, true);

        scanner_index = last_cluster + 1;
    }
    
    Inode* root_inode = new Inode();
    root_inode->gid = 0;
    root_inode->uid = 0;
    root_inode->file_size = 0;
    root_inode->flags.dir_or_normal = DIR_TYPE;
    root_inode->flags.extents_or_indextable = INDEX_TABLE_TYPE;
    root_inode->flags.user_access = 7;
    root_inode->flags.group_access = 7;
    root_inode->flags.other_access = 7;
    root_inode->creation_time = 0;
    root_inode->last_modification_time = 0;
    root_inode->last_access_time = 0;
    uint64_t rootdirbg=0;
    uint64_t rootdir_first_cluster = search_avaliable_cluster_bitmap_bit(rootdirbg);
    root_inode->data_desc.index_table.direct_pointers[0]=rootdir_first_cluster;
    set_cluster_bitmap_bit(0, rootdir_first_cluster, true);
    set_inode_bitmap_bit(0, 0, true);
    write_inode(0,0,root_inode);
    fs_metainf->root_block_group_index = 0;
    fs_metainf->root_directory_inode_index = 0;
    phylayer->write(HYPER_CLUSTER_INDEX*fs_metainf->cluster_block_count,0,fs_metainf,sizeof(HyperCluster));
    delete root_inode;
    return OS_SUCCESS; 
}

/*文件的cluster索引转换成
实际的簇索引
*/
int init_fs_t::filecluster_to_fscluster_in_idxtb(Inode the_inode, uint64_t file_cluster_index, uint64_t &cluster_index)
{   
    if (file_cluster_index < LEVLE1_INDIRECT_START_CLUSTER_INDEX) {

        cluster_index = the_inode.data_desc.index_table.direct_pointers[file_cluster_index];
        if (cluster_index == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
if(!is_memdiskv1){
    if (file_cluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
        // 一级间接指针范围
        uint64_t offset = file_cluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
        if (offset >= MaxClusterEntryCount) {
            return OS_INVALID_PARAMETER;
        }

        uint64_t indirect_block_index = the_inode.data_desc.index_table.single_indirect_pointer * fs_metainf->cluster_block_count;
        uint64_t* lv0_cluster_tb = new uint64_t[MaxClusterEntryCount];
        int status = phylayer->readblk(indirect_block_index, 1, lv0_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv0_cluster_tb;
            return status;
        }

        cluster_index = lv0_cluster_tb[offset];
        delete[] lv0_cluster_tb;
        if (cluster_index == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    if (file_cluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
        // 二级间接指针范围
        uint64_t offset = file_cluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
        if (offset >= MaxClusterEntryCount * MaxClusterEntryCount) {
            return OS_INVALID_PARAMETER;
        }

        uint64_t lv1_offset = offset >> 9;  // 相当于 offset / 512
        uint64_t lv0_offset = offset & 511; // 相当于 offset % 512

        uint64_t lv1_block_index = the_inode.data_desc.index_table.double_indirect_pointer * fs_metainf->cluster_block_count;
        uint64_t* lv1_cluster_tb = new uint64_t[MaxClusterEntryCount];
        int status = phylayer->readblk(lv1_block_index, 1, lv1_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv1_cluster_tb;
            return status;
        }

        uint64_t lv0_block_index = lv1_cluster_tb[lv1_offset] * fs_metainf->cluster_block_count;
        uint64_t* lv0_cluster_tb = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(lv0_block_index, 1, lv0_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv0_cluster_tb;
            delete[] lv1_cluster_tb;
            return status;
        }

        cluster_index = lv0_cluster_tb[lv0_offset];
        delete[] lv0_cluster_tb;
        delete[] lv1_cluster_tb;
        if (cluster_index == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    if (file_cluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
        // 三级间接指针范围
        uint64_t offset = file_cluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
        if (offset >= (uint64_t)MaxClusterEntryCount * MaxClusterEntryCount * MaxClusterEntryCount) {
            return OS_INVALID_PARAMETER;
        }

        uint64_t lv2_offset = offset >> 18; // offset / (512*512)
        uint64_t lv1_offset = (offset >> 9) & 511; // (offset / 512) % 512
        uint64_t lv0_offset = offset & 511;        // offset % 512

        uint64_t lv2_block_index = the_inode.data_desc.index_table.triple_indirect_pointer * fs_metainf->cluster_block_count;
        uint64_t* lv2_cluster_tb = new uint64_t[MaxClusterEntryCount];
        int status = phylayer->readblk(lv2_block_index, 1, lv2_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv2_cluster_tb;
            return status;
        }

        uint64_t lv1_block_index = lv2_cluster_tb[lv2_offset] * fs_metainf->cluster_block_count;
        uint64_t* lv1_cluster_tb = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(lv1_block_index, 1, lv1_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv1_cluster_tb;
            delete[] lv2_cluster_tb;
            return status;
        }

        uint64_t lv0_block_index = lv1_cluster_tb[lv1_offset] * fs_metainf->cluster_block_count;
        uint64_t* lv0_cluster_tb = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(lv0_block_index, 1, lv0_cluster_tb);
        if (status != OS_SUCCESS) {
            delete[] lv0_cluster_tb;
            delete[] lv1_cluster_tb;
            delete[] lv2_cluster_tb;
            return status;
        }

        cluster_index = lv0_cluster_tb[lv0_offset];
        delete[] lv0_cluster_tb;
        delete[] lv1_cluster_tb;
        delete[] lv2_cluster_tb;
        if (cluster_index == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }
}else{//内存盘模式下不分配内存，直接解析
        if (file_cluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
            // 一级间接指针范围
            uint64_t offset = file_cluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
            if (offset >= MaxClusterEntryCount) {
                return OS_INVALID_PARAMETER;
            }

            uint64_t* lv0_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.index_table.single_indirect_pointer * fs_metainf->cluster_block_count
            );
            cluster_index = lv0_cluster_tb[offset];
            if (cluster_index == 0) {
                return OS_FILE_NOT_FOUND;
            }
            return OS_SUCCESS;
        }

        if (file_cluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
            // 二级间接指针范围
            uint64_t offset = file_cluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
            if (offset >= (uint64_t)MaxClusterEntryCount * MaxClusterEntryCount) {
                return OS_INVALID_PARAMETER;
            }

            uint64_t lv1_offset = offset >> 9;
            uint64_t lv0_offset = offset & 511;

            uint64_t* lv1_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.index_table.double_indirect_pointer * fs_metainf->cluster_block_count
            );
            uint64_t* lv0_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                lv1_cluster_tb[lv1_offset] * fs_metainf->cluster_block_count
            );
            cluster_index = lv0_cluster_tb[lv0_offset];
            if (cluster_index == 0) {
                return OS_FILE_NOT_FOUND;
            }
            return OS_SUCCESS;
        }

        if (file_cluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
            // 三级间接指针范围
            uint64_t offset = file_cluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
            if (offset >= (uint64_t)MaxClusterEntryCount * MaxClusterEntryCount * MaxClusterEntryCount) {
                return OS_INVALID_PARAMETER;
            }

            uint64_t lv2_offset = offset >> 18;
            uint64_t lv1_offset = (offset >> 9) & 511;
            uint64_t lv0_offset = offset & 511;

            uint64_t* lv2_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.index_table.triple_indirect_pointer * fs_metainf->cluster_block_count
            );
            uint64_t* lv1_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                lv2_cluster_tb[lv2_offset] * fs_metainf->cluster_block_count
            );
            uint64_t* lv0_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                lv1_cluster_tb[lv1_offset] * fs_metainf->cluster_block_count
            );
            cluster_index = lv0_cluster_tb[lv0_offset];
            if (cluster_index == 0) {
                return OS_FILE_NOT_FOUND;
            }
            return OS_SUCCESS;
        }
    }
    return OS_INVALID_PARAMETER;
}
int init_fs_t::filecluster_to_fscluster_in_extents(Inode the_inode, uint64_t logical_file_cluster_index, uint64_t &physical_cluster_index, FileExtentsEntry_t &extents_entry)
{   
    int status;
    uint32_t extents_count=the_inode.data_desc.extents.entries_count;
    uint32_t extents_array_size=extents_count*sizeof(FileExtentsEntry_t);
    FileExtentsEntry_t*extents_array;
    if(is_memdiskv1)
    {
        extents_array=(FileExtentsEntry_t*)memdiskv1_blockdevice->get_vaddr(
            the_inode.data_desc.extents.first_cluster_index_of_extents_array *fs_metainf->cluster_block_count
        );

    }else{
        extents_array=new FileExtentsEntry_t[extents_count];
        uint64_t extents_array_first_cluster=the_inode.data_desc.extents.first_cluster_index_of_extents_array;
        status=phylayer->read(
            extents_array_first_cluster*fs_metainf->cluster_block_count,
            0,
            extents_array,
            extents_array_size
        );
        if(status!=OS_SUCCESS)
        {
            delete[] extents_array;
            return status;
        }
       
    } 
    uint64_t cluster_scanner=0;
    for(int i=0;i<extents_count;i++){
        if(cluster_scanner<=logical_file_cluster_index&&logical_file_cluster_index<cluster_scanner+extents_array[i].length_in_clusters)
        {
            physical_cluster_index=logical_file_cluster_index-cluster_scanner+extents_array[i].first_cluster_index; 
            delete[] extents_array;
            return OS_SUCCESS;
        }else{
            cluster_scanner+=extents_array[i].length_in_clusters;
        }
        }
    if(!is_memdiskv1)delete[] extents_array;
    return OS_IO_ERROR;
}

int init_fs_t::global_set_cluster_bitmap_bit(uint64_t block_index, bool value) {
    // 检查输入参数有效性
    if (!is_valid || fs_metainf == nullptr) {
        return OS_INVALID_PARAMETER;
    }
    
    // 检查全局簇索引是否超出范围
    if (block_index >= fs_metainf->total_clusters) {
        return OS_INVALID_PARAMETER;
    }
    
    for(uint64_t i=0;i<fs_metainf->total_blocks_group_valid ;i++){
        SuperCluster* sc = get_supercluster(i);
        if(block_index>=sc->first_cluster_index&&
           block_index<sc->first_cluster_index+sc->cluster_count){
               uint64_t local_index=block_index - sc->first_cluster_index;
               return set_cluster_bitmap_bit(i, local_index, value);
           }
    }
    return OS_INVALID_PARAMETER;
}

int init_fs_t::global_set_cluster_bitmap_bits(uint64_t base_index, uint64_t bit_count, bool value) {
    // 检查输入参数有效性
    if (!is_valid || fs_metainf == nullptr) {
        return OS_INVALID_PARAMETER;
    }
    
    // 检查起始索引是否超出范围
    if (base_index >= fs_metainf->total_clusters) {
        return OS_INVALID_PARAMETER;
    }
    
    // 检查位数是否为零或导致溢出
    if (bit_count == 0 || base_index + bit_count > fs_metainf->total_clusters) {
        return OS_INVALID_PARAMETER;
    }
    
  for(uint64_t i=0;i<fs_metainf->total_blocks_group_valid ;i++){
        SuperCluster* sc = get_supercluster(i);
        if(base_index>=sc->first_cluster_index&&
           base_index<sc->first_cluster_index+sc->cluster_count
        &&base_index+bit_count<=sc->first_cluster_index+sc->cluster_count){
               uint64_t local_base_index=base_index - sc->first_cluster_index;
               set_cluster_bitmap_bits(i, local_base_index, bit_count, value);
               return OS_SUCCESS;
           }
    }
    return OS_INVALID_PARAMETER;
}

// 从根inode解析路径
int init_fs_t::path_analyze(char *path, Inode &inode)
{   
    uint64_t filepathlen=strlen(path);
    if(filepathlen==0||filepathlen>=FILE_PATH_MAX_LEN)return OS_INVALID_FILE_PATH;
    if(path[0]!='/')return OS_INVALID_FILE_PATH;
    char*NameStrStartptr=path+1;
    char*NameStrEndptr=path+1;
    char*end=path+filepathlen;
    Inode RootdirInode;
    if(is_memdiskv1){
    get_inode(fs_metainf->root_block_group_index,fs_metainf->root_directory_inode_index,RootdirInode);
    FileEntryinDir* rootdirs_file_entry;
    
    while (NameStrEndptr<end||NameStrStartptr<end)
    {
        while(*NameStrEndptr!='/')NameStrEndptr++;
        *NameStrEndptr='\0';

    }
}else{

}
    return OS_SUCCESS;
}
