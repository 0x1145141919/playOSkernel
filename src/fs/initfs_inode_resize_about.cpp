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
        uint8_t start_diresct_lcluster=0;
        uint8_t end_diresct_lcluster=0;
        uint16_t start_single_lcluster=0;
        uint16_t end_single_lcluster=0;
        uint32_t start_double_lclusters=0;
        uint32_t end_double_lclusters=0;
        uint32_t start_triple_lclusters=0;
        uint32_t end_triple_lclusters=0;
        //解析上面这些索引
        uint64_t skipped_clusters_count=0;
        uint64_t inextens_start_logical_cluster_index=0;
        uint32_t extents_entry_scanner_index=0;

        // 计算簇数量
        const uint64_t old_size = the_inode.file_size;
        const uint64_t new_size = the_inode.file_size + Increase_clusters_count * fs_metainf->cluster_size;
        const uint64_t old_clusters = (old_size + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;
        const uint64_t new_clusters = (new_size + fs_metainf->cluster_size - 1) / fs_metainf->cluster_size;

        // 直接表范围计算
        uint8_t start_diresct_lcluster = 0;
        uint8_t end_diresct_lcluster = 0;
        if (new_clusters > old_clusters) {
            // 文件扩展时：处理新增的直接表项
            if (old_clusters < DIRECT_CLUSTERS) {
                start_diresct_lcluster = static_cast<uint8_t>(old_clusters);
            }
            if (new_clusters > DIRECT_CLUSTERS) {
                end_diresct_lcluster = DIRECT_CLUSTERS - 1;
            } else {
                end_diresct_lcluster = static_cast<uint8_t>(new_clusters - 1);
            }
        } else {
            // 文件缩小时：处理被截断的直接表项
            if (new_clusters < DIRECT_CLUSTERS) {
                start_diresct_lcluster = static_cast<uint8_t>(new_clusters);
            }
            if (old_clusters > DIRECT_CLUSTERS) {
                end_diresct_lcluster = DIRECT_CLUSTERS - 1;
            } else {
                end_diresct_lcluster = static_cast<uint8_t>(old_clusters - 1);
            }
        }

        // 一阶间接表范围计算
        uint16_t start_single_lcluster = 0;
        uint16_t end_single_lcluster = 0;
        const uint64_t level1_start = LEVLE1_INDIRECT_START_CLUSTER_INDEX;
        const uint64_t level2_start = LEVEL2_INDIRECT_START_CLUSTER_INDEX;
        if (new_clusters > old_clusters) {
            if (old_clusters >= level1_start) {
                start_single_lcluster = static_cast<uint16_t>(old_clusters - level1_start);
            }
            if (new_clusters > level2_start) {
                end_single_lcluster = static_cast<uint16_t>(level2_start - level1_start - 1);
            } else {
                end_single_lcluster = static_cast<uint16_t>(new_clusters - level1_start - 1);
            }
        } else {
            if (new_clusters >= level1_start) {
                start_single_lcluster = static_cast<uint16_t>(new_clusters - level1_start);
            }
            if (old_clusters > level2_start) {
                end_single_lcluster = static_cast<uint16_t>(level2_start - level1_start - 1);
            } else {
                end_single_lcluster = static_cast<uint16_t>(old_clusters - level1_start - 1);
            }
        }

        // 二阶间接表范围计算
        uint32_t start_double_lclusters = 0;
        uint32_t end_double_lclusters = 0;
        const uint64_t level3_start = LEVEL3_INDIRECT_START_CLUSTER_INDEX;
        if (new_clusters > old_clusters) {
            if (old_clusters >= level2_start) {
                start_double_lclusters = static_cast<uint32_t>(old_clusters - level2_start);
            }
            if (new_clusters > level3_start) {
                end_double_lclusters = static_cast<uint32_t>(level3_start - level2_start - 1);
            } else {
                end_double_lclusters = static_cast<uint32_t>(new_clusters - level2_start - 1);
            }
        } else {
            if (new_clusters >= level2_start) {
                start_double_lclusters = static_cast<uint32_t>(new_clusters - level2_start);
            }
            if (old_clusters > level3_start) {
                end_double_lclusters = static_cast<uint32_t>(level3_start - level2_start - 1);
            } else {
                end_double_lclusters = static_cast<uint32_t>(old_clusters - level2_start - 1);
            }
        }

        // 三阶间接表范围计算
        uint32_t start_triple_lclusters = 0;
        uint32_t end_triple_lclusters = 0;
        const uint64_t triple_limit = level3_start + TRIPLE_INDIRECT_CLUSTERS;
        if (new_clusters > old_clusters) {
            if (old_clusters >= level3_start) {
                start_triple_lclusters = static_cast<uint32_t>(old_clusters - level3_start);
            }
            if (new_clusters > triple_limit) {
                end_triple_lclusters = TRIPLE_INDIRECT_CLUSTERS - 1;
            } else {
                end_triple_lclusters = static_cast<uint32_t>(new_clusters - level3_start - 1);
            }
        } else {
            if (new_clusters >= level3_start) {
                start_triple_lclusters = static_cast<uint32_t>(new_clusters - level3_start);
            }
            if (old_clusters > triple_limit) {
                end_triple_lclusters = TRIPLE_INDIRECT_CLUSTERS - 1;
            } else {
                end_triple_lclusters = static_cast<uint32_t>(old_clusters - level3_start - 1);
            }
        }

        if(start_diresct_lcluster&&end_diresct_lcluster)//直接表
        {
            uint64_t cluster_scanner=start_diresct_lcluster; 
            uint32_t in_extents_offset=0;
            while (cluster_scanner<=end_diresct_lcluster)
            {   
               in_extents_offset=inextens_start_logical_cluster_index - skipped_clusters_count;
                the_inode.data_desc.index_table.direct_pointers[cluster_scanner]=
                extents_array[extents_entry_scanner_index].first_cluster_index+in_extents_offset;
                if(in_extents_offset>=extents_array[extents_entry_scanner_index].length_in_clusters)
                {
                    skipped_clusters_count+=extents_array[extents_entry_scanner_index].length_in_clusters;
                    extents_entry_scanner_index++;
                    inextens_start_logical_cluster_index=skipped_clusters_count;
                    in_extents_offset=0;
                }
                cluster_scanner++;
            }
            
        }
        if(start_single_lcluster&&end_single_lcluster)//一阶间接表
        { 
            if(start_single_lcluster==LEVLE1_INDIRECT_START_CLUSTER_INDEX)
            {
                status=lv0_create_and_fill(
                    the_inode.data_desc.index_table.single_indirect_pointer,
                    extents_array,
                    extents_entry_count,
                    skipped_clusters_count,
                    inextens_start_logical_cluster_index,
                    extents_entry_scanner_index,
                    end_single_lcluster-start_single_lcluster+1
                );
                if(status!=OS_SUCCESS)
                {

                }
            }
            else{//不是第一次创建，需要读取已有的表进行修改
                uint64_t*lv0tb_phycluster=nullptr;
                if(is_memdiskv1)
                {
                    lv0tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
                        start_single_lcluster*fs_metainf->cluster_block_count
                    );
                }else{
                    lv0tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
                    status=phylayer->readblk(
                        the_inode.data_desc.index_table.single_indirect_pointer,
                        start_single_lcluster,
                        lv0tb_phycluster
                    );
                    if(status!=OS_SUCCESS)
                    {
                        delete[] lv0tb_phycluster;
                        return status;
                    } 
                }
                uint32_t in_extent_offset=inextens_start_logical_cluster_index-skipped_clusters_count;    
                for(uint16_t i=start_single_lcluster;i<=end_single_lcluster;i++)
                {
                    lv0tb_phycluster[i]=extents_array[extents_entry_scanner_index].first_cluster_index+in_extent_offset;
                    if(in_extent_offset>=extents_array[extents_entry_scanner_index].length_in_clusters)
                    {
                        skipped_clusters_count+=extents_array[extents_entry_scanner_index].length_in_clusters;
                        extents_entry_scanner_index++;
                        inextens_start_logical_cluster_index=skipped_clusters_count;
                        in_extent_offset=0;
                    }
                }
                if(!is_memdiskv1)delete[] lv0tb_phycluster;
                status=phylayer->writeblk(
                    the_inode.data_desc.index_table.single_indirect_pointer*fs_metainf->cluster_block_count,
                    fs_metainf->cluster_block_count,
                    lv0tb_phycluster
                );
            }
        }
        if(start_double_lclusters&&end_double_lclusters)//二阶间接表
        {   
            if(start_double_lclusters==LEVEL2_INDIRECT_START_CLUSTER_INDEX)
            {
                status=lv1_create_and_fill(
                    the_inode.data_desc.index_table.double_indirect_pointer,
                    extents_array,
                    extents_entry_count,
                    skipped_clusters_count,
                    inextens_start_logical_cluster_index,
                    extents_entry_scanner_index,
                    end_double_lclusters-start_double_lclusters+1
                );
            }else{//不是第一次创建，需要读取已有的表进行修改
                uint64_t*lv1tb_phycluster=nullptr;
                if(is_memdiskv1)
                {
                    lv1tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
                        start_double_lclusters*fs_metainf->cluster_block_count
                    );
            }else{
                lv1tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
                status=phylayer->readblk(
                    the_inode.data_desc.index_table.double_indirect_pointer,
                    start_double_lclusters,
                    lv1tb_phycluster
                );
                if(status!=OS_SUCCESS)
                {
                    delete[] lv1tb_phycluster;
                    return status;
                }
            }
           uint32_t first_cluster_in_lv0tboffset=(start_double_lclusters-LEVEL2_INDIRECT_START_CLUSTER_INDEX)&511;
           uint16_t first_lv0tb_to_fill=512-first_cluster_in_lv0tboffset;
              //先处理第一个lv0表
            uint64_t*lv0tb_to_fill=nullptr;
            if(is_memdiskv1)
            {
                lv0tb_to_fill=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
                    start_double_lclusters*fs_metainf->cluster_block_count
                );
            }else{
                lv0tb_to_fill=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
                status=phylayer->readblk(
                    lv1tb_phycluster[((start_double_lclusters-LEVEL2_INDIRECT_START_CLUSTER_INDEX)>>9)&511],
                    start_double_lclusters,
                    lv0tb_to_fill
                );
                if(status!=OS_SUCCESS){
                    delete[] lv0tb_to_fill;
                    delete[] lv1tb_phycluster;
                    return status;   
                 }
                 uint32_t in_extent_offset=0;
                 for(uint16_t i=first_cluster_in_lv0tboffset;
                     i<512;i++)
                 {
                    in_extent_offset=inextens_start_logical_cluster_index-skipped_clusters_count;
                     lv0tb_to_fill[i]=extents_array[extents_entry_scanner_index].first_cluster_index+in_extent_offset;
                     if(in_extent_offset>=extents_array[extents_entry_scanner_index].length_in_clusters)
                     {
                         skipped_clusters_count+=extents_array[extents_entry_scanner_index].length_in_clusters;
                         extents_entry_scanner_index++;
                         inextens_start_logical_cluster_index=skipped_clusters_count;
                         in_extent_offset=0;
                     }
                 }
            }
            uint16_t lv1_start=(start_double_lclusters-LEVEL2_INDIRECT_START_CLUSTER_INDEX+511)>>9;
            uint16_t lv1_end=(end_double_lclusters-LEVEL2_INDIRECT_START_CLUSTER_INDEX)>>9;
            for(uint16_t i=lv1_start;i<=lv1_end;i++)
            {
                status=lv0_create_and_fill(
                    lv1tb_phycluster[i],
                    extents_array,
                    extents_entry_count,
                    skipped_clusters_count,
                    inextens_start_logical_cluster_index,
                    extents_entry_scanner_index,
                    (i==lv1_end)?((end_double_lclusters&511)+1):512
                );
                if(status!=OS_SUCCESS){
                    delete[] lv0tb_to_fill;
                    delete[] lv1tb_phycluster;
                    return status;
                }
            } delete[] lv1tb_phycluster;  
            delete[] lv0tb_to_fill;
            }
           
        }
        if(start_triple_lclusters&&end_triple_lclusters)//三阶间接表
        {
            if(start_triple_lclusters==LEVEL3_INDIRECT_START_CLUSTER_INDEX)
            {
                status=lv2_create_and_fill(
                    the_inode.data_desc.index_table.triple_indirect_pointer,
                    extents_array,
                    extents_entry_count,
                    inextens_start_logical_cluster_index,
                    end_triple_lclusters-start_triple_lclusters+1
                );
            }else{
                //不是第一次创建，需要读取已有的表进行修改
                uint32_t in_seg_offset=start_triple_lclusters-LEVEL3_INDIRECT_START_CLUSTER_INDEX;
                uint64_t*lv2tb_phycluster=nullptr;
                if(is_memdiskv1)
                {
                    lv2tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
                        start_triple_lclusters*fs_metainf->cluster_block_count
                    );
                    if(lv2tb_phycluster==nullptr){
                        return OS_IO_ERROR;
                    }
                }else{
                    lv2tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
                    status=phylayer->readblk(
                        the_inode.data_desc.index_table.triple_indirect_pointer,
                        fs_metainf->cluster_block_count,
                        lv2tb_phycluster
                    );
                }
                if(in_seg_offset&511)
                {
                                    
                    uint32_t lv0_start_offset = in_seg_offset & 511;
                    uint16_t lv1_index = (in_seg_offset >> 9) & 511;
                    uint16_t lv2_index = in_seg_offset >> 18;

                    uint64_t lv1tb_phys_cluster = lv2tb_phycluster[lv2_index];
                    uint64_t* lv0tb_to_fill = nullptr;

                    if (is_memdiskv1)
                    {
                        lv0tb_to_fill = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                            lv1tb_phys_cluster * fs_metainf->cluster_block_count + lv1_index * fs_metainf->cluster_block_count
                        );
                    }
                    else
                    {
                        lv0tb_to_fill = new uint64_t[fs_metainf->cluster_size / sizeof(uint64_t)];
                        status = phylayer->readblk(
                            lv1tb_phys_cluster,
                            lv1_index,
                            lv0tb_to_fill
                        );
                        if (status != OS_SUCCESS)
                        {
                            delete[] lv0tb_to_fill;
                            delete[] lv2tb_phycluster;
                            return status;
                        }
                    }

                    uint32_t in_extent_offset = inextens_start_logical_cluster_index - skipped_clusters_count;
                    for (uint16_t i = lv0_start_offset; i < 512; i++)
                    {
                        lv0tb_to_fill[i] = extents_array[extents_entry_scanner_index].first_cluster_index + in_extent_offset;
                        if (in_extent_offset >= extents_array[extents_entry_scanner_index].length_in_clusters)
                        {
                            skipped_clusters_count += extents_array[extents_entry_scanner_index].length_in_clusters;
                            extents_entry_scanner_index++;
                            inextens_start_logical_cluster_index = skipped_clusters_count;
                            in_extent_offset = 0;
                        }
                        else
                        {
                            in_extent_offset++;
                        }
                    }

                    if (!is_memdiskv1)
                    {
                        status = phylayer->writeblk(
                            lv1tb_phys_cluster * fs_metainf->cluster_block_count + lv1_index * fs_metainf->cluster_block_count,
                            fs_metainf->cluster_block_count,
                            lv0tb_to_fill
                        );
                        delete[] lv0tb_to_fill;
                        if (status != OS_SUCCESS)
                        {
                            delete[] lv2tb_phycluster;
                            return status;
                        }
                    }
                }//要处理初始0级表
                in_seg_offset=(in_seg_offset+511)&(~511);
                if(in_seg_offset>>9)
                {
                    uint16_t lv1_start = (in_seg_offset >> 9) & 511;
                    uint16_t lv2_index = in_seg_offset >> 18;

                    uint64_t lv1tb_phys_cluster = lv2tb_phycluster[lv2_index];
                    uint64_t* lv1tb_to_fill = nullptr;

                    if (is_memdiskv1)
                    {
                        lv1tb_to_fill = (uint64_t*)memdiskv1_blockdevice->get_vaddr(
                            lv1tb_phys_cluster * fs_metainf->cluster_block_count
                        );
                    }
                    else
                    {
                        lv1tb_to_fill = new uint64_t[fs_metainf->cluster_size / sizeof(uint64_t)];
                        status = phylayer->readblk(
                            lv1tb_phys_cluster,
                            0,
                            lv1tb_to_fill
                        );
                        if (status != OS_SUCCESS)
                        {
                            delete[] lv1tb_to_fill;
                            delete[] lv2tb_phycluster;
                            return status;
                        }
                    }

                    for (uint16_t i = lv1_start; i < 512; i++)
                    {
                        status = lv0_create_and_fill(
                            lv1tb_to_fill[i],
                            extents_array,
                            extents_entry_count,
                            skipped_clusters_count,
                            inextens_start_logical_cluster_index,
                            extents_entry_scanner_index,
                            512
                        );
                        if (status != OS_SUCCESS)
                        {
                            if (!is_memdiskv1)
                            {
                                delete[] lv1tb_to_fill;
                            }
                            delete[] lv2tb_phycluster;
                            return status;
                        }
                    }

                    if (!is_memdiskv1)
                    {
                        status = phylayer->writeblk(
                            lv1tb_phys_cluster * fs_metainf->cluster_block_count,
                            fs_metainf->cluster_block_count,
                            lv1tb_to_fill
                        );
                        delete[] lv1tb_to_fill;
                        if (status != OS_SUCCESS)
                        {
                            delete[] lv2tb_phycluster;
                            return status;
                        }
                    }
                }//要处理初始1级表
                //in_seg_offset向上取整到512*512的倍数
                in_seg_offset=(in_seg_offset+512*512-1)/(512*512);
                uint16_t lv2_start=(in_seg_offset)>>18;
                uint16_t lv2_end=(end_triple_lclusters-LEVEL3_INDIRECT_START_CLUSTER_INDEX)>>18;
                for(uint16_t i=lv2_start;i<=lv2_end;i++)
                {
                    status=lv1_create_and_fill(
                        lv2tb_phycluster[i],
                        extents_array,
                        extents_entry_count,
                        skipped_clusters_count,
                        inextens_start_logical_cluster_index,
                        extents_entry_scanner_index,
                        (i==lv2_end)?(end_triple_lclusters&511):512
                    );
                    if(status!=OS_SUCCESS){
                        delete[] lv2tb_phycluster;
                        return status;
                    }
                }
                if(!is_memdiskv1){
                    phylayer->writeblk(
                        the_inode.data_desc.index_table.triple_indirect_pointer*fs_metainf->cluster_block_count,
                        fs_metainf->cluster_block_count,
                        lv2tb_phycluster
                    );
                    delete[] lv2tb_phycluster;
                }
            }
        }
        //先直接表，后是一阶间接，二阶间接，三阶间接
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
        the_inode.allocated_clusters+=extents_array[i].length_in_clusters;
    }
    
}
int init_fs_t::lv2_create_and_fill(
    uint64_t &lv2_tb_cluster_phyindex, 
    FileExtentsEntry_t *extents_entry, 
    uint64_t extents_entry_count, 
    uint64_t extents_start_logical_cluster_index, 
    uint32_t to_allocate_clusters_count
)
{   
    int status=0;
    uint64_t lv2tb_bg_idx=0;
    lv2_tb_cluster_phyindex=search_avaliable_cluster_bitmap_bit(lv2tb_bg_idx);
    status=set_cluster_bitmap_bit(lv2tb_bg_idx,lv2_tb_cluster_phyindex,true);
    if(status!=0)return status;
    
    uint64_t* lv2_tb_phycluster=nullptr;
    if(is_memdiskv1 ){
        lv2_tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
            lv2_tb_cluster_phyindex*fs_metainf->cluster_block_count
        );
    }else{
         lv2_tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
    }
    
    const uint32_t CLUSTERS_PER_LV1_TABLE = 512 * 512;  // 262144
    uint16_t lv2_max_entry=(to_allocate_clusters_count>>18) & 511;  // 等价于 (to_allocate_clusters_count / CLUSTERS_PER_LV1_TABLE)
    uint32_t startcluster_extents_idx=0;
    uint64_t extents_logical_cluster_scanner=0;uint16_t i1=0;
    uint64_t skipped_clusters=0;
    for(;i1<extents_entry_count;i1++)
    {
        if(extents_logical_cluster_scanner<=extents_start_logical_cluster_index
        &&extents_start_logical_cluster_index<extents_logical_cluster_scanner+extents_entry[i1].length_in_clusters)
        {
            startcluster_extents_idx=i1;
            break;
        }else{
            extents_logical_cluster_scanner+=extents_entry[i1].length_in_clusters;
        }
    }
    if(i1==extents_entry_count){
        if(!is_memdiskv1 && lv2_tb_phycluster) {
            delete[] lv2_tb_phycluster;
        }
        return OS_IO_ERROR;
    }
    skipped_clusters=extents_logical_cluster_scanner;
    for(uint16_t i=0;i<=lv2_max_entry;i++)
    {
        status=lv1_create_and_fill(
            lv2_tb_phycluster[i],
            extents_entry,
            extents_entry_count,
            skipped_clusters,
            extents_start_logical_cluster_index,
            startcluster_extents_idx,
            i==lv2_max_entry?(to_allocate_clusters_count&0x3FFFF):CLUSTERS_PER_LV1_TABLE
        );
        
        // 检查 lv1_create_and_fill 的返回值
        if(status != OS_SUCCESS) {
            if(!is_memdiskv1 && lv2_tb_phycluster) {
                delete[] lv2_tb_phycluster;
            }
            return status;
        }
    }
    if(!is_memdiskv1){
        phylayer->writeblk(
            lv2_tb_cluster_phyindex*fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv2_tb_phycluster
        );
        delete[] lv2_tb_phycluster;
    }
    return OS_SUCCESS;
}
int init_fs_t::lv1_create_and_fill(
    uint64_t &lv1_tb_cluster_phyindex, 
    FileExtentsEntry_t *extents_entry, 
    uint64_t extents_entry_count, 
    uint64_t&skipped_clusters_count,
    uint64_t &extents_start_logical_cluster_index, 
    uint32_t &extents_entry_scanner_index, 
    uint32_t to_allocate_clusters_count
)
{   
    int status=0;
    uint64_t lv1tb_bg_idx=0;
    lv1_tb_cluster_phyindex=search_avaliable_cluster_bitmap_bit(lv1tb_bg_idx);
    status=set_cluster_bitmap_bit(lv1tb_bg_idx,lv1_tb_cluster_phyindex,true);
    if(status!=0)return status;
    
    uint64_t* lv1_tb_phycluster=nullptr;
    if(is_memdiskv1 ){
        lv1_tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
            lv1_tb_cluster_phyindex*fs_metainf->cluster_block_count
        );
    }else{
         lv1_tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
    }
    
    const uint32_t CLUSTERS_PER_LV0_TABLE = 512;  // 512
    uint16_t lv1_max_entry=(to_allocate_clusters_count>>9) & 511;  // 等价于 (to_allocate_clusters_count / CLUSTERS_PER_LV0_TABLE)
    uint64_t extents_logical_cluster_scanner=0;uint16_t i1=extents_entry_scanner_index;
    // 调整扫描起始位置
    for(uint16_t i=0; i<extents_entry_scanner_index; i++) {
        extents_logical_cluster_scanner += extents_entry[i].length_in_clusters;
    }
    
    for(;i1<extents_entry_count;i1++)
    {
        if(extents_logical_cluster_scanner<=extents_start_logical_cluster_index
        &&extents_start_logical_cluster_index<extents_logical_cluster_scanner+extents_entry[i1].length_in_clusters)
        {
            extents_entry_scanner_index=i1;
            break;
        }else{
            extents_logical_cluster_scanner+=extents_entry[i1].length_in_clusters;
        }
    }
    if(i1==extents_entry_count){
        if(!is_memdiskv1 && lv1_tb_phycluster) {
            delete[] lv1_tb_phycluster;
        }
        return OS_IO_ERROR;
    }
    for(uint16_t i=0;i<=lv1_max_entry;i++)
    {
        status=lv0_create_and_fill(
            lv1_tb_phycluster[i],
            extents_entry,
            extents_entry_count,
            skipped_clusters_count,
            extents_start_logical_cluster_index,
            extents_entry_scanner_index,
            i==lv1_max_entry?(to_allocate_clusters_count&0x1FF):CLUSTERS_PER_LV0_TABLE
        );
        
        // 检查 lv0_create_and_fill 的返回值
        if(status != OS_SUCCESS) {
            if(!is_memdiskv1 && lv1_tb_phycluster) {
                delete[] lv1_tb_phycluster;
            }
            return status;
        }
    }
    if(!is_memdiskv1){
        phylayer->writeblk(
            lv1_tb_cluster_phyindex*fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv1_tb_phycluster
        );
        delete[] lv1_tb_phycluster;
    }
    return OS_SUCCESS;
}
/**
 * @brief 创建并填充0级索引表（叶子节点）
 * 
 * 该函数用于创建并填充一个0级索引表，它是最底层的索引表，
 * 直接指向实际的数据簇。函数会分配物理簇用于存储索引表，
 * 并根据文件的extent信息填充索引表内容。
 * 
 * @param lv1_tb_cluster_phyindex 输出参数，返回新分配的0级索引表的物理簇索引
 * @param extents_entry 文件的extent条目数组，描述了文件数据在磁盘上的分布情况
 * @param extents_entry_count extent条目的数量
 * @param skipped_clusters_count 已经跳过的簇计数，用于计算当前extent内的偏移量
 * @param inextens_start_logical_cluster_index 当前处理的逻辑簇索引
 * @param extents_entry_scanner_index extent条目扫描器索引，指示当前正在处理的extent条目
 * @param to_allocate_clusters_count 需要分配的簇数量
 * @return int 返回操作结果，成功返回OS_SUCCESS，失败返回相应错误码
 */
int init_fs_t::lv0_create_and_fill(
    uint64_t &lv1_tb_cluster_phyindex, 
    FileExtentsEntry_t *extents_entry, 
    uint64_t extents_entry_count,
    uint64_t&skipped_clusters_count,
    uint64_t &inextens_start_logical_cluster_index,
    uint32_t &extents_entry_scanner_index,
    uint32_t to_allocate_clusters_count)
{
    int status=0;
    uint64_t lv0tb_bg_idx=0;
    
    // 在位图中查找可用的簇，并标记为已使用
    lv1_tb_cluster_phyindex=search_avaliable_cluster_bitmap_bit(lv0tb_bg_idx);
    status=set_cluster_bitmap_bit(lv0tb_bg_idx,lv1_tb_cluster_phyindex,true);
    if(status!=0)return status;
    
    uint64_t* lv0_tb_phycluster=nullptr;
    
    // 根据存储类型获取0级索引表的虚拟地址或分配内存
    if(is_memdiskv1 ){
        lv0_tb_phycluster=(uint64_t*)memdiskv1_blockdevice->get_vaddr(
            lv1_tb_cluster_phyindex*fs_metainf->cluster_block_count
        );
    }else{
         lv0_tb_phycluster=new uint64_t[fs_metainf->cluster_size/sizeof(uint64_t)];
    }
    
    // 参数有效性检查：确保分配的簇数量在有效范围内
    if(to_allocate_clusters_count>512||to_allocate_clusters_count==0)
    {
        delete[] lv0_tb_phycluster;
        return OS_INVALID_PARAMETER;
    }
    
    // 计算在当前extent中的偏移量
    uint32_t in_extents_offset=inextens_start_logical_cluster_index - skipped_clusters_count;
    
    // 填充0级索引表，将逻辑簇映射到物理簇
    for(uint32_t i=0;i<to_allocate_clusters_count;i++)
    {
        lv0_tb_phycluster[i]=extents_entry[extents_entry_scanner_index].first_cluster_index+in_extents_offset;
        in_extents_offset++;
        
        // 检查是否需要移动到下一个extent条目
        if(in_extents_offset>=extents_entry[extents_entry_scanner_index].length_in_clusters)
        {
            skipped_clusters_count+=extents_entry[extents_entry_scanner_index].length_in_clusters;
            extents_entry_scanner_index++;
            in_extents_offset=0;
            inextens_start_logical_cluster_index=skipped_clusters_count;
            // 检查extent条目是否用完但还有簇需要分配
            if(extents_entry_scanner_index>=extents_entry_count&&i!=to_allocate_clusters_count-1)
            {
                if(!is_memdiskv1 && lv0_tb_phycluster) {
                    delete[] lv0_tb_phycluster;
                }
                return OS_IO_ERROR;
            }
        }
    }
  
    // 如果不是内存磁盘，则将索引表写入物理存储并释放临时内存
    if(!is_memdiskv1){
        phylayer->writeblk(
            lv1_tb_cluster_phyindex*fs_metainf->cluster_block_count,
            fs_metainf->cluster_block_count,
            lv0_tb_phycluster
        );
        delete[] lv0_tb_phycluster;
    }
    return OS_SUCCESS;
}

int init_fs_t::Decrease_inode_allocated_clusters(
    Inode &the_inode, uint64_t Decrease_clusters_count)
{   
    uint64_t start_delete_lcluster=the_inode.file_size/fs_metainf->cluster_size;
    
    if(the_inode.flags.extents_or_indextable==INDEX_TABLE_TYPE)
    {
        
    }
    else{

    }
    return 0;
}
