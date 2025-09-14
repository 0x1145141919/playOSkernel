#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#include "DoubleLinkList.h"

/**
 * 这里的align_require是（1ULL<<align_require）字节对齐
 * 输入的size_in_byte是字节数，向上取整到4kb
 * alloc_or_free1表示alloc.,0表示free
 */

phyaddr_t KernelSpacePgsMemMgr::Inner_fixed_addr_manage(phyaddr_t base, phymem_pgs_queue queue, bool alloc_or_free, pgaccess access)
{
    phyaddr_t scan_addr=base;
    pgflags flag_of_pg={0 };
    flag_of_pg.physical_or_virtual_pg=0;
    flag_of_pg.is_occupied=alloc_or_free;
    flag_of_pg.is_atom=1;
    flag_of_pg.is_exist=1;
    flag_of_pg.is_kernel=access.is_kernel;
    flag_of_pg.is_readable=access.is_readable;
    flag_of_pg.is_writable=access.is_writeable;
    flag_of_pg.is_executable=access.is_executable;
    for(int i=0;i<queue.entry_count;i++)
    {
        uint8_t lv=queue.entry[i].pgs_lv;
        flag_of_pg.pg_lv=lv;
        for(int j=0;j<queue.entry[i].pgs_count;j++)
        {
            if((this->*PgCBtb_construct_func[lv])(scan_addr,flag_of_pg)!=OS_SUCCESS)
            return scan_addr;
            scan_addr+=PAGE_SIZE_IN_LV[lv];
        }
    }
    return scan_addr;
}
void *KernelSpacePgsMemMgr::pgs_allocate(uint64_t size_in_byte, uint8_t align_require)
{

    return nullptr;
}

int KernelSpacePgsMemMgr::pgs_fixedaddr_allocate(IN phyaddr_t addr, IN size_t size_in_byte)
{

    return OS_SUCCESS;
}


int KernelSpacePgsMemMgr::pgs_free(phyaddr_t addr )
{   
    return OS_SUCCESS;
}

 KernelSpacePgsMemMgr::phymem_pgs_queue *KernelSpacePgsMemMgr::seg_to_queue(phyaddr_t base,uint64_t size_in_bytes){
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