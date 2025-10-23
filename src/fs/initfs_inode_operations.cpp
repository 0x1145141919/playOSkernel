#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
/**
 * @brief 改变inode指向的数据流的大小
 *并使用bit相关操作分配释放数据流所占领的相关簇
 * @param the_inode inode对象
 * @param new_size 新的inode大小
 * @return int 错误码
 
 */

int init_fs_t::resize_inode(Inode &the_inode, uint64_t new_size)
{   int status=OS_SUCCESS;
    uint64_t old_logical_end_cluster_index=the_inode.file_size / fs_metainf->cluster_size;
    uint64_t new_logical_end_cluster_index=new_size / fs_metainf->cluster_size;
    int64_t cluster_shift=new_logical_end_cluster_index-old_logical_end_cluster_index;
    if(cluster_shift>0)status=Increase_inode_allocated_clusters(the_inode,cluster_shift);
    if(cluster_shift==0)the_inode.file_size=new_size;
    if(cluster_shift<0)status=Decrease_inode_allocated_clusters(the_inode,-cluster_shift);
    return status;
}
int init_fs_t::Increase_inode_allocated_clusters(Inode &the_inode, uint64_t Increase_clusters_count)
{
    if(the_inode.flags.extents_or_indextable == INDEX_TABLE_TYPE){
        
    }else{

    }
}
int init_fs_t::write_inode(uint64_t block_group_index, uint64_t inode_index, Inode *inode)
{

    // 检查块组索引是否有效
    if (block_group_index >= fs_metainf->total_blocks_group_valid) {
        return -1;
    }
    int status;
SuperCluster* sc = get_supercluster(block_group_index);
    if (sc ==nullptr|| sc->magic != SUPER_CLUSTER_MAGIT) {
        return -1;
    }

    // 计算该块组中inode数组的最大索引
    uint64_t inodes_per_cluster = fs_metainf->cluster_size / fs_metainf->inode_size;
    if (inodes_per_cluster == 0) {
        return -1;
    }
    uint64_t max_inode_index = static_cast<uint64_t>(sc->inodes_array_cluster_count) * inodes_per_cluster;
    if (inode_index >= max_inode_index) {
        // 仅在非内存盘时释放SuperCluster
        return -1;
    }

    // 计算目标inode在磁盘上的具体位置
    uint64_t inode_offset = inode_index * fs_metainf->inode_size;
    uint64_t target_block = (sc->inodes_array_first_cluster * fs_metainf->cluster_block_count + inode_offset) / fs_metainf->block_size;
    uint32_t offset_in_block = inode_offset % fs_metainf->block_size;

    // 使用物理层接口将inode数据写入指定位置
    if (is_memdiskv1) {
        // 修正：直接使用块索引获取基址，再添加块内偏移
        void* dest = (uint8_t*)memdiskv1_blockdevice->get_vaddr(target_block) + offset_in_block;
        ksystemramcpy(inode, dest, fs_metainf->inode_size);
        status = 0;
    } else {
        status = phylayer->write(target_block, offset_in_block, inode, fs_metainf->inode_size);
    }
    return status;
}

int init_fs_t::get_inode(uint64_t block_group_index, uint64_t inode_index, Inode &inode)
{
   if(block_group_index >= fs_metainf->total_blocks_group_valid)return OS_INVALID_PARAMETER;
   SuperCluster* sc = get_supercluster(block_group_index);
   if(sc->magic != SUPER_CLUSTER_MAGIT)return OS_FILE_SYSTEM_DAMAGED;
   if(inode_index >= sc->inodes_count_max)return OS_INVALID_PARAMETER;
   if(is_memdiskv1)
   {
    Inode* inode_base=reinterpret_cast<Inode*>(reinterpret_cast<uint8_t*>(fs_metainf)+
        sc->inodes_array_first_cluster*fs_metainf->cluster_size
    );
    inode=inode_base[inode_index];
   }else{
    Inode indoe_tmp;
    phylayer->read(sc->inodes_array_first_cluster*fs_metainf->cluster_block_count
        ,inode_index*fs_metainf->inode_size,
        &indoe_tmp,
        fs_metainf->inode_size);
        inode=indoe_tmp;
   }
    return OS_SUCCESS;
}
/**写入inode指向的数据流的内容，
 *参数:
 *  the_inode: inode对象
 *  stream_base_offset: 数据流的起始偏移
 *  size: 写入的数据大小
 *  buffer: 写入到的数据存放的缓冲区
 *此函数为内部接口，若超出文件大小会报错
 */
int init_fs_t::inode_content_write(Inode the_inode, uint64_t stream_base_offset, uint64_t size, uint8_t *buffer)
{
    if(size == 0) return OS_SUCCESS;
    if (stream_base_offset + size > the_inode.file_size) return OS_IO_ERROR;
    
    int status;
    uint64_t start_cluster_index = stream_base_offset / fs_metainf->cluster_size;
    uint32_t in_cluster_start_offset = stream_base_offset % fs_metainf->cluster_size;
    uint32_t first_cluster_data_size = fs_metainf->cluster_size - in_cluster_start_offset;
    uint64_t stream_end = stream_base_offset + size;
    uint64_t end_cluster_index = stream_end / fs_metainf->cluster_size;
    uint32_t in_cluster_end_offset = stream_end % fs_metainf->cluster_size; 
    uint64_t cluster_scanner = start_cluster_index;
    uint64_t buffer_byte_scanner = 0;
    
    if(the_inode.flags.extents_or_indextable == INDEX_TABLE_TYPE)
    {
        // 处理初始簇数据流的逻辑
        uint64_t first_cluster_in_fs;
        status = filecluster_to_fscluster_in_idxtb(
            the_inode,
            start_cluster_index,
            first_cluster_in_fs
        );
        
        if(is_memdiskv1)
        {
            ksystemramcpy(
                buffer,
                memdiskv1_blockdevice->get_vaddr(first_cluster_in_fs * fs_metainf->cluster_block_count + in_cluster_start_offset),
                first_cluster_data_size
            );
        }
        else
        {
            status = phylayer->write(
                first_cluster_in_fs * fs_metainf->cluster_block_count,
                in_cluster_start_offset,
                buffer,
                first_cluster_data_size
            );
            if(status != OS_SUCCESS) return status;
        }
        
        buffer_byte_scanner += first_cluster_data_size;
        cluster_scanner++;
        
        while(cluster_scanner <= end_cluster_index)
        {
            if(cluster_scanner == end_cluster_index)
            {
                uint64_t last_cluster_in_absolute;
                status = filecluster_to_fscluster_in_idxtb(
                    the_inode,
                    cluster_scanner,
                    last_cluster_in_absolute
                );
                
                if(is_memdiskv1)
                {
                    ksystemramcpy(
                        buffer + buffer_byte_scanner,
                        memdiskv1_blockdevice->get_vaddr(last_cluster_in_absolute * fs_metainf->cluster_block_count),
                        in_cluster_end_offset
                    );
                }
                else
                {
                    status = phylayer->write(
                        last_cluster_in_absolute * fs_metainf->cluster_block_count,
                        0,
                        buffer + buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                    if(status != OS_SUCCESS) return status;
                }
                
                cluster_scanner++;
                continue;
            }
            
            if(cluster_scanner < LEVLE1_INDIRECT_START_CLUSTER_INDEX)
            {
                if(is_memdiskv1)
                {
                    while(cluster_scanner < LEVLE1_INDIRECT_START_CLUSTER_INDEX && cluster_scanner <= end_cluster_index)
                    {
                        ksystemramcpy(
                            buffer + buffer_byte_scanner,
                            memdiskv1_blockdevice->get_vaddr(the_inode.data_desc.index_table.direct_pointers[cluster_scanner] * fs_metainf->cluster_block_count),
                            fs_metainf->cluster_size
                        );
                        buffer_byte_scanner += fs_metainf->cluster_size;
                        cluster_scanner++;
                    }
                }
                else
                {
                    while(cluster_scanner < LEVLE1_INDIRECT_START_CLUSTER_INDEX && cluster_scanner <= end_cluster_index)
                    {
                        status = phylayer->writeblk(
                            the_inode.data_desc.index_table.direct_pointers[cluster_scanner] * fs_metainf->cluster_block_count,
                            fs_metainf->cluster_block_count,
                            buffer + buffer_byte_scanner
                        );
                        if (status != OS_SUCCESS)
                        {
                            return status;
                        }
                        buffer_byte_scanner += fs_metainf->cluster_size;
                        cluster_scanner++;
                    }
                }
                continue;
            } 
            
            if(cluster_scanner < LEVEL2_INDIRECT_START_CLUSTER_INDEX)
            {
                uint64_t stage_end_cluster_index = end_cluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX ? end_cluster_index : LEVEL2_INDIRECT_START_CLUSTER_INDEX;
                status = inode_level1_idiwrite(
                    the_inode.data_desc.index_table.single_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer + buffer_byte_scanner
                );
                if(status != OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner = stage_end_cluster_index;
                buffer_byte_scanner = in_cluster_start_offset + stage_end_cluster_index * fs_metainf->cluster_size;
            }
            
            if (cluster_scanner < LEVEL3_INDIRECT_START_CLUSTER_INDEX)
            {
                uint64_t stage_end_cluster_index = end_cluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX ? end_cluster_index : LEVEL3_INDIRECT_START_CLUSTER_INDEX;
                status = inode_level2_idiwrite(
                    the_inode.data_desc.index_table.double_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer + buffer_byte_scanner
                );
                if(status != OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner = stage_end_cluster_index;
                buffer_byte_scanner = in_cluster_start_offset + stage_end_cluster_index * fs_metainf->cluster_size;
            }
            
            if(cluster_scanner < LEVEL4_INDIRECT_START_CLUSTER_INDEX)
            {
                uint64_t stage_end_cluster_index = end_cluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX ? end_cluster_index : LEVEL4_INDIRECT_START_CLUSTER_INDEX;
                status = inode_level3_idiwrite(
                    the_inode.data_desc.index_table.triple_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer + buffer_byte_scanner
                );
                if(status != OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner = stage_end_cluster_index;
                buffer_byte_scanner = in_cluster_start_offset + stage_end_cluster_index * fs_metainf->cluster_size;
            }
        }
    }
    else
    { 
        if(is_memdiskv1)
        {
            while (true)
            {
                if(cluster_scanner == start_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_start_cluster;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_start_cluster,
                        extents_entry
                    );
                    
                    ksystemramcpy(
                        buffer,
                        memdiskv1_blockdevice->get_vaddr(fs_start_cluster * fs_metainf->cluster_block_count + in_cluster_start_offset),
                        first_cluster_data_size
                    );
                    
                    buffer_byte_scanner += first_cluster_data_size;
                    cluster_scanner++;
                }
                
                if(cluster_scanner == end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_end_cluster;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_end_cluster,
                        extents_entry
                    );
                    
                    ksystemramcpy(
                        buffer + buffer_byte_scanner,
                        memdiskv1_blockdevice->get_vaddr(fs_end_cluster * fs_metainf->cluster_block_count),
                        in_cluster_end_offset
                    );
                    break;
                }
                
                if (start_cluster_index < cluster_scanner && cluster_scanner < end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_scanner_index;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_scanner_index,
                        extents_entry
                    );
                    
                    if(status != OS_SUCCESS) return status;   
                    
                    bool is_this_seg_contain_end_cluster = (end_cluster_index - cluster_scanner)
                        < (extents_entry.first_cluster_index + extents_entry.length_in_clusters - fs_scanner_index);
                    
                    uint64_t write_cluster_count;
                    
                    if(is_this_seg_contain_end_cluster)
                    {
                        write_cluster_count = end_cluster_index - cluster_scanner;
                    }
                    else
                    {
                        write_cluster_count = extents_entry.first_cluster_index + extents_entry.length_in_clusters - fs_scanner_index;
                    }
                    
                    ksystemramcpy(
                        buffer + buffer_byte_scanner,
                        memdiskv1_blockdevice->get_vaddr(fs_scanner_index * fs_metainf->cluster_block_count),
                        fs_metainf->cluster_size * write_cluster_count
                    );
                    
                    buffer_byte_scanner += fs_metainf->cluster_size * write_cluster_count;
                    cluster_scanner += write_cluster_count;
                }
            }
        }
        else
        { 
            while (true)
            {
                if(cluster_scanner == start_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_start_cluster;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_start_cluster,
                        extents_entry
                    );
                    
                    status = phylayer->write(
                        fs_start_cluster * fs_metainf->cluster_block_count,
                        in_cluster_start_offset,
                        buffer,
                        first_cluster_data_size
                    );
                    
                    if(status != OS_SUCCESS) return status;
                    
                    buffer_byte_scanner += first_cluster_data_size;
                    cluster_scanner++;
                }
                
                if(cluster_scanner == end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_end_cluster;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_end_cluster,
                        extents_entry
                    );
                    
                    status = phylayer->write(
                        fs_end_cluster * fs_metainf->cluster_block_count,
                        0,
                        buffer + buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                    
                    if(status != OS_SUCCESS) return status;
                    
                    cluster_scanner++;
                    buffer_byte_scanner += in_cluster_end_offset;
                    break;
                }
                
                if (start_cluster_index < cluster_scanner && cluster_scanner < end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_scanner_index;
                    status = filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_scanner_index,
                        extents_entry
                    );
                    
                    if(status != OS_SUCCESS) return status;   
                    
                    bool is_this_seg_contain_end_cluster = (end_cluster_index - cluster_scanner)
                        < (extents_entry.first_cluster_index + extents_entry.length_in_clusters - fs_scanner_index);
                    
                    uint64_t write_cluster_count;
                    
                    if(is_this_seg_contain_end_cluster)
                    {
                        write_cluster_count = end_cluster_index - cluster_scanner;
                    }
                    else
                    {
                        write_cluster_count = extents_entry.first_cluster_index + extents_entry.length_in_clusters - fs_scanner_index;
                    }
                    
                    status = phylayer->write(
                        fs_scanner_index * fs_metainf->cluster_block_count,
                        0,
                        buffer + buffer_byte_scanner,
                        fs_metainf->cluster_size * write_cluster_count
                    );
                    
                    if (status != OS_SUCCESS) return status;
                    
                    buffer_byte_scanner += fs_metainf->cluster_size * write_cluster_count;
                    cluster_scanner += write_cluster_count;
                }
            }
        }
    }
    
    return OS_SUCCESS;
}

int init_fs_t::inode_level1_idiwrite(uint64_t rootCluster_of_lv1_index, uint64_t fsize, 
                                     uint64_t datastream_start_logical_clst, 
                                     uint64_t datastream_end_logical_clst, 
                                     uint8_t *buffer)
{
    // 参数校验
    if (datastream_start_logical_clst < LEVLE1_INDIRECT_START_CLUSTER_INDEX ||
        datastream_end_logical_clst >= LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
        return OS_INVALID_PARAMETER;
    }
    
    if (datastream_start_logical_clst > datastream_end_logical_clst) {
        return OS_INVALID_PARAMETER;
    }
    
    // 计算文件占用的总簇数（向上取整）
    uint64_t total_clusters = (fsize + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
    if (datastream_end_logical_clst >= total_clusters) {
        return OS_INVALID_PARAMETER;
    }

    int status = OS_SUCCESS;
    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
    uint64_t cluster_scanner = datastream_start_logical_clst;
    
    // 计算需要写入的簇数量
    uint64_t clusters_to_write = datastream_end_logical_clst - datastream_start_logical_clst + 1;
    
    if (is_memdiskv1) {
        // 内存磁盘路径 - 直接使用虚拟地址访问
        uint64_t* lv0_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
            rootCluster_of_lv1_index * fs_metainf->cluster_block_count);
        
        if (lv0_cluster_entries == nullptr) {
            return OS_FILE_SYSTEM_DAMAGED;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv0_cluster_entries[i] == 0) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            // 获取数据簇的虚拟地址
            void* data_cluster_addr = memdiskv1_blockdevice->get_vaddr(
                lv0_cluster_entries[i] * fs_metainf->cluster_block_count);
            
            if (data_cluster_addr == nullptr) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            // 复制数据到内存盘
            ksystemramcpy(
                buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size,
                data_cluster_addr,
                fs_metainf->cluster_size
            );
            
            cluster_scanner++;
            clusters_to_write--;
        }
    } else {
        // 物理磁盘路径 - 使用块设备接口
        uint64_t* lv0_cluster_entries = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(
            rootCluster_of_lv1_index * fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv0_cluster_entries
        );
        
        if (status != OS_SUCCESS) {
            delete[] lv0_cluster_entries;
            return status;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv0_cluster_entries[i] == 0) {
                delete[] lv0_cluster_entries;
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            // 写入数据到物理磁盘
            status = phylayer->writeblk(
                lv0_cluster_entries[i] * fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size
            );
            
            if (status != OS_SUCCESS) {
                delete[] lv0_cluster_entries;
                return status;
            }
            
            cluster_scanner++;
            clusters_to_write--;
        }
        
        delete[] lv0_cluster_entries;
    }
    
    // 检查是否成功写入所有簇
    if (clusters_to_write > 0) {
        return OS_FILE_SYSTEM_DAMAGED; // 索引不足
    }
    
    return OS_SUCCESS;
}

int init_fs_t::inode_level2_idiwrite(uint64_t rootCluster_of_lv2_index, uint64_t fsize, 
                                     uint64_t datastream_start_logical_clst, 
                                     uint64_t datastream_end_logical_clst, 
                                     uint8_t *buffer)
{
    // 参数校验
    if (datastream_start_logical_clst < LEVEL2_INDIRECT_START_CLUSTER_INDEX ||
        datastream_end_logical_clst >= LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
        return OS_INVALID_PARAMETER;
    }
    
    if (datastream_start_logical_clst > datastream_end_logical_clst) {
        return OS_INVALID_PARAMETER;
    }
    
    // 计算文件占用的总簇数（向上取整）
    uint64_t total_clusters = (fsize + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
    if (datastream_end_logical_clst >= total_clusters) {
        return OS_INVALID_PARAMETER;
    }

    int status = OS_SUCCESS;
    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
    uint64_t cluster_scanner = datastream_start_logical_clst;
    
    // 计算需要写入的簇数量
    uint64_t clusters_to_write = datastream_end_logical_clst - datastream_start_logical_clst + 1;
    
    if (is_memdiskv1) {
        // 内存磁盘路径 - 直接使用虚拟地址访问
        uint64_t* lv1_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
            rootCluster_of_lv2_index * fs_metainf->cluster_block_count);
        
        if (lv1_cluster_entries == nullptr) {
            return OS_FILE_SYSTEM_DAMAGED;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv1_cluster_entries[i] == 0) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            uint64_t* lv0_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                lv1_cluster_entries[i] * fs_metainf->cluster_block_count);
            
            if (lv0_cluster_entries == nullptr) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            for (uint32_t j = 0; j < MaxClusterEntryCount && clusters_to_write > 0; j++) {
                if (lv0_cluster_entries[j] == 0) {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                // 获取数据簇的虚拟地址
                void* data_cluster_addr = memdiskv1_blockdevice->get_vaddr(
                    lv0_cluster_entries[j] * fs_metainf->cluster_block_count);
                
                if (data_cluster_addr == nullptr) {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                // 复制数据到内存盘
                ksystemramcpy(
                    buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size,
                    data_cluster_addr,
                    fs_metainf->cluster_size
                );
                
                cluster_scanner++;
                clusters_to_write--;
            }
        }
    } else {
        // 物理磁盘路径 - 使用块设备接口
        uint64_t* lv1_cluster_entries = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(
            rootCluster_of_lv2_index * fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv1_cluster_entries
        );
        
        if (status != OS_SUCCESS) {
            delete[] lv1_cluster_entries;
            return status;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv1_cluster_entries[i] == 0) {
                delete[] lv1_cluster_entries;
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            uint64_t* lv0_cluster_entries = new uint64_t[MaxClusterEntryCount];
            status = phylayer->readblk(
                lv1_cluster_entries[i] * fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                lv0_cluster_entries
            );
            
            if (status != OS_SUCCESS) {
                delete[] lv0_cluster_entries;
                delete[] lv1_cluster_entries;
                return status;
            }
            
            for (uint32_t j = 0; j < MaxClusterEntryCount && clusters_to_write > 0; j++) {
                if (lv0_cluster_entries[j] == 0) {
                    delete[] lv0_cluster_entries;
                    delete[] lv1_cluster_entries;
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                // 写入数据到物理磁盘
                status = phylayer->writeblk(
                    lv0_cluster_entries[j] * fs_metainf->cluster_block_count,
                    fs_metainf->cluster_block_count,
                    buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size
                );
                
                if (status != OS_SUCCESS) {
                    delete[] lv0_cluster_entries;
                    delete[] lv1_cluster_entries;
                    return status;
                }
                
                cluster_scanner++;
                clusters_to_write--;
            }
            
            delete[] lv0_cluster_entries;
        }
        
        delete[] lv1_cluster_entries;
    }
    
    // 检查是否成功写入所有簇
    if (clusters_to_write > 0) {
        return OS_FILE_SYSTEM_DAMAGED; // 索引不足
    }
    
    return OS_SUCCESS;
}

int init_fs_t::inode_level3_idiwrite(uint64_t rootCluster_of_lv3_index, uint64_t fsize, 
                                     uint64_t datastream_start_logical_clst, 
                                     uint64_t datastream_end_logical_clst, 
                                     uint8_t *buffer)
{
    // 参数校验
    if (datastream_start_logical_clst < LEVEL3_INDIRECT_START_CLUSTER_INDEX ||
        datastream_end_logical_clst >= LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
        return OS_INVALID_PARAMETER;
    }
    
    if (datastream_start_logical_clst > datastream_end_logical_clst) {
        return OS_INVALID_PARAMETER;
    }
    
    // 计算文件占用的总簇数（向上取整）
    uint64_t total_clusters = (fsize + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
    if (datastream_end_logical_clst >= total_clusters) {
        return OS_INVALID_PARAMETER;
    }

    int status = OS_SUCCESS;
    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
    uint64_t cluster_scanner = datastream_start_logical_clst;
    
    // 计算需要写入的簇数量
    uint64_t clusters_to_write = datastream_end_logical_clst - datastream_start_logical_clst + 1;
    
    if (is_memdiskv1) {
        // 内存磁盘路径 - 直接使用虚拟地址访问
        uint64_t* lv2_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
            rootCluster_of_lv3_index * fs_metainf->cluster_block_count);
        
        if (lv2_cluster_entries == nullptr) {
            return OS_FILE_SYSTEM_DAMAGED;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv2_cluster_entries[i] == 0) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            uint64_t* lv1_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                lv2_cluster_entries[i] * fs_metainf->cluster_block_count);
            
            if (lv1_cluster_entries == nullptr) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            for (uint32_t j = 0; j < MaxClusterEntryCount && clusters_to_write > 0; j++) {
                if (lv1_cluster_entries[j] == 0) {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                uint64_t* lv0_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                    lv1_cluster_entries[j] * fs_metainf->cluster_block_count);
                
                if (lv0_cluster_entries == nullptr) {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                for (uint32_t k = 0; k < MaxClusterEntryCount && clusters_to_write > 0; k++) {
                    if (lv0_cluster_entries[k] == 0) {
                        return OS_FILE_SYSTEM_DAMAGED;
                    }
                    
                    // 获取数据簇的虚拟地址
                    void* data_cluster_addr = memdiskv1_blockdevice->get_vaddr(
                        lv0_cluster_entries[k] * fs_metainf->cluster_block_count);
                    
                    if (data_cluster_addr == nullptr) {
                        return OS_FILE_SYSTEM_DAMAGED;
                    }
                    
                    // 复制数据到内存盘
                    ksystemramcpy(
                        buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size,
                        data_cluster_addr,
                        fs_metainf->cluster_size
                    );
                    
                    cluster_scanner++;
                    clusters_to_write--;
                }
            }
        }
    } else {
        // 物理磁盘路径 - 使用块设备接口
        uint64_t* lv2_cluster_entries = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(
            rootCluster_of_lv3_index * fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv2_cluster_entries
        );
        
        if (status != OS_SUCCESS) {
            delete[] lv2_cluster_entries;
            return status;
        }
        
        for (uint32_t i = 0; i < MaxClusterEntryCount && clusters_to_write > 0; i++) {
            if (lv2_cluster_entries[i] == 0) {
                delete[] lv2_cluster_entries;
                return OS_FILE_SYSTEM_DAMAGED;
            }
            
            uint64_t* lv1_cluster_entries = new uint64_t[MaxClusterEntryCount];
            status = phylayer->readblk(
                lv2_cluster_entries[i] * fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                lv1_cluster_entries
            );
            
            if (status != OS_SUCCESS) {
                delete[] lv1_cluster_entries;
                delete[] lv2_cluster_entries;
                return status;
            }
            
            for (uint32_t j = 0; j < MaxClusterEntryCount && clusters_to_write > 0; j++) {
                if (lv1_cluster_entries[j] == 0) {
                    delete[] lv1_cluster_entries;
                    delete[] lv2_cluster_entries;
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                
                uint64_t* lv0_cluster_entries = new uint64_t[MaxClusterEntryCount];
                status = phylayer->readblk(
                    lv1_cluster_entries[j] * fs_metainf->cluster_block_count,
                    fs_metainf->cluster_block_count,
                    lv0_cluster_entries
                );
                
                if (status != OS_SUCCESS) {
                    delete[] lv0_cluster_entries;
                    delete[] lv1_cluster_entries;
                    delete[] lv2_cluster_entries;
                    return status;
                }
                
                for (uint32_t k = 0; k < MaxClusterEntryCount && clusters_to_write > 0; k++) {
                    if (lv0_cluster_entries[k] == 0) {
                        delete[] lv0_cluster_entries;
                        delete[] lv1_cluster_entries;
                        delete[] lv2_cluster_entries;
                        return OS_FILE_SYSTEM_DAMAGED;
                    }
                    
                    // 写入数据到物理磁盘
                    status = phylayer->writeblk(
                        lv0_cluster_entries[k] * fs_metainf->cluster_block_count,
                        fs_metainf->cluster_block_count,
                        buffer + (cluster_scanner - datastream_start_logical_clst) * fs_metainf->cluster_size
                    );
                    
                    if (status != OS_SUCCESS) {
                        delete[] lv0_cluster_entries;
                        delete[] lv1_cluster_entries;
                        delete[] lv2_cluster_entries;
                        return status;
                    }
                    
                    cluster_scanner++;
                    clusters_to_write--;
                }
                
                delete[] lv0_cluster_entries;
            }
            
            delete[] lv1_cluster_entries;
        }
        
        delete[] lv2_cluster_entries;
    }
    
    // 检查是否成功写入所有簇
    if (clusters_to_write > 0) {
        return OS_FILE_SYSTEM_DAMAGED; // 索引不足
    }
    
    return OS_SUCCESS;
}
/**读取inode指向的数据流的内容，
 *参数:
 *  the_inode: inode对象
 *  stream_base_offset: 数据流的起始偏移
 *  size: 读取的数据大小
 *  buffer: 读取到的数据存放的缓冲区
 *此函数为内部接口，若超出文件大小会报错
 */
int init_fs_t::inode_content_read(Inode the_inode, uint64_t stream_base_offset, uint64_t size, uint8_t *buffer)
{   
    if(size==0)return OS_SUCCESS;
    if (stream_base_offset + size > the_inode.file_size) return OS_IO_ERROR;
    int status;
    uint64_t start_cluster_index=stream_base_offset/fs_metainf->cluster_size;
    uint32_t in_cluster_start_offset=stream_base_offset%fs_metainf->cluster_size;
    uint32_t first_cluster_data_size=fs_metainf->cluster_size-in_cluster_start_offset;
    uint64_t stream_end=stream_base_offset+size;
    uint64_t end_cluster_index=stream_end/fs_metainf->cluster_size;
    uint32_t in_cluster_end_offset=stream_end%fs_metainf->cluster_size; 
    uint64_t cluster_scanner=start_cluster_index;
    uint64_t buffer_byte_scanner=0;
    if(the_inode.flags.extents_or_indextable==INDEX_TABLE_TYPE)
    {
        
        //处理初始簇数据流的逻辑
        uint64_t first_cluster_in_fs;
        status=filecluster_to_fscluster_in_idxtb(
            the_inode,
            start_cluster_index,
            first_cluster_in_fs
        );
        phylayer->read(
            first_cluster_in_fs*fs_metainf->cluster_block_count,
            in_cluster_start_offset,
            buffer,
            first_cluster_data_size
        );
        buffer_byte_scanner+=first_cluster_data_size;
        cluster_scanner++;
        while(cluster_scanner<=end_cluster_index)
        {
            
            if(cluster_scanner==end_cluster_index)
            {
                uint64_t last_cluster_in_absolute;
                status=filecluster_to_fscluster_in_idxtb(
                    the_inode,
                    cluster_scanner,
                    last_cluster_in_absolute
                );
                if(is_memdiskv1)
                {
                    ksystemramcpy(
                        memdiskv1_blockdevice->get_vaddr(last_cluster_in_absolute*fs_metainf->cluster_block_count),
                        buffer+buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                }else{
                    status=phylayer->read(
                        last_cluster_in_absolute*fs_metainf->cluster_block_count,
                        0,
                        buffer+buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                    if(status!=OS_SUCCESS)return status;
                }
                cluster_scanner++;
                continue;
            }
            if(cluster_scanner<LEVLE1_INDIRECT_START_CLUSTER_INDEX)
            {
                if(is_memdiskv1)
                {
                    while(cluster_scanner<LEVLE1_INDIRECT_START_CLUSTER_INDEX&&cluster_scanner<=end_cluster_index)
                    {
                        ksystemramcpy(
                            memdiskv1_blockdevice->get_vaddr(the_inode.data_desc.index_table.direct_pointers[cluster_scanner]*fs_metainf->cluster_block_count),
                            buffer+buffer_byte_scanner,
                            fs_metainf->cluster_size
                        );
                        buffer_byte_scanner+=fs_metainf->cluster_size;
                        cluster_scanner++;
                    }
                }else{
                    while(cluster_scanner<LEVLE1_INDIRECT_START_CLUSTER_INDEX&&cluster_scanner<=end_cluster_index)
                    {
                        status= phylayer->readblk(
                            the_inode.data_desc.index_table.direct_pointers[cluster_scanner]*fs_metainf->cluster_block_count,
                            fs_metainf->cluster_block_count,
                            buffer+buffer_byte_scanner
                        );
                        if (status!=OS_SUCCESS)
                        {
                            return status;
                        }
                        buffer_byte_scanner+=fs_metainf->cluster_size;
                        cluster_scanner++;
                    }
                }
                continue;
            } 
            if(cluster_scanner<LEVEL2_INDIRECT_START_CLUSTER_INDEX)
            /*
            这种情况下所需要的接口是内含簇引索区间合法性校验，在指定区间内读取数据流到指定缓冲区的接口
            */
            {
                uint64_t stage_end_cluster_index=end_cluster_index<LEVEL2_INDIRECT_START_CLUSTER_INDEX?end_cluster_index:LEVEL2_INDIRECT_START_CLUSTER_INDEX;
                status=inode_level1_idiread(
                    the_inode.data_desc.index_table.single_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer+buffer_byte_scanner
                );
                if(status!=OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner=stage_end_cluster_index;
                buffer_byte_scanner=in_cluster_start_offset+stage_end_cluster_index*fs_metainf->cluster_size;
            }
            if (cluster_scanner<LEVEL3_INDIRECT_START_CLUSTER_INDEX)
            {
                uint64_t stage_end_cluster_index=end_cluster_index<LEVEL3_INDIRECT_START_CLUSTER_INDEX?end_cluster_index:LEVEL3_INDIRECT_START_CLUSTER_INDEX;
                status=inode_level2_idiread(
                    the_inode.data_desc.index_table.double_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer+buffer_byte_scanner
                );
                if(status!=OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner=stage_end_cluster_index;
                buffer_byte_scanner=in_cluster_start_offset+stage_end_cluster_index*fs_metainf->cluster_size;
            }
            if(cluster_scanner<LEVEL4_INDIRECT_START_CLUSTER_INDEX)
            {
                uint64_t stage_end_cluster_index=end_cluster_index<LEVEL4_INDIRECT_START_CLUSTER_INDEX?end_cluster_index:LEVEL4_INDIRECT_START_CLUSTER_INDEX;
                status=inode_level3_idiread(
                    the_inode.data_desc.index_table.triple_indirect_pointer,
                    the_inode.file_size,
                    cluster_scanner,
                    stage_end_cluster_index,
                    buffer+buffer_byte_scanner
                );
                if(status!=OS_SUCCESS)
                {
                    return status;
                }
                cluster_scanner=stage_end_cluster_index;
                buffer_byte_scanner=in_cluster_start_offset+stage_end_cluster_index*fs_metainf->cluster_size;
            }
            
        }
    }else{ 
        if(is_memdiskv1)
        {
            while (true)
            {
                if(cluster_scanner==start_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_start_cluster;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_start_cluster,
                        extents_entry
                    );
                    ksystemramcpy(
                        memdiskv1_blockdevice->get_vaddr(fs_start_cluster*fs_metainf->cluster_block_count,in_cluster_start_offset),
                        buffer,
                        first_cluster_data_size
                    );
                    buffer_byte_scanner+=first_cluster_data_size;
                    cluster_scanner++;
                }
                if(cluster_scanner==end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_end_cluster;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_end_cluster,
                        extents_entry
                    );
                    ksystemramcpy(
                        memdiskv1_blockdevice->get_vaddr(fs_end_cluster*fs_metainf->cluster_block_count),
                        buffer+buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                    break;
                }
                if (start_cluster_index<cluster_scanner &&cluster_scanner<end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_scanner_index;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_scanner_index,
                        extents_entry
                    );
                    if(status!=OS_SUCCESS)return status;   
                    bool is_this_seg_contain_end_cluster=(end_cluster_index-cluster_scanner)
                    <(extents_entry.first_cluster_index+extents_entry.length_in_clusters-fs_scanner_index);
                    uint64_t read_cluster_count;
                    if(is_this_seg_contain_end_cluster)
                    {
                        read_cluster_count=end_cluster_index-cluster_scanner;
                    }else{
                        read_cluster_count=extents_entry.first_cluster_index+extents_entry.length_in_clusters-fs_scanner_index;
                    }      ksystemramcpy(
                            memdiskv1_blockdevice->get_vaddr(fs_scanner_index*fs_metainf->cluster_block_count),
                            buffer+buffer_byte_scanner,
                            fs_metainf->cluster_size*read_cluster_count
                        );
                        buffer_byte_scanner+=fs_metainf->cluster_size*read_cluster_count;
                        cluster_scanner+=read_cluster_count;
                }
            }
            
        }
        else
        { 
            while (true)
            {
                if(cluster_scanner==start_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_start_cluster;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_start_cluster,
                        extents_entry
                    );
                    phylayer->read(fs_start_cluster*fs_metainf->cluster_block_count, in_cluster_start_offset, buffer, first_cluster_data_size);
                    buffer_byte_scanner+=first_cluster_data_size;
                    cluster_scanner++;
                }
                if(cluster_scanner==end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_end_cluster;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_end_cluster,
                        extents_entry
                    );
                    phylayer->read(
                        fs_end_cluster*fs_metainf->cluster_block_count,
                        0,
                        buffer+buffer_byte_scanner,
                        in_cluster_end_offset
                    );
                    cluster_scanner++;
                    buffer_byte_scanner+=in_cluster_end_offset;
                    break;
                }
                if (start_cluster_index<cluster_scanner &&cluster_scanner<end_cluster_index)
                {
                    FileExtentsEntry_t extents_entry;
                    uint64_t fs_scanner_index;
                    status=filecluster_to_fscluster_in_extents(
                        the_inode,
                        cluster_scanner,
                        fs_scanner_index,
                        extents_entry
                    );
                    if(status!=OS_SUCCESS)return status;   
                    bool is_this_seg_contain_end_cluster=(end_cluster_index-cluster_scanner)
                    <(extents_entry.first_cluster_index+extents_entry.length_in_clusters-fs_scanner_index);
                    uint64_t read_cluster_count;
                    if(is_this_seg_contain_end_cluster)
                    {
                        read_cluster_count=end_cluster_index-cluster_scanner;
                    }else{
                        read_cluster_count=extents_entry.first_cluster_index+extents_entry.length_in_clusters-fs_scanner_index;
                    }phylayer->read(
                            fs_scanner_index*fs_metainf->cluster_block_count,
                            0,
                            buffer+buffer_byte_scanner,
                            fs_metainf->cluster_size*read_cluster_count
                        );
                        buffer_byte_scanner+=fs_metainf->cluster_size*read_cluster_count;
                        cluster_scanner+=read_cluster_count;
                }
            }
            return OS_SUCCESS;
        }
    }

}

int init_fs_t::inode_level1_idiread(uint64_t rootClutser_of_lv1_index, uint64_t fsize, uint64_t start_cluster_index_of_datastream, uint64_t end_cluster_index_of_datastream, uint8_t *buffer)
{
    if(start_cluster_index_of_datastream<LEVLE1_INDIRECT_START_CLUSTER_INDEX
    ||end_cluster_index_of_datastream>=LEVEL2_INDIRECT_START_CLUSTER_INDEX)return OS_INVALID_PARAMETER;
    if(fsize/fs_metainf->cluster_size<end_cluster_index_of_datastream)return OS_INVALID_PARAMETER;
    int status;
    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
    uint64_t cluster_scanner = start_cluster_index_of_datastream;

    if (is_memdiskv1) {
        uint64_t* lv0_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(rootClutser_of_lv1_index * fs_metainf->cluster_block_count);
        if (lv0_cluster_entries == nullptr) {
            return OS_FILE_SYSTEM_DAMAGED;
        }
        for (uint32_t i = 0; i < MaxClusterEntryCount; i++) {
            if (lv0_cluster_entries[i] == 0) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            if (cluster_scanner < end_cluster_index_of_datastream) {
                ksystemramcpy(
                    (uint8_t*)memdiskv1_blockdevice->get_vaddr(lv0_cluster_entries[i] * fs_metainf->cluster_block_count),
                    buffer + (cluster_scanner - start_cluster_index_of_datastream) * fs_metainf->cluster_size,
                    fs_metainf->cluster_size
                );
                cluster_scanner++;
            } else {
                return OS_SUCCESS;
            }
        }
    } else {
        uint64_t* lv0_cluster_entries = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(
            rootClutser_of_lv1_index * fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv0_cluster_entries
        );
        if (status != OS_SUCCESS) {
            delete[] lv0_cluster_entries;
            return status;
        }
        for (uint32_t i = 0; i < MaxClusterEntryCount; i++) {
            if (lv0_cluster_entries[i] == 0) {
                delete[] lv0_cluster_entries;
                return OS_FILE_SYSTEM_DAMAGED;
            }
            if (cluster_scanner < end_cluster_index_of_datastream) {
                status = phylayer->readblk(
                    lv0_cluster_entries[i] * fs_metainf->cluster_block_count,
                     fs_metainf->cluster_block_count,
                    buffer + (cluster_scanner - start_cluster_index_of_datastream) * fs_metainf->cluster_size
                );
                if (status != OS_SUCCESS) {
                    delete[] lv0_cluster_entries;
                    return status;
                }
                cluster_scanner++;
            } else {
                delete[] lv0_cluster_entries;
                return OS_SUCCESS;
            }
        }
        delete[] lv0_cluster_entries;
    }
    
    return OS_BAD_FUNCTION;
}

int init_fs_t::inode_level2_idiread(uint64_t rootClutser_of_lv1_index, uint64_t fsize, uint64_t start_cluster_index_of_datastream, uint64_t end_cluster_index_of_datastream, uint8_t *buffer)
{
        if(start_cluster_index_of_datastream<LEVEL2_INDIRECT_START_CLUSTER_INDEX
    ||end_cluster_index_of_datastream>=LEVEL3_INDIRECT_START_CLUSTER_INDEX)return OS_INVALID_PARAMETER;
    if(fsize/fs_metainf->cluster_size<end_cluster_index_of_datastream)return OS_INVALID_PARAMETER;
    int status;
    uint32_t MaxClusterEntryCount = fs_metainf->cluster_size / sizeof(uint64_t);
    uint64_t cluster_scanner = start_cluster_index_of_datastream;

    if (is_memdiskv1) {
        uint64_t* lv1_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(rootClutser_of_lv1_index * fs_metainf->cluster_block_count);
        if (lv1_cluster_entries == nullptr) {
            return OS_FILE_SYSTEM_DAMAGED;
        }
        for (uint32_t i = 0; i < MaxClusterEntryCount; i++) {
            uint64_t* lv0_cluster_entries = (uint64_t*)memdiskv1_blockdevice->get_vaddr(lv1_cluster_entries[i] * fs_metainf->cluster_block_count);
            if (lv0_cluster_entries == nullptr) {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            for (uint32_t j = 0; j < MaxClusterEntryCount; j++) {
                if (lv0_cluster_entries[j] == 0) {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                if (cluster_scanner < end_cluster_index_of_datastream) {
                    ksystemramcpy(
                        (uint8_t*)memdiskv1_blockdevice->get_vaddr(lv0_cluster_entries[j] * fs_metainf->cluster_block_count),
                        buffer + (cluster_scanner - start_cluster_index_of_datastream) * fs_metainf->cluster_size,
                        fs_metainf->cluster_size
                    );
                    cluster_scanner++;
                } else {
                    return OS_SUCCESS;
                }
            }
        }
    } else {
        uint64_t* lv1_cluster_entries = new uint64_t[MaxClusterEntryCount];
        status = phylayer->readblk(
            rootClutser_of_lv1_index * fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv1_cluster_entries
        );
        if (status != OS_SUCCESS) {
            delete[] lv1_cluster_entries;
            return status;
        }
        for (uint32_t i = 0; i < MaxClusterEntryCount; i++) {
            uint64_t* lv0_cluster_entries = new uint64_t[MaxClusterEntryCount];
            status = phylayer->readblk(
                lv1_cluster_entries[i] * fs_metainf->cluster_block_count,
                1,
                lv0_cluster_entries
            );
            if (status != OS_SUCCESS) {
                delete[] lv0_cluster_entries;
                delete[] lv1_cluster_entries;
                return status;
            }
            for (uint32_t j = 0; j < MaxClusterEntryCount; j++) {
                if (cluster_scanner < end_cluster_index_of_datastream) {
                    status = phylayer->readblk(
                        lv0_cluster_entries[j] * fs_metainf->cluster_block_count,
                        fs_metainf->cluster_block_count,
                        buffer + (cluster_scanner - start_cluster_index_of_datastream) * fs_metainf->cluster_size
                    );
                    if (status != OS_SUCCESS) {
                        delete[] lv0_cluster_entries;
                        delete[] lv1_cluster_entries;
                        return status;
                    }
                    cluster_scanner++;
                } else {
                    delete[] lv0_cluster_entries;
                    delete[] lv1_cluster_entries;
                    return OS_SUCCESS;
                }
            }
            delete[] lv0_cluster_entries;
        }
        delete[] lv1_cluster_entries;
    }
    
    return OS_BAD_FUNCTION;
}

int init_fs_t::inode_level3_idiread(uint64_t rootClutser_of_lv3_index, uint64_t fsize, uint64_t start_cluster_index_of_datastream, uint64_t end_cluster_index_of_datastream, uint8_t *buffer)
{     if(start_cluster_index_of_datastream<LEVEL3_INDIRECT_START_CLUSTER_INDEX
    ||end_cluster_index_of_datastream>=LEVEL4_INDIRECT_START_CLUSTER_INDEX)return OS_INVALID_PARAMETER;
    if(fsize/fs_metainf->cluster_size<end_cluster_index_of_datastream)return OS_INVALID_PARAMETER;
    int status;
    uint32_t MaxClusterEntryCount=fs_metainf->cluster_size/sizeof(uint64_t);
    //引索合法性校验
    uint64_t cluster_scanner=start_cluster_index_of_datastream;
    if(is_memdiskv1){
        uint64_t*lv2_cluster_entries=(uint64_t*)memdiskv1_blockdevice->get_vaddr(rootClutser_of_lv3_index*fs_metainf->cluster_block_count);
        for(uint32_t i=0;i<MaxClusterEntryCount;i++)
        {
            uint64_t*lv1_cluster_entries=(uint64_t*)memdiskv1_blockdevice->get_vaddr(lv2_cluster_entries[i]*fs_metainf->cluster_block_count);
            if(lv1_cluster_entries==nullptr)
            {
                return OS_FILE_SYSTEM_DAMAGED;
            }
            for(uint32_t j=0;j<MaxClusterEntryCount;j++)
            {
                uint64_t*lv0_cluster_entries=(uint64_t*)memdiskv1_blockdevice->get_vaddr(lv1_cluster_entries[j]*fs_metainf->cluster_block_count);
                if(lv0_cluster_entries==nullptr)
                {
                    return OS_FILE_SYSTEM_DAMAGED;
                }
                for(uint32_t k=0;k<MaxClusterEntryCount;k++)
                {
                    if(lv0_cluster_entries[k]==0)
                    {
                        return OS_FILE_SYSTEM_DAMAGED;
                    }
                    if(cluster_scanner<end_cluster_index_of_datastream){
                    ksystemramcpy(
                        memdiskv1_blockdevice->get_vaddr(lv0_cluster_entries[k] * fs_metainf->cluster_size),
                        buffer+(cluster_scanner-start_cluster_index_of_datastream)*fs_metainf->cluster_size,
                        fs_metainf->cluster_size
                    );
                    cluster_scanner++;
                }else{
                    return OS_SUCCESS;
                }
                }
            }
        }
    }else{
        uint64_t*lv2_cluster_entries=new uint64_t[MaxClusterEntryCount];
        status =phylayer->readblk(
            rootClutser_of_lv3_index*fs_metainf->cluster_block_count,
            1,
            lv2_cluster_entries
        );
        if(status!=OS_SUCCESS){
            delete[] lv2_cluster_entries;
            return status;
        }
        for(uint32_t i=0;i<MaxClusterEntryCount;i++)
        {
            uint64_t*lv1_cluster_entries=new uint64_t[MaxClusterEntryCount];
            status =phylayer->readblk(
                lv2_cluster_entries[i]*fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                lv1_cluster_entries
            );
            if(status!=OS_SUCCESS)
            {
                delete[] lv1_cluster_entries;
                delete[] lv2_cluster_entries;
                return status;
            }
            
            for(uint32_t j=0;j<MaxClusterEntryCount;j++)
            {
                uint64_t*lv0_cluster_entries=new uint64_t[MaxClusterEntryCount];
                status =phylayer->readblk(
                    lv1_cluster_entries[j]*fs_metainf->cluster_block_count,
                    fs_metainf->cluster_block_count,
                    lv0_cluster_entries
                );
                if(status!=OS_SUCCESS)
                {
                    delete[] lv0_cluster_entries;
                    delete[] lv1_cluster_entries;
                    delete[] lv2_cluster_entries;
                    return status;
                }
                for(uint32_t k=0;k<MaxClusterEntryCount;k++)
                {
                    if(cluster_scanner<end_cluster_index_of_datastream)
                    {
                        phylayer->readblk(lv0_cluster_entries[k]*fs_metainf->cluster_block_count,
                            fs_metainf->cluster_block_count,
                            buffer+(cluster_scanner-start_cluster_index_of_datastream)*fs_metainf->cluster_size
                        );
                        cluster_scanner++;
                    }else{
                        return OS_SUCCESS;
                    }
                }
                delete[] lv0_cluster_entries;
            }
            delete[] lv1_cluster_entries; 
        }
    }
    
return OS_BAD_FUNCTION;
}
