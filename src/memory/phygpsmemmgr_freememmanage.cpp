#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
#include "utils.h"
#include "DoubleLinkList.h"

/**
 * 对于下面的单独的页分配，释放函数
 * 其业务为一次从起始地址分配/释放一个页
 * 首先，自然会检查起始地址是否按要求对齐
 * 其次，若是试图分配/释放的页为不存在/非原子页/保留页
 * 试图分配已分配的页，会报错
 * 以及其它可能的报错触发
 */
/**
 * 当然，这些函数的原子节点上的父节点要判断原子节点存储形式
 * 是位图？（is_lowerlv_bitmap为1）一个bit还是2个bit？（is_reserved的话位图一个项2bit）
 * 不是位图就是PgControlBlockHeader项
 */
int PgsMemMgr::PgCBtb_lv0_entry_pgmanage(phyaddr_t addr, bool is_allocate,pgaccess access)
{
    return 0;
}
int PgsMemMgr::PgCBtb_lv1_entry_pgmanage(phyaddr_t addr, bool is_allocate,pgaccess access)
{
    return 0;
}
int PgsMemMgr::PgCBtb_lv2_entry_pgmanage(phyaddr_t addr, bool is_allocate,pgaccess access)
{
    return 0;
}
int PgsMemMgr::PgCBtb_lv3_entry_pgmanage(phyaddr_t addr, bool is_allocate,pgaccess access)
{
    return 0;
}
int PgsMemMgr::PgCBtb_lv4_entry_pgmanage(phyaddr_t addr, bool is_allocate,pgaccess access)
{
    return 0;
}
/**
 * 这里的align_require是（1ULL<<align_require）字节对齐
 * 输入的size_in_byte是字节数，向上取整到4kb
 */
void *PgsMemMgr::pgs_allocate(size_t size_in_byte,pgaccess access ,uint8_t align_require)
{
    return nullptr;
}

int PgsMemMgr::pgs_fixedaddr_allocate(IN phyaddr_t addr, IN size_t size_in_byte, pgaccess access,IN PHY_MEM_TYPE type)
{
    return 0;
}

int PgsMemMgr::pgs_clear(void *addr, size_t size_in_byte)
{
    return 0;
}

int PgsMemMgr::pgs_free(void *addr, size_t size_in_byte)
{
    return 0;
}

/**
 * 内部内存分配接口，
 * 传入基址，页队列，权限
 * 根据页队列解析出的项一个一个地分配
 * 分配途中若哪个页出现错误，则返回出错的页基地址
 * 外面有pgs_fixedaddr_allocate外部接口查询具体问题所在
 */
phyaddr_t PgsMemMgr::Inner_fixed_addr_alocate(phyaddr_t base, phymem_pgs_queue queue, pgaccess access)
{
    return OS_SUCCESS;
}
 PgsMemMgr::phymem_pgs_queue *PgsMemMgr::seg_to_queue(phyaddr_t base,uint64_t size_in_bytes){
    uint64_t end_addr = base + size_in_bytes;
    end_addr+=PAGE_SIZE_IN_LV[0]-1;
    end_addr&=~PAGE_OFFSET_MASK[0];
    phymem_pgs_queue* queue = new phymem_pgs_queue;
    if (queue==nullptr)
    {
        return nullptr;
    }
        phyaddr_t scan_addr = base;
    int queue_index = 0;
    
    // 遍历内存区域，确定最佳的页大小组合
    while (scan_addr < end_addr)
    {
        // 从最大支持的页大小开始尝试
        int pglv_index = Max_huge_pg_index;
        
        // 找到适合当前地址的最大页大小
        while (pglv_index >= 0)
        {
            // 检查地址对齐和是否超出内存区域
            if ((scan_addr & PAGE_OFFSET_MASK[pglv_index]) == 0 && // 地址对齐
                scan_addr + PAGE_SIZE_IN_LV[pglv_index] - 1 <= end_addr) // 不超出内存区域
            {
                break;
            }
            pglv_index--;
        }
        
        if (pglv_index < 0)
        {
            delete queue;
            return nullptr;
        }
        
        // 添加到队列
        if (queue->entry_count == 0 || 
            queue->entry[queue->entry_count - 1].pgs_lv != pglv_index)
        {
            // 新类型的页
            if (queue->entry_count >= 10)
            {
                delete queue;
                return nullptr; // 队列已满
            }
            queue->entry[queue->entry_count].pgs_lv = pglv_index;
            queue->entry[queue->entry_count].pgs_count = 1;
            queue->entry_count++;
        }
        else
        {
            // 相同类型的页，增加计数
            queue->entry[queue->entry_count - 1].pgs_count++;
        }
        
        scan_addr += PAGE_SIZE_IN_LV[pglv_index];
    }
    return queue;
}