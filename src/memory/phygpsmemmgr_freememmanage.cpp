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

phyaddr_t KernelSpacePgsMemMgr::Inner_fixed_addr_manage(
phyaddr_t base, 
phymem_pgs_queue queue, 
pgaccess access,
bool modify_pgtb
)
{
    phyaddr_t scan_addr=base;
    pgflags flag_of_pg={0 };
    flag_of_pg.physical_or_virtual_pg=0;
    flag_of_pg.is_occupied=access.is_occupyied;
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
    if (modify_pgtb)
    {
        /* code */
    }
    
    return scan_addr;
}
// 定义提前终止码
#define OS_EARLY_RETURN 100

/**
 * 处理PTE级别（最后一级）的页表项
 */
int KernelSpacePgsMemMgr::process_pte_level(
    uint64_t* pt_base, 
    lowerlv_PgCBtb* pt_PgCBtb, 
    uint64_t& scan_addr, 
    uint64_t endaddr,
    uint64_t pml4_index, 
    uint64_t pdpt_index, 
    uint64_t pd_index,
    uint64_t start_pt_index
) {
    for (int l = start_pt_index; l < 512; l++) {
        pgflags tmp_flags = pt_PgCBtb->entries[l].flags;
        if (tmp_flags.is_exist) {
            phyaddr_t page_base = (pml4_index << 39) + (pdpt_index << 30) + (pd_index << 21) + (l << 12);
            int result = pgtb_entry_convert(pt_base[l], pt_PgCBtb->entries[l], page_base);
            if (result != OS_SUCCESS) {
                return result;
            }
            
            scan_addr += PAGE_SIZE_IN_LV[0];
            if (scan_addr >= endaddr) {
                return OS_EARLY_RETURN;
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 处理PDE级别（第三级）的页表项
 */
int KernelSpacePgsMemMgr::process_pde_level(
    uint64_t* pd_base, 
    lowerlv_PgCBtb* pd_PgCBtb, 
    uint64_t& scan_addr, 
    uint64_t endaddr,
    uint64_t pml4_index, 
    uint64_t pdpt_index,
    uint64_t start_pd_index
) {
    for (int k = start_pd_index; k < 512; k++) {
        pgflags tmp_flags = pd_PgCBtb->entries[k].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理2MB大页
                phyaddr_t huge_2mb_base = (pml4_index << 39) + (pdpt_index << 30) + (k << 21);
                int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], huge_2mb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
                
                scan_addr += PAGE_SIZE_IN_LV[1];
                if (scan_addr >= endaddr) {
                    return OS_EARLY_RETURN;
                }
            } else {
                // 处理指向PT的普通PDE
                uint64_t* pt_base;
                lowerlv_PgCBtb* pt_PgCBtb = pd_PgCBtb->entries[k].base.lowerlvPgCBtb;
                
                if ((pd_base[k] & PageTableEntry::P_MASK) == 0) {
                    pt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                    if (pt_base == nullptr) {
                        kputsSecure("modify_pgtb_in_4lv:pgalloc failed for pt_base\n");
                        return OS_OUT_OF_MEMORY;
                    }
                    
                    int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], (phyaddr_t)pt_base);
                    if (result != OS_SUCCESS) {
                        pgtb_heap_ptr->free((phyaddr_t)pt_base);
                        return result;
                    }
                } else {
                    pt_base = (uint64_t*)(pd_base[k] & PDE::ADDR_PT_MASK);
                }
                
                // 处理PTE级别
                uint64_t start_pt_index = (scan_addr & lineaddr_index_filters::PT_INDEX_MASK_lv0) >> 12;
                int result = process_pte_level(pt_base, pt_PgCBtb, scan_addr, endaddr, 
                                              pml4_index, pdpt_index, k, start_pt_index);
                
                if (result == OS_EARLY_RETURN) {
                    return OS_EARLY_RETURN;
                } else if (result != OS_SUCCESS) {
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 处理PDPTE级别（第二级）的页表项
 */
int KernelSpacePgsMemMgr::process_pdpte_level(
    uint64_t* pdpt_base, 
    lowerlv_PgCBtb* pdpt_PgCBtb, 
    uint64_t& scan_addr, 
    uint64_t endaddr,
    uint64_t pml4_index,
    uint64_t start_pdpt_index
) {
    for (int j = start_pdpt_index; j < 512; j++) {
        pgflags tmp_flags = pdpt_PgCBtb->entries[j].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理1GB大页
                phyaddr_t huge_1gb_base = (pml4_index << 39) + (j << 30);
                int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], huge_1gb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
                
                scan_addr += PAGE_SIZE_IN_LV[2];
                if (scan_addr >= endaddr) {
                    return OS_EARLY_RETURN;
                }
            } else {
                // 处理指向PD的普通PDPTE
                uint64_t* pd_base;
                lowerlv_PgCBtb* pd_PgCBtb = pdpt_PgCBtb->entries[j].base.lowerlvPgCBtb;
                
                if ((pdpt_base[j] & PageTableEntry::P_MASK) == 0) {
                    pd_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                    if (pd_base == nullptr) {
                        kputsSecure("modify_pgtb_in_4lv:pgalloc failed for pd_base\n");
                        return OS_OUT_OF_MEMORY;
                    }
                    
                    int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], (phyaddr_t)pd_base);
                    if (result != OS_SUCCESS) {
                        pgtb_heap_ptr->free((phyaddr_t)pd_base);
                        return result;
                    }
                } else {
                    pd_base = (uint64_t*)(pdpt_base[j] & PDPTE::ADDR_PD_MASK);
                }
                
                // 处理PDE级别
                uint64_t start_pd_index = (scan_addr & lineaddr_index_filters::PD_INDEX_MASK_lv1) >> 21;
                int result = process_pde_level(pd_base, pd_PgCBtb, scan_addr, endaddr, 
                                              pml4_index, j, start_pd_index);
                
                if (result == OS_EARLY_RETURN) {
                    return OS_EARLY_RETURN;
                } else if (result != OS_SUCCESS) {
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 处理PML4E级别（第一级）的页表项
 */
int KernelSpacePgsMemMgr::process_pml4e_level(
    uint64_t* rootPgtb, 
    lowerlv_PgCBtb* root_PgCBtb, 
    uint64_t& scan_addr, 
    uint64_t endaddr,
    uint64_t start_pml4_index
) {
    for (uint16_t i = start_pml4_index; i < 512; i++) {
        pgflags tmp_flags = root_PgCBtb->entries[i].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                kputsSecure("modify_pgtb_in_4lv:atom entry in Pml4 not support\n");
                return OS_BAD_FUNCTION;
            } else {
                uint64_t* pdpt_base;
                
                if ((rootPgtb[i] & PageTableEntry::P_MASK) == 0) {
                    pdpt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                    if (pdpt_base == nullptr) {
                        kputsSecure("modify_pgtb_in_4lv:pgalloc failed for pdpt_base\n");
                        return OS_OUT_OF_MEMORY;
                    }
                    
                    int result = pgtb_entry_convert(rootPgtb[i], root_PgCBtb->entries[i], (phyaddr_t)pdpt_base);
                    if (result != OS_SUCCESS) {
                        pgtb_heap_ptr->free((phyaddr_t)pdpt_base);
                        return result;
                    }
                } else {
                    pdpt_base = (uint64_t*)(rootPgtb[i] & PML4E::ADDR_MASK);
                }
                
                lowerlv_PgCBtb* pdpt_PgCBtb = root_PgCBtb->entries[i].base.lowerlvPgCBtb;
                
                // 处理PDPTE级别
                uint64_t start_pdpt_index = (scan_addr & lineaddr_index_filters::PDPT_INDEX_MASK_lv2) >> 30;
                int result = process_pdpte_level(pdpt_base, pdpt_PgCBtb, scan_addr, endaddr, i, start_pdpt_index);
                
                if (result == OS_EARLY_RETURN) {
                    return OS_EARLY_RETURN;
                } else if (result != OS_SUCCESS) {
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}
int KernelSpacePgsMemMgr::process_pml5e_level(
    uint64_t* rootPgtb, 
    lowerlv_PgCBtb* root_PgCBtb, 
    uint64_t& scan_addr, 
    uint64_t endaddr,
    uint64_t start_pml5_index
) {
    for (uint16_t i = start_pml5_index; i < 512; i++) {
        pgflags tmp_flags = root_PgCBtb->entries[i].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                kputsSecure("modify_pgtb_in_5lvpg:atom entry in Pml5 not support\n");
                return OS_BAD_FUNCTION;
            } else {
                uint64_t* pml4_base;
                
                if ((rootPgtb[i] & PageTableEntry::P_MASK) == 0) {
                    pml4_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                    if (pml4_base == nullptr) {
                        kputsSecure("modify_pgtb_in_5lvpg:pgalloc failed for pml4_base\n");
                        return OS_OUT_OF_MEMORY;
                    }
                    
                    int result = pgtb_entry_convert(rootPgtb[i], root_PgCBtb->entries[i], (phyaddr_t)pml4_base);
                    if (result != OS_SUCCESS) {
                        pgtb_heap_ptr->free((phyaddr_t)pml4_base);
                        return result;
                    }
                } else {
                    pml4_base = (uint64_t*)(rootPgtb[i] & PML5E::ADDR_MASK);
                }
                
                lowerlv_PgCBtb* pml4_PgCBtb = root_PgCBtb->entries[i].base.lowerlvPgCBtb;
                
                // 处理PML4E级别
                uint64_t start_pml4_index = (scan_addr & lineaddr_index_filters::PML4_INDEX_MASK_lv3) >> 39;
                int result = process_pml4e_level(pml4_base, pml4_PgCBtb, scan_addr, endaddr,start_pml4_index);
                
                if (result == OS_EARLY_RETURN) {
                    return OS_EARLY_RETURN;
                } else if (result != OS_SUCCESS) {
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}
/**
 * 修改四级页表中的页表项
 * 这个函数现在通过调用分层处理函数来实现
 * 返回值为OS_SUCCESS或OS_EARLY_RETURN表示成功，其他值表示失败
 * OS_OUT_OF_MEMORY表示特殊堆内存耗尽
 */
int KernelSpacePgsMemMgr::modify_pgtb_in_4lv(uint64_t base, uint64_t endaddr) {
    lowerlv_PgCBtb* root_PgCBtb = rootlv4PgCBtb->base.lowerlvPgCBtb;
    uint64_t* rootPgtb = pgtb_heap_ptr->pgtb_root_phyaddr;
    uint64_t scan_addr = base;
    
    // 计算起始PML4索引
    uint64_t start_pml4_index = (scan_addr & lineaddr_index_filters::PML4_INDEX_MASK_lv3) >> 39;
    
    // 从PML4级别开始处理
    return process_pml4e_level(rootPgtb, root_PgCBtb, scan_addr, endaddr, start_pml4_index);
}
int KernelSpacePgsMemMgr::modify_pgtb_in_5lv(phyaddr_t base, uint64_t endaddr)
{
    lowerlv_PgCBtb* rootlv4PgCBtb;
    uint64_t* rootPgtb = pgtb_heap_ptr->pgtb_root_phyaddr;
    uint64_t scan_addr = base;
    // 计算起始PML5索引
    uint64_t start_pml5_index = (scan_addr & lineaddr_index_filters::PML5_INDEX_MASK_lv4) >> 48;
    
    // 从PML5级别开始处理
    return process_pml5e_level(rootPgtb, rootlv4PgCBtb, scan_addr, endaddr, start_pml5_index);
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