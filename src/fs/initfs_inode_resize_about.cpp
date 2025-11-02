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
inline int init_fs_t::Increase_inode_allocated_clusters(
    Inode &the_inode, uint64_t Increase_clusters_count)
{//改变inode所占用的簇
    int status;
    FileExtentsEntry_t* extents_array=nullptr;
    uint64_t extents_entry_count;
    status=clusters_bitmap_alloc(
        Increase_clusters_count,
        extents_array,
        extents_entry_count
    );//是基于base_logical_cluster_index之后分配的
    uint64_t base_logical_cluster_index=the_inode.file_size / fs_metainf->cluster_size;
    if(status!=OS_SUCCESS)return status;
    if(the_inode.flags.extents_or_indextable == INDEX_TABLE_TYPE){
        FileExtents_in_clusterScanner scanner(extents_array, extents_entry_count);
        for(uint64_t i=0;i<Increase_clusters_count;i++)
        {
        status=idxtbmode_set_inode_lcluster_phyclsidx(
            the_inode,
            base_logical_cluster_index + i,
            scanner.convert_to_logical_cluster_index()
        );
        if(status!=OS_SUCCESS){
           delete[] extents_array;
            return status;
        }
        status=scanner.the_next();
        if (status!=OS_SUCCESS)
        {
            delete[] extents_array;
            return status;
        }
        
        }
    }else{
        FileExtentsEntry_t* old_extents_array = nullptr;
        if(is_memdiskv1)
        {
            old_extents_array = (FileExtentsEntry_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.extents.first_cluster_index_of_extents_array * fs_metainf->cluster_block_count
            );
            if(old_extents_array==nullptr){
                return OS_BAD_FUNCTION;
            }
        }else{
            old_extents_array = new FileExtentsEntry_t[fs_metainf->cluster_size / sizeof(FileExtentsEntry_t)];
            status = phylayer->readblk(
                the_inode.data_desc.extents.first_cluster_index_of_extents_array*fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                old_extents_array
            );
            if (status != OS_SUCCESS)
            {
                delete[] old_extents_array;
                return status;
            }
        }
        uint64_t old_count=the_inode.data_desc.extents.entries_count;
        if(old_count+extents_entry_count>fs_metainf->cluster_size/sizeof(FileExtentsEntry_t))
        {
            return OS_NOT_SUPPORT;
        }
        if(old_extents_array[old_count-1].first_cluster_index+
           old_extents_array[old_count-1].length_in_clusters==
           extents_array[0].first_cluster_index)
           {
            old_extents_array[old_count-1].length_in_clusters+=extents_array[0].length_in_clusters;
            ksystemramcpy(
                extents_array+1,
                old_extents_array+old_count,
                (extents_entry_count-1)*sizeof(FileExtentsEntry_t)
            );
           }else{
            ksystemramcpy(
                extents_array,
                old_extents_array+old_count,
                extents_entry_count*sizeof(FileExtentsEntry_t)
            );
           }
           if(!is_memdiskv1)
           {
            phylayer->writeblk(
                the_inode.data_desc.extents.first_cluster_index_of_extents_array*fs_metainf->cluster_block_count,
                fs_metainf->cluster_block_count,
                old_extents_array
            );
            delete[] old_extents_array;
           }
              the_inode.data_desc.extents.entries_count+=extents_entry_count;
    }
    //接下来是根据extents_array正式分配
    for(uint64_t i=0;i<extents_entry_count;i++)
    {
       status=global_set_cluster_bitmap_bits(
            extents_array[i].first_cluster_index,
            extents_array[i].length_in_clusters,
            true
        );
        if(status!=OS_SUCCESS){
           delete[] extents_array;
            return status;
        }
    }
    return status;
}
/**
 * @brief 引索表模式下设置inode的逻辑簇对应的物理簇索引
 * @param the_inode inode对象
 * @param lcluster_index 逻辑簇索引
 * @param phyclsidx 物理簇索引
 * 此函数只用于从下到上扫描建立，
 * 若要填写（n-1）级表的首个索引，
 * 根据上面的逻辑，必然要为其分配相应的簇
 * 以及对应父表信息填写
 */
int init_fs_t::idxtbmode_set_inode_lcluster_phyclsidx(Inode &the_inode, uint64_t lcluster_index, uint64_t phyclsidx)
{//要改逻辑，ai夏季把生成一通
    int status;
    if(0<=lcluster_index&&lcluster_index<LEVLE1_INDIRECT_START_CLUSTER_INDEX)
    {
        the_inode.data_desc.index_table.direct_pointers[lcluster_index]=phyclsidx;
        return OS_SUCCESS;
    }
    if(lcluster_index==LEVLE1_INDIRECT_START_CLUSTER_INDEX)
    {
        // 需要分配一级间接表

        uint64_t new_cluster;
        uint64_t block_group_index;
        int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
        if (ret != OS_SUCCESS) return ret;
            // 标记簇为已用
        ret = global_set_cluster_bitmap_bit(new_cluster, true);
        if (ret != OS_SUCCESS) return ret;
        the_inode.data_desc.index_table.single_indirect_pointer = new_cluster;        
        // 获取一级间接表虚拟地址（转换簇索引为块索引）
        uint64_t block_index = the_inode.data_desc.index_table.single_indirect_pointer * this->fs_metainf->cluster_block_count;
        uint64_t* level1_table;
        if (memdiskv1_blockdevice) {
            // 内存盘模式：直接映射访问
            level1_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index);
            level1_table[0] = phyclsidx;
        } else {
            
            level1_table = new uint64_t[this->fs_metainf->block_size / sizeof(uint64_t)];
            int ret = phylayer->readblk(new_cluster*fs_metainf->cluster_block_count, fs_metainf->cluster_block_count, level1_table);
            if (ret != OS_SUCCESS) return ret;
            
            // 修改索引项
            level1_table[0] = phyclsidx;
            
            ret = phylayer->writeblk(new_cluster*fs_metainf->cluster_block_count, fs_metainf->cluster_block_count, level1_table);
            if (ret != OS_SUCCESS) return ret;
        }
        return OS_SUCCESS;
    }
    if(LEVLE1_INDIRECT_START_CLUSTER_INDEX<lcluster_index&&lcluster_index<LEVEL2_INDIRECT_START_CLUSTER_INDEX)
    {
        uint32_t offset = lcluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
        // 填充一级间接表对应位置
        uint64_t block_index = the_inode.data_desc.index_table.single_indirect_pointer * this->fs_metainf->cluster_block_count;
        uint64_t* level1_table;
        if (memdiskv1_blockdevice) {
            // 内存盘模式
             level1_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index*fs_metainf->cluster_block_count);
            level1_table[offset] = phyclsidx;
        } else {
            uint64_t lv1tbblkidx=the_inode.data_desc.index_table.single_indirect_pointer * this->fs_metainf->cluster_block_count;
            status=phylayer->write(
                lv1tbblkidx,offset*sizeof(uint64_t),
                &phyclsidx,
                sizeof(uint64_t)
            );
            if(status!=OS_SUCCESS)
            {
                return status;
            }
            return OS_SUCCESS;
        }
        return OS_SUCCESS;
    }

    if(LEVEL2_INDIRECT_START_CLUSTER_INDEX<=lcluster_index&&lcluster_index<LEVEL3_INDIRECT_START_CLUSTER_INDEX)
    {
        uint32_t in_seg_offset = lcluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
        uint32_t second_level_index = in_seg_offset>>9;
        uint32_t first_level_index = in_seg_offset % 512;
        
        // 分配二级间接表（首次访问时）
        if (in_seg_offset == 0) {
            uint64_t new_cluster;
            uint64_t block_group_index;
            int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
            if (ret != OS_SUCCESS) return ret;
            ret = global_set_cluster_bitmap_bit(new_cluster, true);
            if (ret != OS_SUCCESS) return ret;
            the_inode.data_desc.index_table.double_indirect_pointer = new_cluster;
        }

        // 分配一级间接表（需要新一级表时）
        if (in_seg_offset % 512 == 0) {
            uint64_t block_index2 = the_inode.data_desc.index_table.double_indirect_pointer * this->fs_metainf->cluster_block_count;
            uint64_t* level2_table=nullptr;
            if (memdiskv1_blockdevice) {
                // 内存盘模式
                level2_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index2);
                uint64_t new_cluster;
                uint64_t block_group_index;
                int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                if (ret != OS_SUCCESS) return ret;
                ret = global_set_cluster_bitmap_bit(new_cluster, true);
                if (ret != OS_SUCCESS) return ret;
                level2_table[second_level_index] = new_cluster;
            } else {
               uint64_t lv2_tb_phyblkidx=the_inode.data_desc.index_table.double_indirect_pointer *fs_metainf->cluster_block_count;
                uint64_t new_cluster;
                uint64_t block_group_index;
                int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                if (ret != OS_SUCCESS) return ret;
                ret = global_set_cluster_bitmap_bit(new_cluster, true);
                if (ret != OS_SUCCESS) return ret;
                status=phylayer->write(
                    lv2_tb_phyblkidx,
                    second_level_index*sizeof(uint64_t),
                    &new_cluster,
                    sizeof(uint64_t)
                );
                if(status!=OS_SUCCESS)
                {
                    return status;
                }
            }
        }
        
        // 逐级解析获取一级间接表并填充
        uint64_t block_index2 = the_inode.data_desc.index_table.double_indirect_pointer * this->fs_metainf->cluster_block_count;
        if (memdiskv1_blockdevice) {
            // 内存盘模式
            uint64_t* level2_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index2);
            uint64_t block_index1 = level2_table[second_level_index] * this->fs_metainf->cluster_block_count;
            uint64_t* level1_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index1);
            level1_table[first_level_index] = phyclsidx;
        } else {
            uint64_t lv2tbblkidx=the_inode.data_desc.index_table.double_indirect_pointer *fs_metainf->cluster_block_count;
            uint64_t lv1tbclsidx=0;
            status=phylayer->write(
                lv2tbblkidx,
                second_level_index*sizeof(uint64_t),
                &lv1tbclsidx,
                sizeof(uint64_t)
            );
            if(status!=OS_SUCCESS) return status;
            uint64_t lv1tbblkidx=fs_metainf->cluster_block_count*lv1tbclsidx;
            status=phylayer->write(
                lv1tbblkidx,
                first_level_index*sizeof(uint64_t),
                &phyclsidx,
                sizeof(uint64_t)
            );if(status!=OS_SUCCESS) return status;
            return OS_SUCCESS;
        }
        return OS_SUCCESS;
    }

    if(LEVEL3_INDIRECT_START_CLUSTER_INDEX<=lcluster_index&&lcluster_index<LEVEL4_INDIRECT_START_CLUSTER_INDEX)
    {
        uint32_t in_seg_offset = lcluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
        uint32_t third_level_index = in_seg_offset>>18;
        uint32_t second_level_index = (in_seg_offset>>9)&511;
        uint32_t first_level_index = in_seg_offset & 511;
        
        // 分配三级间接表（首次访问时）
        if (in_seg_offset == 0 && the_inode.data_desc.index_table.triple_indirect_pointer == 0) {
            uint64_t new_cluster;
            uint64_t block_group_index;
            int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
            if (ret != OS_SUCCESS) return ret;
            ret = global_set_cluster_bitmap_bit(new_cluster, true);
            if (ret != OS_SUCCESS) return ret;
            the_inode.data_desc.index_table.triple_indirect_pointer = new_cluster;
        }

        // 分配二级间接表（需要新二级表时）
        if (in_seg_offset % (512 * 512) == 0) {
            uint64_t block_index3 = the_inode.data_desc.index_table.triple_indirect_pointer * this->fs_metainf->cluster_block_count;
            uint64_t* level3_table=nullptr;  
            if (memdiskv1_blockdevice) {
                // 内存盘模式
                level3_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index3);
                if (level3_table[third_level_index] == 0) {
                    uint64_t new_cluster;
                    uint64_t block_group_index;
                    int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                    if (ret != OS_SUCCESS) return ret;
                    ret = global_set_cluster_bitmap_bit(new_cluster, true);
                    if (ret != OS_SUCCESS) return ret;
                    level3_table[third_level_index] = new_cluster;
                }
            } else {
                uint64_t lv3_tb_phyblkidx=the_inode.data_desc.index_table.triple_indirect_pointer *fs_metainf->cluster_block_count;
                uint64_t new_cluster;
                uint64_t block_group_index;
                int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                if (ret != OS_SUCCESS) return ret;
                ret = global_set_cluster_bitmap_bit(new_cluster, true);
                if (ret != OS_SUCCESS) return ret;
                status=phylayer->write(
                    lv3_tb_phyblkidx,
                    third_level_index*sizeof(uint64_t),
                    &new_cluster,
                    sizeof(uint64_t)
                );
            }
        }

        // 分配一级间接表（需要新一级表时）
        if (in_seg_offset % 512 == 0) {
            uint64_t block_index3 = the_inode.data_desc.index_table.triple_indirect_pointer * this->fs_metainf->cluster_block_count;          
            if (memdiskv1_blockdevice) {
                // 内存盘模式
                uint64_t* level3_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index3);
                uint64_t block_index2 = level3_table[third_level_index] * this->fs_metainf->cluster_block_count;
                uint64_t* level2_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index2);
                uint64_t new_cluster;
                uint64_t block_group_index;
                int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                if (ret != OS_SUCCESS) return ret;
                ret = global_set_cluster_bitmap_bit(new_cluster, true);
                if (ret != OS_SUCCESS) return ret;
                level2_table[second_level_index] = new_cluster;
            } else {
               uint64_t lv3tbblkidx=the_inode.data_desc.index_table.triple_indirect_pointer *fs_metainf->cluster_block_count;
               uint64_t lv2tbclsidx=0;
               status=phylayer->read(
                    lv3tbblkidx,third_level_index*sizeof(uint64_t),
                    &lv2tbclsidx,
                    sizeof(uint64_t)
               );
                if(status!=OS_SUCCESS)return status;
                uint64_t new_cluster;
                uint64_t block_group_index;
                int ret = search_avaliable_cluster_bitmap_bits(1, new_cluster, block_group_index);
                if (ret != OS_SUCCESS) return ret;
                ret = global_set_cluster_bitmap_bit(new_cluster, true);
                if (ret != OS_SUCCESS) return ret;
               status=phylayer->write(
                    lv2tbclsidx*fs_metainf->cluster_block_count,
                    second_level_index*sizeof(uint64_t),
                    &new_cluster,
                    sizeof(uint64_t)
               );
               if (status!=OS_SUCCESS)
               {
                   return status;
               }
               
            }
        }
        
        // 逐级解析获取一级间接表并填充
        uint64_t block_index3 = the_inode.data_desc.index_table.triple_indirect_pointer * this->fs_metainf->cluster_block_count;

        if (memdiskv1_blockdevice) {
            // 内存盘模式
            uint64_t* level3_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index3);
            uint64_t block_index2 = level3_table[third_level_index] * this->fs_metainf->cluster_block_count;
            uint64_t* level2_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index2);
            uint64_t block_index1 = level2_table[second_level_index] * this->fs_metainf->cluster_block_count;
            uint64_t* level1_table = (uint64_t*)memdiskv1_blockdevice->get_vaddr(block_index1);
            level1_table[first_level_index] = phyclsidx;
        } else {
            uint64_t lv3tbblkidx=the_inode.data_desc.index_table.triple_indirect_pointer *fs_metainf->cluster_block_count;
            uint64_t lv2tbclsidx=0;
            status=phylayer->read(
                lv3tbblkidx,third_level_index*sizeof(uint64_t),
                &lv2tbclsidx,
                sizeof(uint64_t)
            );
            if(status!=OS_SUCCESS)return status;
            uint64_t lv2tbblkidx=lv2tbclsidx*fs_metainf->cluster_block_count;
            uint64_t lv1tbclsidx=0;
            status=phylayer->read(
                lv2tbblkidx,second_level_index*sizeof(uint64_t),
                &lv1tbclsidx,
                sizeof(uint64_t)
            );
            if(status!=OS_SUCCESS)return status;
            uint64_t lv1tbblkidx=lv1tbclsidx*fs_metainf->cluster_block_count;
            status=phylayer->write(
                lv1tbblkidx,
                first_level_index*sizeof(uint64_t),
                &phyclsidx,
                sizeof(uint64_t)
            );
            if (status!=OS_SUCCESS)
            {
                return status;
            }
            return OS_SUCCESS;
        }
    }
    return OS_UNREACHABLE_CODE;
}

int init_fs_t::Decrease_inode_allocated_clusters(
    Inode &the_inode, uint64_t Decrease_clusters_count)
{   
    uint64_t start_delete_lcluster=the_inode.file_size/fs_metainf->cluster_size;
    int status=OS_SUCCESS;
    if(the_inode.flags.extents_or_indextable==INDEX_TABLE_TYPE)
    {
        for(uint64_t i=0;i<Decrease_clusters_count;i++)
        {
            status=idxtbmod_delete_lcluster(
                the_inode,
                start_delete_lcluster-i
            );
            if(status!=OS_SUCCESS)return status;
        }
    }
    else{
        if(is_memdiskv1)
        {
            FileExtentsEntry_t* extents_array = (FileExtentsEntry_t*)memdiskv1_blockdevice->get_vaddr(
                the_inode.data_desc.extents.first_cluster_index_of_extents_array * fs_metainf->cluster_block_count
            );
            if(extents_array==nullptr){
                return OS_BAD_FUNCTION;
            }
            uint32_t old_extents_count=the_inode.data_desc.extents.entries_count;
            uint64_t delete_cls_scanner=Decrease_clusters_count;
            while(delete_cls_scanner)
            {
            if(delete_cls_scanner<extents_array[old_extents_count-1].length_in_clusters)
            {delete_cls_scanner=0;
             extents_array[old_extents_count-1].length_in_clusters-=delete_cls_scanner;
             status=global_set_cluster_bitmap_bits(
                extents_array[old_extents_count-1].first_cluster_index+
                extents_array[old_extents_count-1].length_in_clusters,
                delete_cls_scanner,
                false
             );     
            }else{
                delete_cls_scanner-=extents_array[old_extents_count-1].length_in_clusters;
                old_extents_count--;
                status=global_set_cluster_bitmap_bits(
                    extents_array[old_extents_count].first_cluster_index,
                    extents_array[old_extents_count].length_in_clusters,
                    false
                );
                
            }
            if(status!=OS_SUCCESS)return status;
            the_inode.data_desc.extents.entries_count=old_extents_count;
        }
    }
    }
    return OS_SUCCESS;
}
/**
 * 这里的删除只在对应物理簇的位图中释放写0表示空闲
 * 此函数只用于从下到上扫描删除，
 * 若要删除（n-1）级表的首个索引，
 * 根据上面的逻辑，必然要删除对应(n-1)级表信息
 */
int init_fs_t::idxtbmod_delete_lcluster(Inode &the_inode, uint64_t lcluster_index)
{
    int status;
    uint64_t phycluster_index = 0;
    
    // 直接指针区域
    if (0 <= lcluster_index && lcluster_index < LEVLE1_INDIRECT_START_CLUSTER_INDEX) {
        phycluster_index = the_inode.data_desc.index_table.direct_pointers[lcluster_index];
        the_inode.data_desc.index_table.direct_pointers[lcluster_index] = 0;
        return global_set_cluster_bitmap_bit(phycluster_index, false);
    }   
    if (LEVLE1_INDIRECT_START_CLUSTER_INDEX <= lcluster_index && 
        lcluster_index < LEVEL2_INDIRECT_START_CLUSTER_INDEX) {
        uint32_t offset = lcluster_index - LEVLE1_INDIRECT_START_CLUSTER_INDEX;
        // 物理设备模式
        uint64_t lv1tbblkidx = the_inode.data_desc.index_table.single_indirect_pointer * 
                                   fs_metainf->cluster_block_count;
            
            // 读取物理簇索引
            status = phylayer->read(
                lv1tbblkidx, 
                offset * sizeof(uint64_t),
                &phycluster_index,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }        
        // 释放物理簇
        status= global_set_cluster_bitmap_bit(phycluster_index, false);
        if(offset==0)global_set_cluster_bitmap_bit(
            the_inode.data_desc.index_table.single_indirect_pointer, false);

    }
    
    // 二级间接索引区域
    if (LEVEL2_INDIRECT_START_CLUSTER_INDEX <= lcluster_index && 
        lcluster_index < LEVEL3_INDIRECT_START_CLUSTER_INDEX) {
        uint32_t in_seg_offset = lcluster_index - LEVEL2_INDIRECT_START_CLUSTER_INDEX;
        uint32_t second_level_index = in_seg_offset >> 9;
        uint32_t first_level_index = in_seg_offset % 512;
        uint64_t lv2tbblkidx = the_inode.data_desc.index_table.double_indirect_pointer * 
                                   fs_metainf->cluster_block_count;
        uint64_t lv1tbclsidx = 0;
            
            // 读取二级表中的项
            status = phylayer->read(
                lv2tbblkidx,
                second_level_index * sizeof(uint64_t),
                &lv1tbclsidx,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }
            
            // 读取一级表中的物理簇索引
            uint64_t lv1tbblkidx = fs_metainf->cluster_block_count * lv1tbclsidx;
            status = phylayer->read(
                lv1tbblkidx,
                first_level_index * sizeof(uint64_t),
                &phycluster_index,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }
        // 释放物理簇
        status= global_set_cluster_bitmap_bit(phycluster_index, false);
        if(status != OS_SUCCESS)return status;
        if(in_seg_offset%512==0)global_set_cluster_bitmap_bit(lv1tbclsidx, false);
        if(in_seg_offset==(0))global_set_cluster_bitmap_bit(
            the_inode.data_desc.index_table.double_indirect_pointer, false);
    }
    
    // 三级间接索引区域
    if (LEVEL3_INDIRECT_START_CLUSTER_INDEX <= lcluster_index && 
        lcluster_index < LEVEL4_INDIRECT_START_CLUSTER_INDEX) {
        uint32_t in_seg_offset = lcluster_index - LEVEL3_INDIRECT_START_CLUSTER_INDEX;
        uint32_t third_level_index = in_seg_offset >> 18;
        uint32_t second_level_index = (in_seg_offset >> 9) & 511;
        uint32_t first_level_index = in_seg_offset & 511;
            // 物理设备模式
            uint64_t lv3tbblkidx = the_inode.data_desc.index_table.triple_indirect_pointer * 
                                   fs_metainf->cluster_block_count;
            uint64_t lv2tbclsidx = 0;
            
            // 读取三级表中的项
            status = phylayer->read(
                lv3tbblkidx,
                third_level_index * sizeof(uint64_t),
                &lv2tbclsidx,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }
            
            uint64_t lv2tbblkidx = lv2tbclsidx * fs_metainf->cluster_block_count;
            uint64_t lv1tbclsidx = 0;
            
            // 读取二级表中的项
            status = phylayer->read(
                lv2tbblkidx,
                second_level_index * sizeof(uint64_t),
                &lv1tbclsidx,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }
            
            // 读取一级表中的物理簇索引
            uint64_t lv1tbblkidx = lv1tbclsidx * fs_metainf->cluster_block_count;
            status = phylayer->read(
                lv1tbblkidx,
                first_level_index * sizeof(uint64_t),
                &phycluster_index,
                sizeof(uint64_t)
            );
            
            if (status != OS_SUCCESS) {
                return status;
            }      
        
        // 释放物理簇
        status= global_set_cluster_bitmap_bit(phycluster_index, false);
        if (status != OS_SUCCESS) {
            return status;
        }
        if(in_seg_offset%512==0)status=global_set_cluster_bitmap_bit(lv1tbclsidx, false);
        if(status != OS_SUCCESS)return status;
        if(in_seg_offset%(512*512)==0)status=global_set_cluster_bitmap_bit(lv2tbclsidx, false);
        if(status != OS_SUCCESS)return status;
        if(in_seg_offset==0)status=global_set_cluster_bitmap_bit(
            the_inode.data_desc.index_table.triple_indirect_pointer, false);
        if(status != OS_SUCCESS)return status;
    }
    
    return OS_SUCCESS;
}
