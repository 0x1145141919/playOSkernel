#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#ifdef USER_MODE
#include <x86intrin.h>
#endif
#ifdef KERNEL_MODE 
#include "kintrin.h"
#endif
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
    // 如果输入块组索引为0，则从第一个有效块组开始搜索
    uint64_t start_group = (block_group_index == 0) ? fs_metainf->root_block_group_index : block_group_index;
    
    // 确定搜索的结束块组
    uint64_t end_group = (block_group_index == 0) ? fs_metainf->total_blocks_group_valid : block_group_index;
    
    for (uint64_t current_group = start_group; current_group <= end_group; ++current_group) {
        void* bitmap_base = get_bitmap_base(current_group, INODE_BITMAP);
        if (!bitmap_base) continue;

        SuperCluster* sc = get_supercluster(current_group);
        if (!sc) {
            if (!is_memdiskv1) {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            continue;
        }
        
        // 计算该块组中inode位图的总位数
        uint64_t total_bits = sc->inodes_count_max;
        uint32_t num_blocks = (total_bits + 511) / 512;
        
        for (uint32_t i = 0; i < num_blocks; i++) {
            bitset512_t* block = reinterpret_cast<bitset512_t*>(
                reinterpret_cast<uint8_t*>(bitmap_base) + i * sizeof(bitset512_t)
            );
            
            // 处理最后一个块，可能未填满
            if (i == num_blocks - 1) {
                uint16_t max_bit_offset = total_bits & 511;
                int result_offset = get_first_zero_bit_index(block);
                
                // 释放位图资源
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                
                // 检查找到的位是否在有效范围内
                if (result_offset != -1 && static_cast<uint16_t>(result_offset) < max_bit_offset) {
                    block_group_index = current_group; // 更新输出参数
                    return (i << 9) + result_offset; // 返回全局位索引
                }
                // 当前块组无可用位，继续下一个
                break;
            }
            
            // 处理完整填充的块
            int index = get_first_zero_bit_index(block);
            if (index != -1) {
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                block_group_index = current_group; // 更新输出参数
                return i * 512 + static_cast<uint32_t>(index); // 返回全局位索引
            }
        }
        
        // 释放当前块组的位图资源
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
    }
    
    // 所有块组均无可用inode
    return INVALID_BITMAP_INDEX;
}

uint32_t init_fs_t::search_avaliable_cluster_bitmap_bit(uint64_t&block_group_index) {
    // 如果输入块组索引为0，则从第一个有效块组开始搜索
    uint64_t start_group = (block_group_index == 0) ? fs_metainf->root_block_group_index : block_group_index;
    
    // 确定搜索的结束块组
    uint64_t end_group = (block_group_index == 0) ? fs_metainf->total_blocks_group_valid : block_group_index;
    
    for (uint64_t current_group = start_group; current_group <= end_group; ++current_group) {
        void* bitmap_base = get_bitmap_base(current_group, CLUSTER_BITMAP);
        if (!bitmap_base) continue;

        SuperCluster* sc = get_supercluster(current_group);
        if (!sc) {
            if (!is_memdiskv1) {
                delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            }
            continue;
        }
        
        // 计算该块组中簇位图的总位数
        uint64_t total_bits = sc->cluster_count;
        uint32_t num_blocks = (total_bits + 511) / 512;
        
        for (uint32_t i = 0; i < num_blocks; i++) {
            bitset512_t* block = reinterpret_cast<bitset512_t*>(
                reinterpret_cast<uint8_t*>(bitmap_base) + i * sizeof(bitset512_t)
            );
            
            // 处理最后一个块，可能未填满
            if (i == num_blocks - 1) {
                uint16_t max_bit_offset = total_bits & 511;
                int result_offset = get_first_zero_bit_index(block);
                
                // 释放位图资源
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                
                // 检查找到的位是否在有效范围内
                if (result_offset != -1 && static_cast<uint16_t>(result_offset) < max_bit_offset) {
                    block_group_index = current_group; // 更新输出参数
                    return (i << 9) + result_offset; // 返回全局位索引
                }
                // 当前块组无可用位，继续下一个
                break;
            }
            
            // 处理完整填充的块
            int index = get_first_zero_bit_index(block);
            if (index != -1) {
                if (!is_memdiskv1) {
                    delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                }
                block_group_index = current_group; // 更新输出参数
                return i * 512 + static_cast<uint32_t>(index); // 返回全局位索引
            }
        }
        
        // 释放当前块组的位图资源
        if (!is_memdiskv1) {
            delete[] reinterpret_cast<uint8_t*>(bitmap_base);
        }
    }
    
    // 所有块组均无可用簇
    return INVALID_BITMAP_INDEX;
}

int init_fs_t::search_avaliable_cluster_bitmap_bits(
    uint64_t aquired_avaliable_clusters_count,
    uint64_t& result_base,
    uint64_t& block_group_index
) {
    if (aquired_avaliable_clusters_count == 0) return -1;



    uint64_t start_group = (block_group_index == 0) ? fs_metainf->root_block_group_index : block_group_index;
    uint64_t end_group   = (block_group_index == 0) ? fs_metainf->total_blocks_group_valid : block_group_index;

    for (uint64_t current_group = start_group; current_group <= end_group; ++current_group) {
        void* bitmap_base = get_bitmap_base(current_group, CLUSTER_BITMAP);
        if (!bitmap_base) continue;

        SuperCluster* sc = get_supercluster(current_group);
        if (!sc) {
            if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            continue;
        }

        if (sc->cluster_count < aquired_avaliable_clusters_count) {
            if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            continue;
        }

        uint64_t total_bits = sc->cluster_count;
        uint64_t total_bytes = (total_bits + 7) / 8;
        uint64_t words = (total_bits + 63) / 64;

        // 栈上缓存反转后的 word（最大支持 2^32 簇，足够）
        // 若担心栈空间，可改为静态或外部传入
        uint64_t bitmap_words[8192]; // 8192 * 8 = 64KB，内核可接受
        if (words > 8192) {
            if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
            continue; // 过大，跳过
        }

        // 反转每个字节并组装成正常 bit order 的 word
        uint8_t* src = reinterpret_cast<uint8_t*>(bitmap_base);
        for (uint64_t w = 0; w < words; ++w) {
            uint64_t word = 0;
            uint64_t byte_offset = w * 8;
            for (int i = 0; i < 8; ++i) {
                uint64_t byte_idx = byte_offset + i;
                if (byte_idx < total_bytes) {
                    word |= (uint64_t)bit_reverse_table[src[byte_idx]] << (i * 8);
                }
            }
            bitmap_words[w] = word;
        }

        uint64_t consec = 0;
        uint64_t run_start = 0;

        for (uint64_t w = 0; w < words; ++w) {
            uint64_t word = bitmap_words[w];
            uint64_t bits_in_word = (w == words - 1) ? (total_bits - w * 64) : 64;
            if (bits_in_word == 0) break;

            // 优化：全空闲
            uint64_t full_mask = (1ULL << bits_in_word) - 1;
            if ((word & full_mask) == 0) {
                if (consec == 0) run_start = w * 64;
                consec += bits_in_word;
                if (consec >= aquired_avaliable_clusters_count) {
                    result_base = run_start;
                    block_group_index = current_group;
                    if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                    return 0;
                }
                continue;
            }

            // 优化：全占用
            if ((word & full_mask) == full_mask) {
                consec = 0;
                continue;
            }

            // 逐位扫描
            for (uint64_t bit = 0; bit < bits_in_word; ++bit) {
                if ((word & (1ULL << bit)) == 0) { // 空闲
                    if (consec == 0) run_start = w * 64 + bit;
                    consec++;
                    if (consec >= aquired_avaliable_clusters_count) {
                        result_base = run_start;
                        block_group_index = current_group;
                        if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
                        return 0;
                    }
                } else {
                    consec = 0;
                }
            }
        }

        if (!is_memdiskv1) delete[] reinterpret_cast<uint8_t*>(bitmap_base);
    }

    return -1;
}
int init_fs_t::FileExtentsEntry_merger(FileExtentsEntry_t *old_buff,FileExtentsEntry_t *new_buff,uint64_t&entryies_count){
    uint64_t new_scanner=0;
    new_buff[new_scanner]=old_buff[0];
    for(uint64_t old_scanner=1;old_scanner<entryies_count;old_scanner++)
    {
        if(new_buff[new_scanner].first_cluster_index+new_buff[new_scanner].length_in_clusters==old_buff[old_scanner].first_cluster_index){
            new_buff[new_scanner].length_in_clusters+=old_buff[old_scanner].length_in_clusters;
        }else{
            new_scanner++;
            new_buff[new_scanner]=old_buff[old_scanner];
        }
    }
    entryies_count=new_scanner+1;
    return OS_SUCCESS;
}
/**
 *类型：纯人工函数，非黑箱
*功能：扫描块组中的空闲簇，存储到指定FileExtentsEntry_t *extents_entry里面
*函数内会为FileExtentsEntry_t *extents_entry分配堆，调用者负责释放
*返回值：0成功，非0失败
 */

int init_fs_t::clusters_bitmap_alloc(uint64_t alloc_clusters_count, FileExtentsEntry_t *extents_entry, uint64_t&entry_count)
{   uint64_t left_clusters_count = alloc_clusters_count;
    constexpr uint64_t MAX_EXTENTS_ENTRIES = 1024; 
    FileExtentsEntry_t *buffer_a=new FileExtentsEntry_t[MAX_EXTENTS_ENTRIES];
    FileExtentsEntry_t *buffer_b=new FileExtentsEntry_t[MAX_EXTENTS_ENTRIES];
    FileExtentsEntry_t *using_buffer=buffer_a;
    FileExtentsEntry_t *prepared_buffer=buffer_b;
    uint64_t extents_index=0;
    for(int i = 0; i < fs_metainf->total_blocks_group_valid; i++)//遍历所有块组
        {
            SuperCluster* sc = get_supercluster(i);
            if(sc)
            {
                delete buffer_a;
                delete buffer_b;
                return OS_FILE_SYSTEM_DAMAGED;
            }
            uint64_t*clusters_bitmap=(uint64_t*)get_bitmap_base(i,CLUSTER_BITMAP);
            uint64_t bitmap_words_count=(sc->cluster_count+63)/64;
            for(uint64_t j = 0; j < bitmap_words_count; j++)//以字为单位遍历位图
            {
                uint64_t realword=reverse_perbytes(clusters_bitmap[j]);
                uint64_t scanned_content=(realword&1)?(~realword):realword;//如果最低位是1则取反，否则不变
                bool is_reversed=(realword&1);
                uint8_t inword_left_bits=64;
                uint64_t word_cluster_base=j*64+sc->first_cluster_index;
                while (inword_left_bits)
                {
                    if(scanned_content==0){
                        if(is_reversed)break;
                        using_buffer[extents_index].first_cluster_index=word_cluster_base+64-inword_left_bits;
                        using_buffer[extents_index].length_in_clusters=inword_left_bits>left_clusters_count?left_clusters_count:inword_left_bits;
                        left_clusters_count-=inword_left_bits;
                            if(left_clusters_count==0){
                                extents_entry=new FileExtentsEntry_t[MAX_EXTENTS_ENTRIES];
                                FileExtentsEntry_merger(using_buffer,extents_entry,extents_index);
                                entry_count=extents_index;
#ifdef KERNEL_MODE
                                gKpoolmemmgr.realloc(extents_entry,sizeof(FileExtentsEntry_t)*extents_index);
#endif
                                if(!is_memdiskv1){
                                    delete[] reinterpret_cast<uint8_t*>(clusters_bitmap);
                                }
                                 delete[] buffer_a;
                                delete[] buffer_b;
                                return OS_SUCCESS;
                            }
                        extents_index++;
                        if(extents_index>=MAX_EXTENTS_ENTRIES){
                           FileExtentsEntry_merger(using_buffer,prepared_buffer,extents_index);
                           FileExtentsEntry_t* temp=using_buffer;
                           using_buffer=prepared_buffer;
                           prepared_buffer=temp;
                        }
                        inword_left_bits=0;
                    }
                    uint8_t begin_continual_bits_count=__builtin_ctzll(scanned_content);
                    if(!is_reversed){
                        using_buffer[extents_index].first_cluster_index=word_cluster_base+64-inword_left_bits;
                        using_buffer[extents_index].length_in_clusters=begin_continual_bits_count>left_clusters_count?left_clusters_count:begin_continual_bits_count;
                        extents_index++;
                        left_clusters_count-=begin_continual_bits_count;
                        if(left_clusters_count==0){
                                extents_entry=new FileExtentsEntry_t[MAX_EXTENTS_ENTRIES];
                                FileExtentsEntry_merger(using_buffer,extents_entry,extents_index);
#ifdef KERNEL_MODE
                                gKpoolmemmgr.realloc(extents_entry,sizeof(FileExtentsEntry_t)*extents_index);
#endif
                                 if(!is_memdiskv1){
                                    delete[] reinterpret_cast<uint8_t*>(clusters_bitmap);
                                }
                                entry_count=extents_index;
                                delete[] buffer_a;
                                delete[] buffer_b;
                                return OS_SUCCESS;
                        }
                        if(extents_index>=MAX_EXTENTS_ENTRIES){
                            FileExtentsEntry_merger(using_buffer,prepared_buffer,extents_index);
                           FileExtentsEntry_t* temp=using_buffer;
                           using_buffer=prepared_buffer;
                           prepared_buffer=temp;
                        }
                    }
                    //看看是否为段内连续0位
                    is_reversed=!is_reversed;   
                    inword_left_bits-=begin_continual_bits_count;
                    scanned_content>>=begin_continual_bits_count;
                    scanned_content=~scanned_content;
                }
            }
            if(!is_memdiskv1){
            delete[] reinterpret_cast<uint8_t*>(clusters_bitmap);
        }
        }
        delete[] buffer_a;
        delete[] buffer_b;
        
        return OS_INSUFFICIENT_STORAGE_SPACE;
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
}/**
 * @brief 通过逻辑偏移量获取索引
  */

int init_fs_t::logical_offset_toindex(
    FileExtentsEntry_t *extents_array, 
    uint64_t extents_count, 
    uint64_t logical_offset, 
    uint64_t &result_index
)
{uint64_t clusters_scanner=0;
    for(uint64_t i=0;i<extents_count;i++)
    {
        if(clusters_scanner<=logical_offset&&logical_offset<clusters_scanner+extents_array[i].length_in_clusters)
        {
            result_index=extents_array[i].first_cluster_index+(logical_offset-clusters_scanner);
            return 0;
        }
        clusters_scanner+=extents_array[i].length_in_clusters;
    }
    return OS_INVALID_PARAMETER ;
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