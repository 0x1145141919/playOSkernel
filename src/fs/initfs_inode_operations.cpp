#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"

int init_fs_t::write_inode(uint64_t block_group_index, uint64_t inode_index, Inode *inode)
{

    // 检查块组索引是否有效
    if (block_group_index >= fs_metainf->total_blocks_group_valid) {
        return -1;
    }
    int status=OS_SUCCESS;
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
    // 使用物理层接口将inode数据写入指定位置
    if (is_memdiskv1) {
        Inode*array=(Inode*)memdiskv1_blockdevice->get_vaddr(sc->inodes_array_first_cluster*fs_metainf->cluster_block_count);
        array[inode_index]=*inode;
    } else {
        status = phylayer->write(
            sc->inodes_array_first_cluster*fs_metainf->cluster_block_count, 
            fs_metainf->inode_size*inode_index, 
            inode, 
            fs_metainf->inode_size);
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



