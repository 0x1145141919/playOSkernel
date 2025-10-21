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
// Helper function to get base address of a bitmap
void* init_fs_t::get_bitmap_base(uint64_t block_group_index, uint32_t bitmap_type) {
    // 内存盘优化路径：直接通过虚拟地址连续特性解析内存
    if (is_memdiskv1) {

        SuperCluster* sc = get_supercluster(block_group_index);
        // 根据位图类型获取起始簇索引
        uint32_t bitmap_first_cluster;
        switch (bitmap_type) {
            case INODE_BITMAP:
                bitmap_first_cluster = sc->inodes_bitmap_first_cluster;
                break;
            case CLUSTER_BITMAP:
                bitmap_first_cluster = sc->clusters_bitmap_first_cluster;
                break;
            default:
                return nullptr;
        }

        // 直接返回位图在内存中的虚拟地址（无需任何复制或分配）
        return memdiskv1_blockdevice->get_vaddr(bitmap_first_cluster * fs_metainf->cluster_size);
    }
    int status;


    SuperCluster* sc = new SuperCluster();
    if (!sc) {

        return nullptr;
    }
    

    if (sc->magic != SUPER_CLUSTER_MAGIT) {
        return nullptr;
    }

    uint32_t bitmap_first_cluster;
    uint8_t bitmap_cluster_count;
    switch (bitmap_type) {
        case INODE_BITMAP:
            bitmap_first_cluster = sc->inodes_bitmap_first_cluster;
            bitmap_cluster_count = sc->inodes_bitmap_cluster_count;
            break;
        case CLUSTER_BITMAP:
            bitmap_first_cluster = sc->clusters_bitmap_first_cluster;
            bitmap_cluster_count = sc->clusters_bitmap_cluster_count;
            break;
        default:

            return nullptr;
    }
    
    uint64_t bitmap_size = bitmap_cluster_count * fs_metainf->cluster_block_count * fs_metainf->block_size;
    void* bitmap_data = new uint8_t[bitmap_size];
    if (!bitmap_data) {

        return nullptr;
    }
    
    uint64_t bitmap_block_index = bitmap_first_cluster * fs_metainf->cluster_block_count;
    status = phylayer->read(bitmap_block_index, 0, bitmap_data, bitmap_size);
    if (status != 0) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_data);

        return nullptr;
    }

    return bitmap_data;
}

uint32_t init_fs_t::search_avaliable_inode_bitmap_bit(uint64_t block_group_index) {
    void* bitmap_base = get_bitmap_base(block_group_index, INODE_BITMAP);
    if (!bitmap_base) return static_cast<uint32_t>(-1);

    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return static_cast<uint32_t>(-1);
    }
    uint64_t total_bits = sc->inodes_count_max;
    uint32_t num_blocks = (total_bits + 511) / 512;
    for (uint32_t i = 0; i < num_blocks; i++) {
        bitset512_t* block = reinterpret_cast<bitset512_t*>(
            reinterpret_cast<uint8_t*>(bitmap_base) + i * sizeof(bitset512_t)
        );
          if(i == num_blocks - 1)
        {
            uint16_t max_bit_offset=total_bits&511;
            uint16_t result_offset=get_first_zero_bit_index(block);
            if(!is_memdiskv1)
            {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            if(result_offset==-1)return i>>9;
            return result_offset<max_bit_offset?(i<<9)+result_offset:-1;
        }
        int index = get_first_zero_bit_index(block);
        if (index != -1) {
            if (!is_memdiskv1) {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            return i * 512 + static_cast<uint32_t>(index);
        }
    }
    
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return static_cast<uint32_t>(-1);
}

uint32_t init_fs_t::search_avaliable_cluster_bitmap_bit(uint64_t block_group_index) {
    void* bitmap_base = get_bitmap_base(block_group_index, CLUSTER_BITMAP);
    if (!bitmap_base) return static_cast<uint32_t>(-1);

    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return static_cast<uint32_t>(-1);
    }
    uint64_t total_bits = sc->cluster_count;
    


    uint32_t num_blocks = (total_bits + 511) / 512;

    for (uint32_t i = 0; i < num_blocks; i++) {
        bitset512_t* block = reinterpret_cast<bitset512_t*>(
            reinterpret_cast<uint8_t*>(bitmap_base) + i * sizeof(bitset512_t)
        );
        if(i == num_blocks - 1)
        {
            uint16_t max_bit_offset=total_bits&511;
            uint16_t result_offset=get_first_zero_bit_index(block);
            if(!is_memdiskv1)
            {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            return result_offset<max_bit_offset?(i<<9)+result_offset:-1;
        }
        int index = get_first_zero_bit_index(block);
        if (index != -1) {
            if (!is_memdiskv1) {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            return i * 512 + static_cast<uint32_t>(index);
        }
    }
    
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return static_cast<uint32_t>(-1);
}


int init_fs_t::set_inode_bitmap_bit(uint64_t block_group_index, uint64_t inode_index, bool value) {
    void* bitmap_base = get_bitmap_base(block_group_index, INODE_BITMAP);
    if (!bitmap_base) {
        return -1;
    }

    uint32_t block_idx = inode_index / 512;
    uint16_t bit_offset = inode_index % 512;
    
    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;
    }
    if(inode_index>=sc->inodes_count_max)
    {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;
    }
    bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
        reinterpret_cast<uint8_t*>(bitmap_base) + block_idx * sizeof(bitset512_t)
    );
    
    setbit_entry1bit_width(target_block, value, bit_offset);

    int write_status = 0;
    if (!is_memdiskv1) {
        uint32_t bitmap_first_cluster = sc->inodes_bitmap_first_cluster;
        uint64_t bitmap_block_index = bitmap_first_cluster * fs_metainf->cluster_block_count;
        uint64_t byte_offset = block_idx * sizeof(bitset512_t);
        uint64_t modified_block_index = bitmap_block_index + byte_offset / fs_metainf->block_size;
        uint32_t offset_in_block = byte_offset % fs_metainf->block_size;

        write_status = phylayer->write(modified_block_index, offset_in_block, target_block, sizeof(bitset512_t));
    }

    // 仅在非内存盘时释放资源
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }

    return (write_status == 0) ? 0 : -1;
}

int init_fs_t::set_inode_bitmap_bits(uint64_t block_group_index, uint64_t base_index, uint64_t bit_count, bool value) {
    void* bitmap_base = get_bitmap_base(block_group_index, INODE_BITMAP);
    if (!bitmap_base) {
        return -1;
    }

    uint64_t start_block = base_index / 512;
    uint64_t end_block = (base_index + bit_count - 1) / 512;

    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;
    }
    
    uint32_t bitmap_first_cluster = sc->inodes_bitmap_first_cluster;
    uint64_t bitmap_block_index = bitmap_first_cluster * fs_metainf->cluster_block_count;
    uint64_t inode_max_count=sc->inodes_count_max;
    if(base_index+bit_count>inode_max_count)
    {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;

    }
    for (uint64_t block_idx = start_block; block_idx <= end_block; ++block_idx) {
        uint16_t local_start = (block_idx == start_block) ? (base_index % 512) : 0;
        uint16_t local_end = (block_idx == end_block) ? ((base_index + bit_count - 1) % 512) : 511;
        uint16_t count = local_end - local_start + 1;

        bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
            reinterpret_cast<uint8_t*>(bitmap_base) + block_idx * sizeof(bitset512_t)
        );
        
        setbits_entry1bit_width(target_block, value, local_start, count);

        uint64_t byte_offset = block_idx * sizeof(bitset512_t);
        uint64_t modified_block_index = bitmap_block_index + byte_offset / fs_metainf->block_size;
        uint32_t offset_in_block = byte_offset % fs_metainf->block_size;
        
        if (!is_memdiskv1) {
            int write_status = phylayer->write(modified_block_index, offset_in_block, target_block, sizeof(bitset512_t));
            if (write_status != 0) {
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                return -1;
            }
        }
    }
    
    // 仅在非内存盘时释放位图内存
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return 0;
}

bool init_fs_t::get_inode_bitmap_bit(uint64_t block_group_index, uint64_t inode_index, int &status) {
    void* bitmap_base = get_bitmap_base(block_group_index, INODE_BITMAP);
    if (!bitmap_base) {
        status = -1;
        return false;
    }

    SuperCluster* sc = get_supercluster(block_group_index);
    if (sc == nullptr || sc->magic != SUPER_CLUSTER_MAGIT) {
        
        status = -1;
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return false;
    }

    if (inode_index >= sc->inodes_count_max) {
        
        status = -1;
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return false;
    }
    
    

    uint32_t block_idx = inode_index / 512;
    uint16_t bit_offset = inode_index % 512;
    
    bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
        reinterpret_cast<uint8_t*>(bitmap_base) + block_idx * sizeof(bitset512_t)
    );
    
    status = 0;
    bool result = getbit_entry1bit_width(target_block, bit_offset);
    
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return result;
}

bool init_fs_t::get_cluster_bitmap_bit(
    uint64_t block_group_index,
    uint64_t cluster_index,
    int &status
) {
    status = 0;
    void* bitmap_base = get_bitmap_base(block_group_index, CLUSTER_BITMAP);
    if (!bitmap_base) {
        status = -1;
        return false;
    }
    SuperCluster* sc = get_supercluster(block_group_index);
    if (sc == nullptr || sc->magic != SUPER_CLUSTER_MAGIT) {
        // 仅在非内存盘时释放SuperCluster

        status = -1;
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return false;
    }

    if (cluster_index >= sc->cluster_count) {
        // 仅在非内存盘时释放SuperCluster

        status = -1;
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return false;
    }
    // 仅在非内存盘时释放SuperCluster


    uint32_t blk_idx = cluster_index / 512;
    uint16_t bit_offset = cluster_index % 512;

    bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
        reinterpret_cast<uint8_t*>(bitmap_base) + blk_idx * sizeof(bitset512_t)
    );
    
    bool bit_value = getbit_entry1bit_width(target_block, bit_offset);
    
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return bit_value;
}

int init_fs_t::set_cluster_bitmap_bit(
    uint64_t block_group_index,
    uint64_t cluster_index,
    bool value
) {
    void* bitmap_base = get_bitmap_base(block_group_index, CLUSTER_BITMAP);
    if (!bitmap_base) {
        return -1;
    }

    uint32_t blk_idx = cluster_index / 512;
    uint16_t bit_offset = cluster_index % 512;
    
    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;
    }
    
    uint32_t bitmap_first_cluster = sc->clusters_bitmap_first_cluster;
    uint64_t bitmap_block_index = bitmap_first_cluster * fs_metainf->cluster_block_count;
    if(cluster_index >= sc->cluster_count)
    {

        return -1;
    }

    bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
        reinterpret_cast<uint8_t*>(bitmap_base) + blk_idx * sizeof(bitset512_t)
    );
    
    setbit_entry1bit_width(target_block, value, bit_offset);

    int write_status = 0;
    if (!is_memdiskv1) {
        uint64_t byte_offset = blk_idx * sizeof(bitset512_t);
        uint64_t modified_block_index = bitmap_block_index + byte_offset / fs_metainf->block_size;
        uint32_t offset_in_block = byte_offset % fs_metainf->block_size;
        
        write_status = phylayer->write(modified_block_index, offset_in_block, target_block, sizeof(bitset512_t));
    }

    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    
    return (write_status == 0) ? 0 : -1;
}

int init_fs_t::set_cluster_bitmap_bits(uint64_t block_group_index, uint64_t base_index, uint64_t bit_count, bool value) {
    void* bitmap_base = get_bitmap_base(block_group_index, CLUSTER_BITMAP);
    if (!bitmap_base) {
        return -1;
    }

    SuperCluster* sc = get_supercluster(block_group_index);
    if (!sc) {
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
        return -1;
    }
    
    uint32_t bitmap_first_cluster = sc->clusters_bitmap_first_cluster;
    uint64_t bitmap_block_index = bitmap_first_cluster * fs_metainf->cluster_block_count;
    if(base_index+bit_count>sc->cluster_count){
        // 仅在非内存盘时释放资源
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);

        }
        return -1;
    }
    // 仅在非内存盘时释放SuperCluster

    uint64_t start_block = base_index / 512;
    uint64_t end_block = (base_index + bit_count - 1) / 512;

    for (uint64_t block_idx = start_block; block_idx <= end_block; ++block_idx) {
        uint16_t local_start = (block_idx == start_block) ? (base_index % 512) : 0;
        uint16_t local_end = (block_idx == end_block) ? ((base_index + bit_count - 1) % 512) : 511;
        uint16_t count = local_end - local_start + 1;

        bitset512_t* target_block = reinterpret_cast<bitset512_t*>(
            reinterpret_cast<uint8_t*>(bitmap_base) + block_idx * sizeof(bitset512_t)
        );
        
        setbits_entry1bit_width(target_block, value, local_start, count);

        if (!is_memdiskv1) {
            uint64_t byte_offset = block_idx * sizeof(bitset512_t);
            uint64_t modified_block_index = bitmap_block_index + byte_offset / fs_metainf->block_size;
            uint32_t offset_in_block = byte_offset % fs_metainf->block_size;
            
            int write_status = phylayer->write(modified_block_index, offset_in_block, target_block, sizeof(bitset512_t));
            if (write_status != 0) {
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                return -1;
            }
        }
    }
    
    if (!is_memdiskv1) {
        delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }
    return 0;
}

uint64_t init_fs_t::bitmapindex_to_cluster_index(uint64_t block_group_index, uint32_t bitmap_index)
{
    // 获取总簇数和每块组簇数
    uint64_t total_clusters = fs_metainf->total_clusters;
    uint64_t clusters_per_group = fs_metainf->max_block_group_size_in_clusters;
    
    // 计算块组起始簇索引
    uint64_t group_start_cluster = block_group_index * clusters_per_group;
    
    // 检查块组索引是否有效（防止越界）
    if (group_start_cluster >= total_clusters) {
        return static_cast<uint64_t>(-1);
    }
    
    // 计算当前块组实际簇数（最后一个块组可能不满）
    uint64_t group_cluster_count = clusters_per_group;
    if (block_group_index == (total_clusters + clusters_per_group - 1) / clusters_per_group - 1) {
        group_cluster_count = total_clusters - group_start_cluster;
    }
    
    // 检查位图索引是否在有效范围内
    if (bitmap_index >= group_cluster_count) {
        return static_cast<uint64_t>(-1);
    }
    
    return group_start_cluster + bitmap_index;
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
    uint64_t rootdir_first_cluster = search_avaliable_cluster_bitmap_bit(0);
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

int init_fs_t::write_inode(uint64_t block_group_index, uint64_t inode_index, Inode* inode)
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
//从根inode解析路径
int init_fs_t::path_analyze(char *path, Inode &inode)
{
    if(strlen(path)==0||strlen(path)>=FILE_PATH_MAX_LEN)return OS_INVALID_FILE_PATH;
    if(path[0]!='/')return OS_INVALID_FILE_PATH;
    
    return OS_SUCCESS;
}
