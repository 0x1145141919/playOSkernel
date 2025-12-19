#include "init_fs.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
/**写入inode指向的数据流的内容，
 *参数:
 *  the_inode: inode对象
 *  stream_base_offset: 数据流的起始偏移
 *  size: 写入的数据大小
 *  buffer: 写入到的数据存放的缓冲区
 *此函数为内部接口，若超出文件大小会报错
 */
int init_fs_t::inode_content_write(
    Inode the_inode, uint64_t stream_base_offset, uint64_t data_stream_size, uint8_t *buffer)
{
    // 参数有效性检查
    if (!buffer || data_stream_size == 0) {
        return OS_INVALID_PARAMETER;
    }

    // 检查写入范围是否超出文件大小
    if (stream_base_offset + data_stream_size > the_inode.file_size) {
        return OS_INVALID_PARAMETER;
    }

    // 根据inode的数据组织模式选择迭代器
    if (the_inode.flags.extents_or_indextable) {
        // Extent模式处理
        extents_rw_iterator iter(this, the_inode, buffer, stream_base_offset, data_stream_size);
        
        while (true) {
            auto state = iter.wnext();
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
            data_stream_size
        );
        
        while (true) {
            int state = iter.wnext(this);
            if (state == idxtbmode_file_Iteration::END) {
                return OS_SUCCESS;
            }
            if (state == idxtbmode_file_Iteration::ERROR_STATE) {
                return OS_IO_ERROR;
            }
        }
    }
}
init_fs_t::idxtbmode_file_Iteration::idxtbmode_file_Iteration(Inode file_inode, uint8_t *buffer, uint64_t stream_base_offset, uint16_t cls_blk_count, uint32_t blk_size, uint64_t size_to_write)
:file_inode(file_inode), buffer(buffer), stream_base_offset(stream_base_offset), cls_blk_count(cls_blk_count), blk_size(blk_size),left_bytes_to_write(size_to_write)
{
    cls_size=blk_size*cls_blk_count;
    start_lcls=stream_base_offset/cls_size;
    start_lcls_startvytes_offset=stream_base_offset%cls_size;
    bool is_start_cls_full=left_bytes_to_write>(cls_size-start_lcls_startvytes_offset);
    if(is_start_cls_full)
    {
        start_lcls_size_to_write=cls_size-start_lcls_startvytes_offset;
    }else{start_lcls_size_to_write=left_bytes_to_write;}
    left_bytes_to_write-=start_lcls_size_to_write;
    midbase_lcls=start_lcls+1;
    midcls_count=left_bytes_to_write/cls_size;
    end_lcls=midbase_lcls+midcls_count;
    end_lcls_size_to_write=left_bytes_to_write%cls_size;
}
int init_fs_t::idxtbmode_file_Iteration::wnext(init_fs_t *fs_instance)
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
                ksystemramcpy(buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count),
                    cls_size
                );
            } else {
                status = fs_instance->phylayer->write(
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
                ksystemramcpy(buffer,
                    fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count, stream_base_offset % blk_size),
                    start_lcls_size_to_write
                );
            } else {
                status = fs_instance->phylayer->write(
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
            if(end_lcls_size_to_write == 0){
                state = END;
                break;
            }
            uint64_t phyclsidx;
            status = fs_instance->filecluster_to_fscluster_in_idxtb(file_inode, end_lcls, phyclsidx);
            if(status != OS_SUCCESS) {
                state = ERROR_STATE;
                break;
            }
            
            if(fs_instance->is_memdiskv1)
            {
                ksystemramcpy(buffer + cls_size * midcls_scanner + start_lcls_size_to_write,
                    fs_instance->memdiskv1_blockdevice->get_vaddr(phyclsidx * cls_blk_count),
                    end_lcls_size_to_write
                );
            } else {
                status = fs_instance->phylayer->write(
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

