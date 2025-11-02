#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#ifdef USER_MODE
#include <cstdio>
#endif
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
    }else{
        #ifdef USER_MODE
        printf("init_fs_t::init_fs_t() error: invalid filesystem,will mkfs\n");
        SuperClusterArray = new SuperCluster[100];
        Mkfs();
        #endif
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
    fs_metainf->inode_size = sizeof(Inode);
    fs_metainf->super_cluster_size = sizeof(SuperCluster);
    fs_metainf->fileEntryinDir_size = sizeof(FileEntryinDir);
    fs_metainf->hyper_cluster_size = sizeof(HyperCluster);
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
        sc->inodes_array_cluster_count = (sc->inodes_count_max * fs_metainf->inode_size + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;//算出来是0,有问题
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
        SuperClusterArray[i] = *sc;
        set_cluster_bitmap_bits(i,0,sc->inodes_array_cluster_count+sc->clusters_bitmap_cluster_count+sc->inodes_bitmap_cluster_count+1, true);

        scanner_index = last_cluster + 1;
    }
    fs_metainf->total_blocks_group_max=(fs_metainf->block_groups_array_size_in_clusters*fs_metainf->cluster_size)/fs_metainf->block_group_descriptor_size;   
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
    phylayer->write(HYPER_CLUSTER_INDEX*fs_metainf->cluster_block_count,0,fs_metainf,fs_metainf->hyper_cluster_size);
    delete root_inode;
    return OS_SUCCESS; 
}

int init_fs_t::CreateFile(Visitor_t executor, char *relative_path)
{
    int status=OS_SUCCESS;
    uint64_t path_len=strlen(relative_path);
    if(path_len==0||relative_path[0]!='/')
    {
        return OS_INVALID_PARAMETER;
    }
    char*file_name;
    uint64_t idx_scanner=path_len-1;
    while(relative_path[idx_scanner]!='/')idx_scanner--;
    file_name=new char[path_len-idx_scanner+1];
    ksystemramcpy(relative_path+idx_scanner+1,file_name,path_len-idx_scanner);
    file_name[path_len-idx_scanner]=0;
    char*dir_path=new char[idx_scanner+1];
    ksystemramcpy(relative_path,dir_path,idx_scanner);
    dir_path[idx_scanner]=0;
    Inode tail_dir_inode;
    Inode root_inode;
    status=get_inode(
        fs_metainf->root_block_group_index,
        fs_metainf->root_directory_inode_index,
        root_inode
    );
    if(status!=OS_SUCCESS)
    {
        delete[]file_name;
        delete[]dir_path;
        return status;

    }
    FilePathAnalyzer analyzer(root_inode,dir_path,idx_scanner+1,executor);
    int analyzer_state = FilePathAnalyzer::START; // 修复：初始化变量
    while(analyzer_state!=FilePathAnalyzer::END)
    {
        analyzer_state=analyzer.the_next(this);
        if(analyzer_state==FilePathAnalyzer::ACCESS_DENIED)
        {
            delete[]file_name;
            delete[]dir_path;
            return OS_PERMISSON_DENIED;
        }
        if(analyzer_state==FilePathAnalyzer::ERROR_STATE||
        analyzer_state==FilePathAnalyzer::ROOT_DIR_NEED_OTHER_FILE_SYSTEM)
        { 
            delete[]file_name;
            delete[]dir_path;
            return OS_FILE_SYSTEM_DAMAGED;
        }
    }
    tail_dir_inode=analyzer.get_current_inode();
Inode new_file_inode;
new_file_inode.gid = executor.gid;
new_file_inode.uid = executor.uid;
new_file_inode.file_size = 0;
new_file_inode.flags.dir_or_normal = FILE_TYPE;
new_file_inode.flags.extents_or_indextable = INDEX_TABLE_TYPE;
new_file_inode.flags.user_access = 7;
new_file_inode.flags.group_access = 5;
new_file_inode.flags.other_access = 5;
new_file_inode.flags.is_root_inode = false;
new_file_inode.parent_dir_block_group_index = analyzer.get_current_block_group_index(); 
new_file_inode.parent_dir_inode_index = analyzer.get_current_inode_index();
    uint64_t bgidx=0;
    FileEntryinDir new_entry_indir;
    new_entry_indir.inode_index=search_avaliable_inode_bitmap_bit(bgidx);
    new_entry_indir.block_group_index=bgidx;
    ksystemramcpy(file_name,new_entry_indir.filename,path_len-idx_scanner+1);
    status=dir_content_append(tail_dir_inode,new_entry_indir);
    if(status!=OS_SUCCESS) // 添加错误检查
    {
        delete[]file_name;
        delete[]dir_path;
        return status;
    }
    status=write_inode(bgidx,new_entry_indir.inode_index,&new_file_inode);
    if(status!=OS_SUCCESS) // 添加错误检查
    {
        delete[]file_name;
        delete[]dir_path;
        return status;
    }
    status=write_inode(
        analyzer.get_current_block_group_index(),analyzer.get_current_inode_index(),&tail_dir_inode);
    
    // 修复：释放分配的内存
    delete[]file_name;
    delete[]dir_path;
    
    // 修复：添加缺失的返回语句
    return status;
}

int init_fs_t::DeleteFile(Visitor_t executor, char *relative_path)
{
    int status=OS_SUCCESS;
    Inode target_file_inode;
    uint64_t old_len=strlen(relative_path);
    
    // 边界检查，确保不会发生数组越界
    if (old_len == 0) {
        return OS_INVALID_PARAMETER;
    }
    
    uint64_t file_name_base_idx=old_len-1;
    // 添加边界检查，防止file_name_base_idx减到负数
    while(file_name_base_idx > 0 && relative_path[file_name_base_idx-1]!='/') {
        file_name_base_idx--;
    }
    
    // 检查是否成功找到 '/' 字符
    if (file_name_base_idx == 0 && relative_path[0] != '/') {
        return OS_INVALID_PARAMETER;
    }
    
    status=path_analyze(
        relative_path,
        target_file_inode,
        executor
    );
    if(status==OS_SUCCESS)
    {
        Inode tail_dir_inode;
        status=get_inode(
            target_file_inode.parent_dir_block_group_index,
            target_file_inode.parent_dir_inode_index,
            tail_dir_inode
        );
        // 正确处理获取父目录inode失败的情况
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        resize_inode(target_file_inode,0);
        FileEntryinDir target_file_entry;
        ksystemramcpy(
            relative_path+file_name_base_idx,
            target_file_entry.filename,
            old_len-file_name_base_idx+1
        );
        status=dir_content_delete_by_name(
            tail_dir_inode,target_file_entry
        );
        // 检查目录项删除是否成功
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        status=set_inode_bitmap_bit(
            target_file_entry.block_group_index,
            target_file_entry.inode_index,
            false
        );
        // 检查设置inode位图是否成功
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        // 更新父目录inode信息
        status=write_inode(
            tail_dir_inode.parent_dir_block_group_index,
            tail_dir_inode.parent_dir_inode_index,
            &tail_dir_inode
        );
        return status;
    }else{
        return status;
    }
    // 不应执行到这里
    return OS_UNREACHABLE_CODE;
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

int init_fs_t::dir_content_search_by_name(Inode be_searched_dir_inode, char *filename, FileEntryinDir &result_entry)
{
    FileEntryinDir* dir_content=new FileEntryinDir[be_searched_dir_inode.file_size/sizeof(FileEntryinDir)];
    inode_content_read(be_searched_dir_inode,0,be_searched_dir_inode.file_size,(uint8_t*)dir_content);
    for(int i=0;i<be_searched_dir_inode.file_size/sizeof(FileEntryinDir);i++)
    {
        if(strcmp((char*)dir_content[i].filename,filename,MAX_FILE_NAME_LEN)==0)
        {
            result_entry=dir_content[i];
            delete[] dir_content;
            return OS_SUCCESS;
        }
    }
    return OS_FILE_NOT_FOUND;
}

int init_fs_t::dir_content_append(Inode &dir_inode, FileEntryinDir append_entry)
{
    int status=OS_SUCCESS;
    uint64_t old_size=dir_inode.file_size;
    status=resize_inode(dir_inode,old_size+sizeof(FileEntryinDir));
    if(status!=OS_SUCCESS)return status;
    status=inode_content_write(
        dir_inode,
        old_size,
        sizeof(FileEntryinDir),
        (uint8_t*)&append_entry
    );
    return status;
}

int init_fs_t::dir_content_delete_by_name(Inode &dir_inode, FileEntryinDir&del_entry)
{
    int status;
    FileEntryinDir* dir_content=new FileEntryinDir[dir_inode.file_size/sizeof(FileEntryinDir)];
    uint64_t old_size=dir_inode.file_size;
    inode_content_read(dir_inode,0,old_size,(uint8_t*)dir_content);
    uint64_t old_entry_count=old_size/sizeof(FileEntryinDir);
    for(int i=0;i<old_entry_count;i++)
    {
        if(strcmp((char*)dir_content[i].filename,(char*)del_entry.filename,MAX_FILE_NAME_LEN)==0)
        {
            uint64_t new_size=old_size-sizeof(FileEntryinDir);
            ksystemramcpy(
                &dir_content[i+1],
                &dir_content[i],
                (old_entry_count-i-1)*sizeof(FileEntryinDir)
            );
            status=inode_content_write(
                dir_inode,
                i*sizeof(FileEntryinDir),
                (old_entry_count-i-1)*sizeof(FileEntryinDir),
                (uint8_t*)&dir_content[i]);
            status=resize_inode(dir_inode,new_size);
            delete[] dir_content;
            return OS_SUCCESS;
        }
    }
    return OS_FILE_NOT_FOUND;
}

/**
 * @brief 访问检查
 * @param visitor 访问者信息，包含uid和gid
 * @param the_inode 目标inode
 * @param op_type 操作类型（读、写、执行等）
 * @return 访问检查结果，表示是否允许访问
 * 
 */
access_check_result init_fs_t::access_check(Visitor_t visitor, Inode the_inode, inode_op_type op_type)
{
    // 如果是超级用户，允许所有访问
    if (visitor.uid == 0) {
        return ACCESS_PERMITTED;
    }

    uint32_t access_mode = 0; // 请求的访问权限位 0-读, 1-写, 2-执行
    
    switch (op_type) {
        case INODE_OP_READ:
            access_mode = 4; // 读权限位
            break;
        case INODE_OP_WRITE:
            access_mode = 2; // 写权限位
            break;
        case INODE_OP_EXECUTE:
            access_mode = 1; // 执行权限位
            break;
        case DIR_INODE_OP_ACCESS:
            // 对于目录访问操作，默认允许
            return ACCESS_PERMITTED;
    }
    
    // 检查用户是否匹配
    if (visitor.uid == the_inode.uid) {
        // 匹配用户ID，检查用户权限
        if (the_inode.flags.user_access & access_mode) {
            return ACCESS_PERMITTED;
        }
    }
    // 检查组是否匹配
    else if (visitor.gid == the_inode.gid) {
        // 匹配组ID，检查组权限
        if (the_inode.flags.group_access & access_mode) {
            return ACCESS_PERMITTED;
        }
    }
    // 检查其他用户权限
    else {
        if (the_inode.flags.other_access & access_mode) {
            return ACCESS_PERMITTED;
        }
    }
    
    return ACCESS_DENIED;
}

int init_fs_t::global_set_cluster_bitmap_bit(uint64_t cluster_idx, bool value) {
    // 检查输入参数有效性
    if (!is_valid || fs_metainf == nullptr) {
        return OS_INVALID_PARAMETER;
    }
    
    // 检查全局簇索引是否超出范围
    if (cluster_idx >= fs_metainf->total_clusters) {
        return OS_INVALID_PARAMETER;
    }
    
    for(uint64_t i=0;i<fs_metainf->total_blocks_group_valid ;i++){
        SuperCluster* sc = get_supercluster(i);
        if(cluster_idx>=sc->first_cluster_index&&
           cluster_idx<sc->first_cluster_index+sc->cluster_count){
               uint64_t local_index=cluster_idx - sc->first_cluster_index;
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

/**
 * 根据状态机思想以inode为主附带一些其它3变量构造一个类解析路径
 * 有缺陷，没有保存当前inode的块组1号，块组内引索之类的信息
 */
int init_fs_t::path_analyze(char *path, Inode &inode,Visitor_t visitor)
{   
    Inode root_inode;
    get_inode(fs_metainf->root_block_group_index,fs_metainf->root_directory_inode_index,root_inode);
    FilePathAnalyzer path_analyzer(root_inode, path, strlen(path),visitor);//还暂时没有提供根inode的参数
    while (true)
    {
        int result = path_analyzer.the_next(this);
        if(result==path_analyzer.ERROR_STATE)
        {
            return OS_FILE_NOT_FOUND;
        }
        else if(result==path_analyzer.END)
        {
            inode=path_analyzer.get_current_inode();
            return OS_SUCCESS;
        }else if(result==path_analyzer.ROOT_DIR_NEED_OTHER_FILE_SYSTEM)
        {
            return OS_NOT_SUPPORT;
        }

    }
    
}

int init_fs_t::CreateDir(Visitor_t executor, char* relative_path)
{
    int status=OS_SUCCESS;
    uint64_t path_len=strlen(relative_path);
    if(path_len==0||relative_path[0]!='/')
    {
        return OS_INVALID_PARAMETER;
    }
    char*dir_name;
    uint64_t idx_scanner=path_len-1;
    while(idx_scanner > 0 && relative_path[idx_scanner]!='/')idx_scanner--;
    
    // 检查是否成功找到 '/' 字符
    if (idx_scanner == 0 && relative_path[0] != '/') {
        return OS_INVALID_PARAMETER;
    }
    
    dir_name=new char[path_len-idx_scanner];
    ksystemramcpy(relative_path+idx_scanner+1, dir_name, path_len-idx_scanner);
    dir_name[path_len-idx_scanner-1]=0;
    char*parent_dir_path=new char[idx_scanner+1];
    ksystemramcpy(relative_path, parent_dir_path, idx_scanner);
    parent_dir_path[idx_scanner]=0;
    Inode parent_dir_inode;
    Inode root_inode;
    status=get_inode(
        fs_metainf->root_block_group_index,
        fs_metainf->root_directory_inode_index,
        root_inode
    );
    if(status!=OS_SUCCESS)
    {
        delete[]dir_name;
        delete[]parent_dir_path;
        return status;

    }
    FilePathAnalyzer analyzer(root_inode, parent_dir_path, idx_scanner, executor);
    int analyzer_state = FilePathAnalyzer::START;
    while(analyzer_state!=FilePathAnalyzer::END)
    {
        analyzer_state=analyzer.the_next(this);
        if(analyzer_state==FilePathAnalyzer::ACCESS_DENIED)
        {
            delete[]dir_name;
            delete[]parent_dir_path;
            return OS_PERMISSON_DENIED;
        }
        if(analyzer_state==FilePathAnalyzer::ERROR_STATE||
        analyzer_state==FilePathAnalyzer::ROOT_DIR_NEED_OTHER_FILE_SYSTEM)
        { 
            delete[]dir_name;
            delete[]parent_dir_path;
            return OS_FILE_SYSTEM_DAMAGED;
        }
    }
    parent_dir_inode=analyzer.get_current_inode();
    Inode new_dir_inode;
    new_dir_inode.gid = executor.gid;
    new_dir_inode.uid = executor.uid;
    new_dir_inode.file_size = 0;
    new_dir_inode.flags.dir_or_normal = DIR_TYPE;
    new_dir_inode.flags.extents_or_indextable = INDEX_TABLE_TYPE;
    new_dir_inode.flags.user_access = 7;
    new_dir_inode.flags.group_access = 5;
    new_dir_inode.flags.other_access = 5;
    new_dir_inode.flags.is_root_inode = false;
    new_dir_inode.parent_dir_block_group_index = analyzer.get_current_block_group_index(); 
    new_dir_inode.parent_dir_inode_index = analyzer.get_current_inode_index(); 
    uint64_t bgidx=0;
    FileEntryinDir new_entry_indir;
    new_entry_indir.inode_index=search_avaliable_inode_bitmap_bit(bgidx);
    new_entry_indir.block_group_index=bgidx;
    ksystemramcpy(dir_name, new_entry_indir.filename, path_len-idx_scanner);
    status=dir_content_append(parent_dir_inode, new_entry_indir);
    if(status!=OS_SUCCESS)
    {
        delete[]dir_name;
        delete[]parent_dir_path;
        return status;
    }
    status=write_inode(bgidx, new_entry_indir.inode_index, &new_dir_inode);
    if(status!=OS_SUCCESS)
    {
        delete[]dir_name;
        delete[]parent_dir_path;
        return status;
    }
    status=write_inode(
        analyzer.get_current_block_group_index(), analyzer.get_current_inode_index(), &parent_dir_inode);
    
    delete[]dir_name;
    delete[]parent_dir_path;
    
    return status;
}

int init_fs_t::DeleteDir(Visitor_t executor, char* relative_path)
{
    int status=OS_SUCCESS;
    Inode target_dir_inode;
    uint64_t path_len=strlen(relative_path);
    
    // 边界检查，确保不会发生数组越界
    if (path_len == 0) {
        return OS_INVALID_PARAMETER;
    }
    
    uint64_t dir_name_base_idx=path_len-1;
    // 添加边界检查，防止dir_name_base_idx减到负数
    while(dir_name_base_idx > 0 && relative_path[dir_name_base_idx-1]!='/') {
        dir_name_base_idx--;
    }
    
    // 检查是否成功找到 '/' 字符
    if (dir_name_base_idx == 0 && relative_path[0] != '/') {
        return OS_INVALID_PARAMETER;
    }
    
    status=path_analyze(
        relative_path,
        target_dir_inode,
        executor
    );
    if(status==OS_SUCCESS)
    {
        // 检查是否是目录类型
        if(target_dir_inode.flags.dir_or_normal != DIR_TYPE) {
            return OS_INVALID_PARAMETER;
        }
        
        // 检查目录是否为空
        if(target_dir_inode.file_size != 0) {
            return OS_INVALID_OPERATION; // 使用已有的错误码
        }
        
        Inode parent_dir_inode;
        status=get_inode(
            target_dir_inode.parent_dir_block_group_index,
            target_dir_inode.parent_dir_inode_index,
            parent_dir_inode
        );
        // 正确处理获取父目录inode失败的情况
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        FileEntryinDir target_dir_entry;
        ksystemramcpy(
            relative_path+dir_name_base_idx,
            target_dir_entry.filename,
            path_len-dir_name_base_idx+1
        );
        status=dir_content_delete_by_name(
            parent_dir_inode, target_dir_entry
        );
        // 检查目录项删除是否成功
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        status=set_inode_bitmap_bit(
            target_dir_entry.block_group_index,
            target_dir_entry.inode_index,
            false
        );
        // 检查设置inode位图是否成功
        if(status!=OS_SUCCESS)
        {
            return status;
        }
        
        // 更新父目录inode信息
        status=write_inode(
            parent_dir_inode.parent_dir_block_group_index,
            parent_dir_inode.parent_dir_inode_index,
            &parent_dir_inode
        );
        return status;
    }else{
        return status;
    }
    // 不应执行到这里
    return OS_UNREACHABLE_CODE;
}

int init_fs_t::Create_del_and_Inner_surface_test()
{
    // 创建测试用的访客身份
    Visitor_t test_visitor = {0, 0}; // uid=0, gid=0 (root用户)
    
    // 测试1: 创建单个目录
    int status = CreateDir(test_visitor, "/testdir1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试2: 在已创建的目录中创建子目录
    status = CreateDir(test_visitor, "/testdir1/subdir1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试3: 创建多个嵌套目录
    status = CreateDir(test_visitor, "/testdir2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = CreateDir(test_visitor, "/testdir2/level1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = CreateDir(test_visitor, "/testdir2/level1/level2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = CreateDir(test_visitor, "/testdir2/level1/level2/level3");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试4: 创建文件
    status = CreateFile(test_visitor, "/testfile1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = CreateFile(test_visitor, "/testdir1/testfile2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试5: 删除空文件
    status = DeleteFile(test_visitor, "/testfile1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试6: 删除空目录(从最深层开始删除)
    status = DeleteDir(test_visitor, "/testdir2/level1/level2/level3");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = DeleteDir(test_visitor, "/testdir2/level1/level2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = DeleteDir(test_visitor, "/testdir2/level1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试7: 删除包含文件的目录中的文件
    status = DeleteFile(test_visitor, "/testdir1/testfile2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试8: 删除空的子目录
    status = DeleteDir(test_visitor, "/testdir1/subdir1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 测试9: 删除顶层目录和剩余目录
    status = DeleteDir(test_visitor, "/testdir1");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    status = DeleteDir(test_visitor, "/testdir2");
    if (status != OS_SUCCESS) {
        return status;
    }
    
    // 所有测试通过
    return OS_SUCCESS;
}

int init_fs_t::FileExtents_in_clusterScanner::the_next()
{
    if (extents_entry_scanner >= extents_count) {
        return -1; // No more extents
    }

    FileExtentsEntry_t& current_extent = extents_array[extents_entry_scanner];

    if (in_entry_offset < current_extent.length_in_clusters) {
        // Still within the current extent
        in_entry_offset++;
        return 0; // Successfully moved to the next cluster
    } else {
        // Move to the next extent
        extents_entry_scanner++;
        in_entry_offset = 0;
        skipped_clusters_count += current_extent.length_in_clusters;
        if (extents_entry_scanner >= extents_count) {
            return -1; // No more extents
        } else {
            // Move to the first cluster of the new extent
            in_entry_offset++;
            return 0; // Successfully moved to the next cluster
        }
    }
}

init_fs_t::FileExtents_in_clusterScanner::FileExtents_in_clusterScanner(FileExtentsEntry_t *extents_array, uint32_t extents_count)
{
    this->extents_array = extents_array;
    this->extents_count = extents_count;
    this->extents_entry_scanner = 0;
    this->skipped_clusters_count = 0;
    this->in_entry_offset = 0;
}

uint32_t init_fs_t::FileExtents_in_clusterScanner::convert_to_logical_cluster_index()
{
    return this->skipped_clusters_count + this->in_entry_offset;    
}

uint64_t init_fs_t::FileExtents_in_clusterScanner::get_phyclsidx()
{ 
    return extents_array[extents_entry_scanner].first_cluster_index + in_entry_offset;
}

init_fs_t::FilePathAnalyzer::FilePathAnalyzer(
    Inode root_inode, 
    char *path, uint32_t pathname_length,
    Visitor_t visitor
)//todo
{
    this->current_inode = root_inode;
    this->path = path;
    this->pathname_length = pathname_length;
    this->visitor=visitor;
    pathname_scanner = 0;
}

int init_fs_t::FilePathAnalyzer::the_next(init_fs_t *fs_instance)
{
    switch (state)
    {
    case IN_DIR:
    {
        uint32_t to_search_inode_name_startidx = pathname_scanner;
        
        // 检查是否已到达路径末尾
        if (pathname_scanner >= pathname_length)
        {
            state = END;
            return state;
        }
        
        // 查找下一个目录分隔符
        while (pathname_scanner < pathname_length && path[pathname_scanner] != '/')
        {
            pathname_scanner++;
        }
        
        // 提取目录/文件名
        uint32_t name_len = pathname_scanner - to_search_inode_name_startidx;
        if (name_len >= fs_instance->MAX_FILE_NAME_LEN)
        {
            state = ERROR_STATE;
            return state;
        }
        
        char inode_name[fs_instance->MAX_FILE_NAME_LEN];
        ksystemramcpy(
            path + to_search_inode_name_startidx,
            inode_name,
            name_len
        );
        inode_name[name_len] = '\0';  // 正确设置终止符
        
        // 检查空文件名
        if (inode_name[0] == '\0')
        {
            state = ERROR_STATE;
            return state;
        }
        access_check_result cresult=fs_instance->access_check(visitor,current_inode,DIR_INODE_OP_ACCESS);
        if(cresult==ACCESS_DENIED){
            state=ACCESS_DENIED;
            break;
        }
        // 处理特殊目录 . 和 ..
        if (inode_name[0] == '.')
        {
            if (inode_name[1] == '\0')
            {
                // 当前目录：继续处理
                if (pathname_scanner < pathname_length && path[pathname_scanner] == '/')
                {
                    pathname_scanner++; // 跳过 '/'
                }
                return state;
            }
            else if (inode_name[1] == '.' && inode_name[2] == '\0')
            {
                // 上级目录
                if (current_inode.flags.is_root_inode)
                {
                    state = ERROR_STATE;
                    return state;
                }
                else
                {
                    Inode parent_inode;
                    current_dir_block_group_index = current_inode.parent_dir_block_group_index;
                    current_dir_inode_index = current_inode.parent_dir_inode_index;
                    fs_instance->get_inode(
                        current_inode.parent_dir_block_group_index,
                        current_inode.parent_dir_inode_index,
                        parent_inode
                    );
                    current_inode = parent_inode;
                    
                    if (pathname_scanner < pathname_length && path[pathname_scanner] == '/')
                    {
                        pathname_scanner++; // 跳过 '/'
                    }
                    else
                    {
                        // 路径结束，根据当前inode类型设置状态
                        state = (current_inode.flags.dir_or_normal == DIR_TYPE) ? IN_DIR : IN_FILE;
                    }
                    return state;
                }
            }
        }
        
        // 在目录中查找对应的文件/目录项
        uint32_t dir_entries_count = current_inode.file_size / sizeof(FileEntryinDir);
        FileEntryinDir* index_content = new FileEntryinDir[dir_entries_count];
        
        if (!fs_instance->inode_content_read(
            current_inode,
            0,
            current_inode.file_size,
            (uint8_t*)index_content
        ))
        {
            delete[] index_content;
            state = ERROR_STATE;
            return state;
        }
        
        // 遍历目录项查找匹配的文件名
        bool found = false;
        for (uint32_t i = 0; i < dir_entries_count; i++)
        {
            if (strcmp((char*)index_content[i].filename, inode_name,fs_instance->MAX_FILE_NAME_LEN) == 0)
            {
                Inode next_inode;
                if (!fs_instance->get_inode(
                    index_content[i].block_group_index,
                    index_content[i].inode_index,
                    next_inode
                ))
                {
                    delete[] index_content;
                    state = ERROR_STATE;
                    return state;
                }
                current_dir_block_group_index = index_content[i].block_group_index;
                current_dir_inode_index = index_content[i].inode_index;
                current_inode = next_inode;
                found = true;    
                pathname_scanner++;
                state = (current_inode.flags.dir_or_normal == DIR_TYPE) ? IN_DIR : IN_FILE;
                break;
            }
        }
        
        delete[] index_content;
        
        if (!found)
        {
            state = ERROR_STATE;
        }
        return state;
    }
    
    case IN_FILE:
        if(pathname_scanner!=pathname_length){
            state = ERROR_STATE;
            return state;
        }

        return state;
        
    case START:
    {
        if (pathname_scanner != 0)
        {
            state = ERROR_STATE;
            return state;
        }
        
        // 绝对路径以'/'开头，跳过它
        if (pathname_length > 0 && path[0] == '/')
        {
            pathname_scanner = 1;
        }
        else
        {
            pathname_scanner = 0;
        }
        
        state = IN_DIR;
        // 递归调用处理第一个路径组件
        return the_next(fs_instance);
    }
    
    case END:
    case ROOT_DIR_NEED_OTHER_FILE_SYSTEM:
    case ERROR_STATE:
    case ACCESS_DENIED:
        return state;
        
    default:
        state = ERROR_STATE;
        return state;
    }
    return state;
}

uint32_t init_fs_t::FilePathAnalyzer::get_current_inode_index()
{
    return current_dir_inode_index;
}

uint32_t init_fs_t::FilePathAnalyzer::get_current_block_group_index()
{
    return current_dir_block_group_index;
}

init_fs_t::Inode init_fs_t::FilePathAnalyzer::get_current_inode()
{
    return current_inode;
}

