#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"

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

uint32_t init_fs_t::search_avaliable_inode_bitmap_bit(uint64_t&block_group_index) {
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

uint32_t init_fs_t::search_avaliable_cluster_bitmap_bit(uint64_t&block_group_index) {
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