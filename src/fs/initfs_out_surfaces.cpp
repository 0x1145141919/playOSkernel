#include "init_fs.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#ifdef USER_MODE
#include <cstdio>
#endif
/*
加载文件系统，
若有效则加载每个块组的Supercluster缓存到数组中
不论设备种类统一使用phylayer的read接口
*/
init_fs_t::init_fs_t(block_device_t_v1 *phylayer)
{    
    this->phylayer = phylayer;
     if(
        phylayer->blkdevice_type==blockdevice_id::MEMDISK_V1
        //false//临时测试如果完全标准块设备接口
    )
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
        is_valid = true;
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
        BlocksgroupDescriptor bgd = {sc->first_cluster_index, sc->cluster_count, 0};
        phylayer->write(fs_metainf->block_group_descriptor_first_cluster*fs_metainf->cluster_block_count,
        i*fs_metainf->block_group_descriptor_size,&bgd,sizeof(BlocksgroupDescriptor));
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
    root_inode->flags.is_root_inode = true;
    root_inode->creation_time = 0;
    root_inode->last_modification_time = 0;
    root_inode->last_access_time = 0;
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
    uint64_t path_len=strlen_in_kernel(relative_path);
    if(path_len==0||relative_path[0]!='/')
    {
        return OS_INVALID_PARAMETER;
    }
    char*file_name;
    uint64_t idx_scanner=path_len-1;
    while(relative_path[idx_scanner]!='/')idx_scanner--;
        // 检查是否成功找到 '/' 字符
    if (idx_scanner == 0 && relative_path[0] != '/') {
        return OS_INVALID_PARAMETER;
    }
    file_name=new char[path_len-idx_scanner];
    ksystemramcpy(relative_path+idx_scanner+1,file_name,path_len-idx_scanner-1);
    file_name[path_len-idx_scanner-1]=0;
    uint32_t parent_path_len=idx_scanner+1;
    char*dir_path=new char[parent_path_len+1];
    ksystemramcpy(relative_path,dir_path,parent_path_len);
    dir_path[parent_path_len]=0;
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
    ksystemramcpy(file_name,new_entry_indir.filename,path_len-idx_scanner-1);
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
    status=set_inode_bitmap_bit(bgidx,new_entry_indir.inode_index,true);
    // 修复：添加缺失的返回语句
    return status;
}

int init_fs_t::DeleteFile(Visitor_t executor, char *relative_path)
{
    int status=OS_SUCCESS;
    Inode target_file_inode;
    uint64_t old_len=strlen_in_kernel(relative_path);
    
    // 边界检查，确保不会发生数组越界
    if (old_len == 0) {
        return OS_INVALID_PARAMETER;
    }
    
    uint64_t file_name_base_idx=old_len-1;
    // 添加边界检查，防止file_name_base_idx减到负数
    while(file_name_base_idx > 0 && relative_path[file_name_base_idx-1]!='/')file_name_base_idx--;
    
    
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
            target_file_inode.parent_dir_block_group_index,
            target_file_inode.parent_dir_inode_index,
            &tail_dir_inode
        );
        return status;
    }else{
        return status;
    }
    // 不应执行到这里
    return OS_UNREACHABLE_CODE;
}
int init_fs_t::DeleteDir(Visitor_t executor, char* relative_path)
{
    int status=OS_SUCCESS;
    Inode target_dir_inode;
    uint64_t path_len=strlen_in_kernel(relative_path);
    
    // 边界检查，确保不会发生数组越界
    if (path_len == 0) {
        return OS_INVALID_PARAMETER;
    }
    
    uint64_t dir_name_base_idx=path_len-1;
    // 添加边界检查，防止dir_name_base_idx减到负数
    while(dir_name_base_idx > 0 && relative_path[dir_name_base_idx-1]!='/')dir_name_base_idx--;    
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
            target_dir_inode.parent_dir_block_group_index,
            target_dir_inode.parent_dir_inode_index,//有问题
            &parent_dir_inode
        );
        return status;
    }else{
        return status;
    }
    // 不应执行到这里
    return OS_UNREACHABLE_CODE;
}
int init_fs_t::WriteFile(FileID_t in_fs_id, void *src, uint64_t size)
{
    init_fs_opened_file_entry target_file_entry;
    int status;
    status=opened_file_entry_search_by_id(in_fs_id,target_file_entry);
    if(status!=OS_SUCCESS)
    {

    }
    if(target_file_entry.file_flags.is_append_mode)
    {
        uint64_t new_size=target_file_entry.inode.file_size+size;
        uint64_t old_size=target_file_entry.inode.file_size;
        status=resize_inode(
            target_file_entry.inode,
            new_size
        );
        status=inode_content_write(target_file_entry.inode,old_size,size,(uint8_t*)src);
    }else{
        uint64_t new_size=target_file_entry.offset_ptr+size;
        uint64_t old_size=target_file_entry.inode.file_size;
        if(new_size>old_size){
            status=resize_inode(target_file_entry.inode,new_size);
            if(status!=OS_SUCCESS){

            }
        }
        status=inode_content_write(target_file_entry.inode,target_file_entry.offset_ptr,size,(uint8_t*)src);
        target_file_entry.offset_ptr+=size;
    }
    status=opened_file_entry_set_by_id(in_fs_id,target_file_entry);
    return OS_SUCCESS;
}
int init_fs_t::OpenFile(Visitor_t executor, char *relative_path, FileID_t &in_fs_id, FopenFlags flags)
{
    int status=OS_SUCCESS;
    Inode target_file_inode;
    Inode root_inode;
    status=get_inode(
        fs_metainf->root_block_group_index,
        fs_metainf->root_directory_inode_index,
        root_inode
    );
    FilePathAnalyzer path_analyzer(
        root_inode,
        relative_path,
        strlen_in_kernel(relative_path),
        executor
    );
    int analyze_status;
    while (true)
    {
        analyze_status=path_analyzer.the_next(this);
        if(analyze_status==FilePathAnalyzer::END)
        {
            break;
        }
        if(analyze_status==FilePathAnalyzer::ERROR_STATE||
        analyze_status==FilePathAnalyzer::ROOT_DIR_NEED_OTHER_FILE_SYSTEM)
        {
            return OS_IO_ERROR;
        }
        if(analyze_status==FilePathAnalyzer::ACCESS_DENIED)
        {
            return OS_PERMISSON_DENIED;
        }
    }
    uint32_t target_file_inode_index=path_analyzer.get_current_inode_index();
    uint32_t target_file_bgidx=path_analyzer.get_current_block_group_index();
    if((flags.is_write||flags.is_read)==false)return OS_INVALID_PARAMETER;
    if(flags.is_write)
    {
        access_check_result write_check=access_check(executor,path_analyzer.get_current_inode(),inode_op_type::INODE_OP_WRITE);
        if(write_check==access_check_result::ACCESS_DENIED)return OS_PERMISSON_DENIED;
        init_fs_opened_file_entry maybe_same_file;
        status=opened_file_entry_search_by_bgidx_and_inodeidx(
                target_file_bgidx,
                target_file_inode_index,
                maybe_same_file
        );
        if(status==OS_NOT_EXIST){

        }else{
            if(status==OS_SUCCESS)
            {
                if(maybe_same_file.inode.trylock.try_lock()==false)
                {
                    if(flags.is_force==false)
                    {
                        return OS_TRY_LOCK_FAIL;
                    }
                }
            }else{
                return OS_IO_ERROR;
            }
        }
    }
    if(flags.is_read)
    {
        access_check_result read_check=access_check(executor,path_analyzer.get_current_inode(),inode_op_type::INODE_OP_READ);
        if(read_check==access_check_result::ACCESS_DENIED)return OS_PERMISSON_DENIED;
    }
    init_fs_opened_file_entry will_poen_entry;
    will_poen_entry.inode=path_analyzer.get_current_inode();
    will_poen_entry.block_group_index   =target_file_bgidx;
    will_poen_entry.inode_idx=target_file_inode_index;
    will_poen_entry.is_valid_entry=true;
    will_poen_entry.file_flags=flags;
    will_poen_entry.offset_ptr=0;
    status=opened_file_entry_alloc_a_new_entry(will_poen_entry,in_fs_id);
    if(status!=OS_SUCCESS)return status;
    return OS_SUCCESS;
}
int init_fs_t::SeekFile(FileID_t in_fs_id, uint64_t offset)
{
    init_fs_opened_file_entry target_file_entry;
    int status;
    status=opened_file_entry_search_by_id(in_fs_id,target_file_entry);
    target_file_entry.offset_ptr=offset;
    status=opened_file_entry_set_by_id(in_fs_id,target_file_entry);
    return OS_SUCCESS;
}
int init_fs_t::CloseFile(FileID_t in_fs_id)
{
    return opened_file_entry_disable_by_id(in_fs_id);
}
init_fs_t::~init_fs_t()
{
    delete[] SuperClusterArray;
}

int init_fs_t::opened_file_entry_search_by_id(FileID_t in_fs_id, init_fs_opened_file_entry& result_entry)
{//ai生成未检验
    if (in_fs_id >= opened_file_array_max_count) {
        return OS_INVALID_PARAMETER;
    }
    
    if (!opened_file_table[in_fs_id].is_valid_entry) {
        return OS_NOT_EXIST;
    }
    
    result_entry = opened_file_table[in_fs_id];
    return OS_SUCCESS;
}

int init_fs_t::opened_file_entry_set_by_id(FileID_t in_fs_id, init_fs_opened_file_entry& result_entry)
{//ai生成未检验
    if (in_fs_id >= opened_file_array_max_count) {
        return OS_INVALID_PARAMETER;
    }
    
    if (!opened_file_table[in_fs_id].is_valid_entry) {
        return OS_NOT_EXIST;
    }
    
    opened_file_table[in_fs_id] = result_entry;
    return OS_SUCCESS;
}

int init_fs_t::opened_file_entry_disable_by_id(FileID_t in_fs_id)
{//ai生成未检验
    if (in_fs_id >= opened_file_array_max_count) {
        return OS_INVALID_PARAMETER;
    }
    
    if (!opened_file_table[in_fs_id].is_valid_entry) {
        return OS_NOT_EXIST;
    }
    
    opened_file_table[in_fs_id].is_valid_entry = false;
    opened_file_count--;
    return OS_SUCCESS;
}

int init_fs_t::opened_file_entry_search_by_bgidx_and_inodeidx(
    uint32_t bgidx,
    uint32_t inode_idx,
    init_fs_opened_file_entry& result_entry)
{//ai生成未检验
    for (uint32_t i = 0; i < opened_file_array_max_count; i++) {
        if (opened_file_table[i].is_valid_entry &&
            opened_file_table[i].block_group_index == bgidx &&
            opened_file_table[i].inode_idx == inode_idx) {
            result_entry = opened_file_table[i];
            return OS_SUCCESS;
        }
    }
    
    return OS_NOT_EXIST;
}

int init_fs_t::opened_file_entry_alloc_a_new_entry(init_fs_opened_file_entry& new_entry, FileID_t& alloced_entry)
{//ai生成未检验
    for (uint32_t i = 0; i < opened_file_array_max_count; i++) {
        if (!opened_file_table[i].is_valid_entry) {
            opened_file_table[i] = new_entry;
            opened_file_table[i].is_valid_entry = true;
            alloced_entry = i;
            opened_file_count++;
            return OS_SUCCESS;
        }
    }
    
    return OS_OUT_OF_RESOURCE;
}

int init_fs_t::CreateDir(Visitor_t executor, char *relative_path)
{
    int status=OS_SUCCESS;
    uint64_t path_len=strlen_in_kernel(relative_path);
    if(path_len==0||relative_path[0]!='/')
    {
        return OS_INVALID_PARAMETER;
    }
    char*dir_name;
    uint64_t idx_scanner=path_len-1;
    while(idx_scanner > 0 && relative_path[idx_scanner]!='/')idx_scanner--;
    uint64_t parent_apth_len=idx_scanner+1;
    // 检查是否成功找到 '/' 字符
    if (idx_scanner == 0 && relative_path[0] != '/') {
        return OS_INVALID_PARAMETER;
    }
    
    dir_name=new char[path_len-idx_scanner];
    ksystemramcpy(relative_path+idx_scanner+1, dir_name, path_len-idx_scanner);
    dir_name[path_len-idx_scanner-1]=0;
    char*parent_dir_path=new char[parent_apth_len+1];
    ksystemramcpy(relative_path, parent_dir_path, parent_apth_len);
    parent_dir_path[parent_apth_len]=0;
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
    FilePathAnalyzer analyzer(root_inode, parent_dir_path, parent_apth_len, executor);
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
    status=set_inode_bitmap_bit(bgidx, new_entry_indir.inode_index, true);
    if(status!=OS_SUCCESS)
    {
        status=OS_IO_ERROR;
    }
    return status;
}