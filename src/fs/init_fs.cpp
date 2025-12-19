#include "init_fs.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#ifdef USER_MODE
#include <cstdio>
#endif
// 确保结构体类型在使用前已定义



/*文件的cluster索引转换成
实际的簇索引
*/
int init_fs_t::filecluster_to_fscluster_in_idxtb(Inode the_inode, uint64_t file_lcluster_index, uint64_t &phyidx)
{   
    if (file_lcluster_index < LEVLE1_INDIRECT_START_CLUSTER_INDEX) {

        phyidx = the_inode.data_desc.index_table.direct_pointers[file_lcluster_index];
        if (phyidx == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
if(!is_memdiskv1){
    if (file_lcluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
        // 一级间接指针范围
        uint64_t offset = file_lcluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
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

        phyidx = lv0_cluster_tb[offset];
        delete[] lv0_cluster_tb;
        if (phyidx == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    if (file_lcluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
        // 二级间接指针范围
        uint64_t offset = file_lcluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
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

        phyidx = lv0_cluster_tb[lv0_offset];
        delete[] lv0_cluster_tb;
        delete[] lv1_cluster_tb;
        if (phyidx == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }

    if (file_lcluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
        // 三级间接指针范围
        uint64_t offset = file_lcluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
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

        phyidx = lv0_cluster_tb[lv0_offset];
        delete[] lv0_cluster_tb;
        delete[] lv1_cluster_tb;
        delete[] lv2_cluster_tb;
        if (phyidx == 0) {
            return OS_FILE_NOT_FOUND;
        }
        return OS_SUCCESS;
    }
}else{//内存盘模式下不分配内存，直接解析
        if (file_lcluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
            // 一级间接指针范围
            uint64_t offset = file_lcluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
            if (offset >= MaxClusterEntryCount) {
                return OS_INVALID_PARAMETER;
            }

            uint64_t* lv0_cluster_tb = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.index_table.single_indirect_pointer * fs_metainf->cluster_block_count
            );
            phyidx = lv0_cluster_tb[offset];
            if (phyidx == 0) {
                return OS_FILE_NOT_FOUND;
            }
            return OS_SUCCESS;
        }

        if (file_lcluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
            // 二级间接指针范围
            uint64_t offset = file_lcluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
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
            phyidx = lv0_cluster_tb[lv0_offset];
            if (phyidx == 0) {
                return OS_FILE_NOT_FOUND;
            }
            return OS_SUCCESS;
        }

        if (file_lcluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
            // 三级间接指针范围
            uint64_t offset = file_lcluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
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
            phyidx = lv0_cluster_tb[lv0_offset];
            if (phyidx == 0) {
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
            physical_cluster_index=logical_file_cluster_index-cluster_scanner+extents_array[i].first_cluster_phyindex; 
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
            del_entry.block_group_index=dir_content[i].block_group_index;
            del_entry.inode_index=dir_content[i].inode_index;
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
    return extents_array[extents_entry_scanner].first_cluster_phyindex + in_entry_offset;
}
/**
 * 简述迭代器思路：
 * 1. 创建一个迭代器对象，传入文件系统指针，inode指针，缓冲区指针，偏移量，大小
 * 2.先根据数据流基址算出初始逻辑簇
 * 3.遍历拓展表到初始逻辑簇所在的拓展表项，使用积累lcluster_scanner
 * 4.在初始簇后遍历识别后续的中间相，尾巴项,用left_bytes_to_write进行判别
 * 然后在后面迭代读/写的时候根据解析的逻辑簇索引进行数据处理
 */
init_fs_t::extents_rw_iterator::extents_rw_iterator(init_fs_t *fs, Inode inode, uint8_t *buffer, uint64_t stream_offset, uint64_t size):
fs(fs),buffer(buffer),stream_offset(stream_offset),left_bytes_to_write(size)
{
    file_inode=inode;
    int status;
    if(inode.flags.extents_or_indextable==INDEX_TABLE_TYPE){state=ERROR_STATE;return;}
    uint64_t array_phyclsidx=inode.data_desc.extents.first_cluster_index_of_extents_array;
    uint64_t extent_count=inode.data_desc.extents.entries_count;
    if(fs->is_memdiskv1)
    {
        extents=(FileExtentsEntry_t*)fs->memdiskv1_blockdevice->get_vaddr(array_phyclsidx*fs->fs_metainf->cluster_block_count);
    }else{
        extents=new FileExtentsEntry_t[extent_count];
        status=fs->phylayer->read(array_phyclsidx*fs->fs_metainf->cluster_block_count,
        0,extents,extent_count*sizeof(FileExtentsEntry_t));
        if(status!=OS_SUCCESS){
            delete[] extents;
            state=ERROR_STATE;
            return;
        }
    }
    uint64_t start_lcls=stream_offset/fs->fs_metainf->cluster_size;
    uint64_t lcluster_scanner=0;//这个变量在找到start_inarray_idx之后就会事实上被废弃
    mid_array_count=0;
    bool is_start_found=false;
    for(uint64_t i=0;i<extent_count;i++ )
    {
        if(lcluster_scanner<=start_lcls&&(start_lcls<(lcluster_scanner+ extents[i].length_in_clusters)))
        {
            start_inarray_idx=i;
            start_seg_offset=stream_offset-lcluster_scanner*fs->fs_metainf->cluster_size;
            //start_seg_bytes_to
            uint32_t end_to_write_max=extents[i].length_in_clusters*fs->fs_metainf->cluster_size-start_seg_offset;
            if(end_to_write_max>=left_bytes_to_write)
            {
                start_seg_bytes_to=left_bytes_to_write;
                left_bytes_to_write=0;
            }else{
                start_seg_bytes_to=end_to_write_max;
                left_bytes_to_write-=end_to_write_max;
            }
            is_start_found=true;
        }
        if(is_start_found)
        {
            uint64_t max_to_write=extents[i].length_in_clusters*fs->fs_metainf->cluster_size;
            if(max_to_write>=left_bytes_to_write){
               
                end_array_idx=i;
                endseg_to_write_bytes=left_bytes_to_write;
                left_bytes_to_write=0;
                break;
            }else{
                mid_array_count++;
                left_bytes_to_write-=max_to_write;
            }
        }
        lcluster_scanner+=extents[i].length_in_clusters;
    }
    if(!is_start_found||left_bytes_to_write)
    {
        if(!fs->is_memdiskv1)
        {
            delete[] extents;
        }
        state=ERROR_STATE;
        return;
    }
    mid_array_baseidx=start_inarray_idx+1;
    state=START;
    accumlate_skipped_bytes=0;
}

init_fs_t::extents_rw_iterator::State init_fs_t::extents_rw_iterator::wnext()
{
    int status;
    switch(state)
    {
        case MID:
        {
            if(mid_array_scanner>=mid_array_count)
            {
                state=END_ENTRY;
                return state;
            }
            if(fs->is_memdiskv1)
            {
                ksystemramcpy(buffer+accumlate_skipped_bytes,
                fs->memdiskv1_blockdevice->get_vaddr(
                    extents[mid_array_baseidx + mid_array_scanner].first_cluster_phyindex*fs->fs_metainf->cluster_block_count
                ),extents[mid_array_baseidx + mid_array_scanner].length_in_clusters*fs->fs_metainf->cluster_size);

            }else{
                status=fs->phylayer->write(
                    extents[mid_array_baseidx + mid_array_scanner].first_cluster_phyindex*fs->fs_metainf->cluster_block_count,
                    0,buffer+accumlate_skipped_bytes,extents[mid_array_baseidx+ mid_array_scanner].length_in_clusters*fs->fs_metainf->cluster_size
                );
                if(status!=OS_SUCCESS){
                    state=ERROR_STATE;
                    return state;
                }
            }
            accumlate_skipped_bytes+=extents[mid_array_baseidx+mid_array_scanner].length_in_clusters*fs->fs_metainf->cluster_size;
        mid_array_scanner++;
        return state;
        }
        case START:
        {
            if(fs->is_memdiskv1)
            {
                ksystemramcpy(buffer,
                fs->memdiskv1_blockdevice->get_vaddr(
                    extents[start_inarray_idx].first_cluster_phyindex*fs->fs_metainf->cluster_block_count,
                    start_seg_offset
                ),start_seg_bytes_to);

            }else{
                status=fs->phylayer->write(
                    extents[start_inarray_idx].first_cluster_phyindex*fs->fs_metainf->cluster_block_count,
                    start_seg_offset,buffer,start_seg_bytes_to
                );
                if(status!=OS_SUCCESS){
                    state=ERROR_STATE;
                    return state;
                }
            }
            accumlate_skipped_bytes+=start_seg_bytes_to;
            state=MID;
           return state;
        }
        case END_ENTRY:
        {
            if(fs->is_memdiskv1)
            {
                ksystemramcpy(buffer+accumlate_skipped_bytes,
                fs->memdiskv1_blockdevice->get_vaddr(
                    extents[end_array_idx].first_cluster_phyindex*fs->fs_metainf->cluster_block_count
                ),endseg_to_write_bytes);

            }else{
                status=fs->phylayer->write(
                    extents[end_array_idx].first_cluster_phyindex*fs->fs_metainf->cluster_block_count,
                    0,buffer+accumlate_skipped_bytes,endseg_to_write_bytes
                );
                if(status!=OS_SUCCESS){
                    state=ERROR_STATE;
                    return state;
                }
            }
            state=END;
            return state;
        }
        case END:
        case ERROR_STATE:
        return state;
        default:return ERROR_STATE;
    }
}

init_fs_t::extents_rw_iterator::State init_fs_t::extents_rw_iterator::rnext()
{
    int status;
    switch(state)
    {
        case MID:
        {
            if(mid_array_scanner >= mid_array_count)
            {
                state = END_ENTRY;
                return state;
            }
            if(fs->is_memdiskv1)
            {
                uint8_t* src_addr = static_cast<uint8_t*>(fs->memdiskv1_blockdevice->get_vaddr(
                    extents[mid_array_baseidx + mid_array_scanner].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count
                ));
                ksystemramcpy(
                    buffer + accumlate_skipped_bytes,
                    src_addr,
                    extents[mid_array_baseidx + mid_array_scanner].length_in_clusters * 
                    fs->fs_metainf->cluster_size
                );
            }
            else
            {
                status = fs->phylayer->read(
                    extents[mid_array_baseidx + mid_array_scanner].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count,
                    0,
                    buffer + accumlate_skipped_bytes,
                    extents[mid_array_baseidx + mid_array_scanner].length_in_clusters * 
                    fs->fs_metainf->cluster_size
                );
                if(status != OS_SUCCESS){
                    state = ERROR_STATE;
                    return state;
                }
            }
            accumlate_skipped_bytes += extents[mid_array_baseidx + mid_array_scanner].length_in_clusters * 
                fs->fs_metainf->cluster_size;
            mid_array_scanner++;
            return state;
        }
        case START:
        {
            if(fs->is_memdiskv1)
            {
                uint8_t* src_addr = static_cast<uint8_t*>(fs->memdiskv1_blockdevice->get_vaddr(
                    extents[start_inarray_idx].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count
                )) + start_seg_offset;
                ksystemramcpy(
                    buffer,
                    src_addr,
                    start_seg_bytes_to
                );
            }
            else
            {
                status = fs->phylayer->read(
                    extents[start_inarray_idx].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count,
                    start_seg_offset,
                    buffer,
                    start_seg_bytes_to
                );
                if(status != OS_SUCCESS){
                    state = ERROR_STATE;
                    return state;
                }
            }
            accumlate_skipped_bytes += start_seg_bytes_to;
            state = MID;
            return state;
        }
        case END_ENTRY:
        {
            if(fs->is_memdiskv1)
            {
                uint8_t* src_addr = static_cast<uint8_t*>(fs->memdiskv1_blockdevice->get_vaddr(
                    extents[end_array_idx].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count
                ));
                ksystemramcpy(
                    buffer + accumlate_skipped_bytes,
                    src_addr,
                    endseg_to_write_bytes
                );
            }
            else
            {
                status = fs->phylayer->read(
                    extents[end_array_idx].first_cluster_phyindex * 
                    fs->fs_metainf->cluster_block_count,
                    0,
                    buffer + accumlate_skipped_bytes,
                    endseg_to_write_bytes
                );
                if(status != OS_SUCCESS){
                    state = ERROR_STATE;
                    return state;
                }
            }
            state = END;
            return state;
        }
        case END:
        case ERROR_STATE:
            return state;
        default:
            return ERROR_STATE;
    }
}

init_fs_t::extents_rw_iterator::State init_fs_t::extents_rw_iterator::get_state()
{
    return state;
}
