#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
/**读取inode指向的数据流的内容，
 *参数:
 *  the_inode: inode对象
 *  stream_base_offset: 数据流的起始偏移
 *  size: 读取的数据大小
 *  buffer: 读取到的数据存放的缓冲区
 *此函数为内部接口，若超出文件大小会报错
 */
int init_fs_t::inode_content_read(Inode the_inode, uint64_t stream_base_offset, uint64_t size, uint8_t* buffer)
{
    // 参数有效性检查
    if (!buffer || size == 0) {
        return OS_INVALID_PARAMETER;
    }
    
    // 检查偏移量是否超出文件大小
    if (stream_base_offset >= the_inode.file_size) {
        return OS_INVALID_PARAMETER;
    }

    // 调整读取大小以不超过文件剩余部分
    uint64_t bytes_to_read = (stream_base_offset + size > the_inode.file_size) ? 
        (the_inode.file_size - stream_base_offset) : size;

    // 根据inode的数据组织模式选择迭代器
    if (the_inode.flags.extents_or_indextable) {
        // Extent模式处理
        extents_rw_iterator iter(this, the_inode, buffer, stream_base_offset, bytes_to_read);
        
        while (true) {
            auto state = iter.rnext();
            if (state == extents_rw_iterator::State::END) {
                return OS_SUCCESS;
            }
            if (state == extents_rw_iterator::State::ERROR_STATE) {
                return OS_IO_ERROR;
            }
        }
    } else {
        // 索引表模式处理
        idxtbmode_file_Iteration iter(
            the_inode,
            buffer,
            stream_base_offset,
            fs_metainf->cluster_block_count,
            fs_metainf->block_size,
            bytes_to_read
        );
        
        while (true) {
            int state = iter.rnext(this);
            if (state == idxtbmode_file_Iteration::END) {
                return OS_SUCCESS;
            }
            if (state == idxtbmode_file_Iteration::ERROR_STATE) {
                return OS_IO_ERROR;
            }
        }
    }
}

int init_fs_t::idxtbmode_file_Iteration::rnext(init_fs_t *fs_instance)
{
    int status;
    switch(state)
    {
        case MID:
        {   
            if(midcls_scanner == midcls_count) {
                state = END_CLS;
                break; // 添加break防止继续执行下面的代码
            }
            
            uint64_t phyclsidx;
            status = fs_instance->filecluster_to_fscluster_in_idxtb(file_inode, midbase_lcls + midcls_scanner, phyclsidx);
            if(status != OS_SUCCESS) {
                state = ERROR_STATE;
                break;
            }
            
            if(fs_instance->is_memdiskv1)
            {
                ksystemramcpy(fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count),
                    buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    cls_size
                );
            } else {
                status = fs_instance->phylayer->read(
                    phyclsidx * cls_blk_count,
                    0,
                    buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    cls_size
                );
                if(status != OS_SUCCESS) {
                    state = ERROR_STATE;
                    break;
                }
            }
            midcls_scanner++;
            break; // 添加break以防止fall-through
        }
        
        case START:
        { 
            uint64_t phyclsidx;
            status = fs_instance->filecluster_to_fscluster_in_idxtb(file_inode, start_lcls, phyclsidx);
            if(status != OS_SUCCESS) {
                state = ERROR_STATE;
                break;
            }
            
            if(fs_instance->is_memdiskv1)
            {
                ksystemramcpy(fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count, stream_base_offset % blk_size),
                    buffer,
                    start_lcls_size_to_write
                );
            } else {
                status = fs_instance->phylayer->read(
                    phyclsidx * cls_blk_count,
                    stream_base_offset % blk_size,
                    buffer,
                    start_lcls_size_to_write
                );
                if(status != OS_SUCCESS) {
                    state = ERROR_STATE;
                    break;
                }
            }
            midbase_lcls = start_lcls + 1;
            midcls_scanner = 0;
            state = MID;
            break; // 添加break以防止fall-through
        }
        
        case END_CLS: {
            uint64_t phyclsidx;
            status = fs_instance->filecluster_to_fscluster_in_idxtb(file_inode, end_lcls, phyclsidx);
            if(status != OS_SUCCESS) {
                state = ERROR_STATE;
                break;
            }
            
            if(fs_instance->is_memdiskv1)
            {
                ksystemramcpy(
                    fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count),
                    buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    end_lcls_size_to_write
                );
            } else {
                status = fs_instance->phylayer->read(
                    phyclsidx * cls_blk_count,
                    0,
                    buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    end_lcls_size_to_write
                );
                if(status != OS_SUCCESS) {
                    state = ERROR_STATE;
                    break;
                }
            }
            state = END;
            break; // 添加break保持一致性
        }
        
        case ERROR_STATE:
        case END:
        default:
            return state;
    }
    return state; // 修改返回值为state而不是OS_UNREACHABLE_CODE
}
