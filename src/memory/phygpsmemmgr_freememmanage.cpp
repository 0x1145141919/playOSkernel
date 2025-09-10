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

phyaddr_t PgsMemMgr::Inner_fixed_addr_manage(phyaddr_t base, phymem_pgs_queue queue, bool alloc_or_free, pgaccess access)
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
void *PgsMemMgr::pgs_allocate(uint64_t size_in_byte, pgaccess access, uint8_t align_require)
{
    if(size_in_byte==0)return nullptr;
    phyaddr_t scan_addr = 0x100000;
    size_in_byte += PAGE_OFFSET_MASK[0];
    size_in_byte &= ~PAGE_OFFSET_MASK[0];
    uint64_t align_require_mask = (1ULL << align_require) - 1;
    phy_memDesriptor*usage_query_result=nullptr;
    while(true){
    usage_query_result = queryPhysicalMemoryUsage(scan_addr,size_in_byte);
    if(usage_query_result==(phy_memDesriptor*)OS_OUT_OF_MEMORY)
    return (void*)OS_OUT_OF_MEMORY;    
    if(usage_query_result==nullptr)return nullptr;
    if(usage_query_result[0].Type==OS_ALLOCATABLE_MEMORY&&
    (usage_query_result[0].Attribute&1)==0&&
    usage_query_result[0].NumberOfPages*PAGE_SIZE_IN_LV[0]>=size_in_byte)
    {
        delete usage_query_result;
        break;
    }

    else {
        scan_addr+=PAGE_SIZE_IN_LV[0]*usage_query_result[0].NumberOfPages;
        scan_addr+=align_require_mask;
    scan_addr&=~align_require_mask;
    delete usage_query_result;
    continue;
    }
    int i = 0;
        for (; usage_query_result[i].Type!=EfiReservedMemoryType; i++)
        {
        }
        scan_addr=usage_query_result[i-1].PhysicalStart;
    scan_addr+=align_require_mask;
    scan_addr&=~align_require_mask;

    delete usage_query_result;
    }
    
    phymem_pgs_queue*queue=seg_to_queue(scan_addr,size_in_byte);
    phyaddr_t alloced_addr= Inner_fixed_addr_manage(scan_addr,*queue,true,access);
    if (alloced_addr!=scan_addr+size_in_byte)
    {
        delete queue;
        return nullptr;
    }
    delete usage_query_result;
    delete queue;
    return (void*)scan_addr;
}

int PgsMemMgr::pgs_fixedaddr_allocate(IN phyaddr_t addr, IN size_t size_in_byte, pgaccess access)
{
    if(addr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
    size_in_byte += PAGE_OFFSET_MASK[0];
    size_in_byte &= ~PAGE_OFFSET_MASK[0];
    phy_memDesriptor*usage_query_result = queryPhysicalMemoryUsage(addr,size_in_byte);
    if(usage_query_result[1].Type!=EfiReservedMemoryType)
    {
            delete usage_query_result;
            return OS_MEMRY_ALLOCATE_FALT;
     }
    //这个判定是因为如果确认是空闲内存，那么返回的结果必然只有一项，第二项必然是空不是一项可以返回不可分配
    if(usage_query_result[0].Type!=OS_ALLOCATABLE_MEMORY&&
    usage_query_result[0].Attribute&1!=0)
    {
        delete usage_query_result;
        return OS_MEMRY_ALLOCATE_FALT;
    }
    //确定这一项是100%可分配内存，完全空闲
    delete usage_query_result;
    phymem_pgs_queue*queue=seg_to_queue(addr,size_in_byte);
    phyaddr_t alloced_addr= Inner_fixed_addr_manage(addr,*queue,true,access);
    if (alloced_addr!=addr+size_in_byte)
    {
        return OS_MEMRY_ALLOCATE_FALT;
    }
    delete queue;
    return OS_SUCCESS;
}


int PgsMemMgr::pgs_free(phyaddr_t addr, size_t size_in_byte)
{   
    
        if(addr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
    size_in_byte += PAGE_OFFSET_MASK[0];
    size_in_byte &= ~PAGE_OFFSET_MASK[0];
    phy_memDesriptor*usage_query_result = queryPhysicalMemoryUsage(addr,size_in_byte);
    for(;usage_query_result->Type!=EfiReservedMemoryType;usage_query_result++)
    {
        if(usage_query_result->Type!=OS_ALLOCATABLE_MEMORY)
        {
            kputsSecure("PgsMemMgr::pgs_free:invalid memtype when analyse result from queryPhysicalMemoryUsage\n");    
            return OS_MEMRY_ALLOCATE_FALT;
        }
        if(usage_query_result->Attribute&1!=1)
        {
            kputsSecure("PgsMemMgr::pgs_free:try to free freed memory\n");
        }
    }
    delete usage_query_result;
    phymem_pgs_queue*queue=seg_to_queue(addr,size_in_byte);
    pgaccess access={0};
    access.is_kernel=true;
    access.is_writeable=true;
    access.is_readable=true;
    access.is_executable=false;
    phyaddr_t alloced_addr= Inner_fixed_addr_manage(addr,*queue,false,access);
    if (alloced_addr!=addr+size_in_byte)
    {
        return OS_MEMRY_ALLOCATE_FALT;
    }
    delete queue;
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