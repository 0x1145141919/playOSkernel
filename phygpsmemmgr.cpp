#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
const uint8_t masks[8]={128,64,32,16,8,4,2,1};
PgsMemMgr gPgsMemMgr;
bool getbit(pgsbitmap* bitmap,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    return (map[index>>3]&masks[index&7])!=0;
}
void setbit(pgsbitmap*bitmap,bool value,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    if(value)
        map[index>>3]|=masks[index&7];
    else
        map[index>>3]&=~masks[index&7];
}
void setbits(pgsbitmap*bitmap,bool value,uint16_t Start_index,uint16_t len_in_bits)
{
    int bits_left=len_in_bits;
    uint8_t * map_8bit=(uint8_t*)bitmap;
    uint8_t fillcontent8=value?0xff:0;
    uint64_t* map_64bit=(uint64_t*)bitmap;
    uint64_t fillcontent64=value?0xffffffffffffffff:0;
    for (int i = Start_index; i < Start_index+len_in_bits; )
    {
       if (i&63ULL)
       {
not_aligned_6bits:
        if(i&7ULL)
        {
not_aligned_3bits:            
            setbit(bitmap,value,i);
            i++;
            bits_left--;
        }else{
            if(bits_left>=8)
            {
                map_8bit[i>>3]=fillcontent8;
                bits_left-=8;
                i+=8;
            }
            else{
                goto not_aligned_3bits;
            }
        }
       }else{
        if(bits_left>=64)
        {
            map_64bit[i>>6]=fillcontent64;  
            bits_left-=64;
            i+=64;
        }
        else{
            goto not_aligned_6bits;
        }
       }  
    }
}

/**
 * @brief 
 * 
 * 
 * @return 
 *根据全局物理内存描述符表初始化一个类页表的数据结构，
 * 以便后面转换为真正页表
 * 为了兼容五级页表在四级页表的情况下lv4级别的表是只有一项的，也只为其分配一项
 */
void PgsMemMgr::Init()
{
    uint64_t cr4_tmp;
    int status=0;
    flags.is_run_len_encoding_enabled=0;
     asm volatile("mov %%cr4,%0" : "=r"(cr4_tmp));
     cpu_pglv=(cr4_tmp&(1ULL<<12))?5:4;
     if(cpu_pglv==5)
     rootlv4PgCBtb=new PgControlBlockHeader[512];
     else rootlv4PgCBtb=new PgControlBlockHeader;
     phy_memDesriptor* phy_memDesTb=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
     uint64_t entryCount=gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
     uint64_t entryCount_Without_tail_reserved = entryCount  -1;
     for (; ; entryCount_Without_tail_reserved--)
     {
        if(phy_memDesTb[entryCount_Without_tail_reserved].Type==EfiReservedMemoryType)continue;
        else break;
     }//这一步操作是因为可能有些虚拟机的表在最后会有大量不可分配的保留型物理内存，必须去除
     entryCount_Without_tail_reserved++;
     phyaddr_t top_pg_ptr=0;
     for(int i=0;i<entryCount_Without_tail_reserved;i++)
     {
        status=construct_pgsbasedon_phy_memDescriptor(phy_memDesTb[i]);
        if (status!=OS_SUCCESS)
        {
            kputsSecure("construct_pgsbasedon_phy_memDescriptor failed");
            return ;
        }
        
     }
     PgsMemMgr::flags.is_run_len_encoding_enabled=0;
     int pre_pgtb_optimize();

}
int PgsMemMgr::PgCBtb_lv4_entry_construct(phyaddr_t addr, pgflags flags)
{
    int status=0;
    uint64_t lv4_index=(addr&PML5_INDEX_MASK_lv4)>>48;
    lv4_index=cpu_pglv==5?lv4_index:0;
    flags.pg_lv=4;
    rootlv4PgCBtb[lv4_index].flags = flags;
    if (flags.is_atom==0)
    {
        if (flags.is_lowerlv_bitmap==1)
        {
           rootlv4PgCBtb[lv4_index].base.bitmap= new lowerlv_bitmap;
           if (rootlv4PgCBtb[lv4_index].base.bitmap==nullptr)
           {
            return OS_OUT_OF_MEMORY;
           }
           
           gKpoolmemmgr.clear(rootlv4PgCBtb[lv4_index].base.bitmap->bitmap);
        }else
        {
            rootlv4PgCBtb[lv4_index].base.lowerlvPgCBtb= new lowerlv_PgCBtb;
            if (rootlv4PgCBtb[lv4_index].base.lowerlvPgCBtb==nullptr)
            {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(rootlv4PgCBtb[lv4_index].base.lowerlvPgCBtb);
        }
        
    }
    
    return OS_SUCCESS;
}

int PgsMemMgr::PgCBtb_lv3_entry_construct(phyaddr_t addr, pgflags flags)
{

    uint16_t lv3_index=(addr&PML4_INDEX_MASK_lv3)>>39;
    uint64_t lv4_index=(addr&PML5_INDEX_MASK_lv4)>>48;
    PgCBlv4header* lv4_PgCBHeader = cpu_pglv == 5 ? &rootlv4PgCBtb[lv4_index] : rootlv4PgCBtb;
    pgflags higher_uninitialized_entry_flags;
    higher_uninitialized_entry_flags.is_exist=1;
    higher_uninitialized_entry_flags.is_atom=0;
    higher_uninitialized_entry_flags.is_lowerlv_bitmap=0;
    higher_uninitialized_entry_flags.physical_or_virtual_pg=0;
    higher_uninitialized_entry_flags.is_kernel=1;
    // 确保lv4条目存在
    if (lv4_PgCBHeader->flags.is_exist != 1) {
 
        higher_uninitialized_entry_flags.pg_lv = 3;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv4_index;
        int status = PgCBtb_lv4_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    PgCBlv3header* lv3_PgCBHeader = &lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];

    // 初始化lv3条目
    lv3_PgCBHeader->flags = flags;
     if (flags.is_atom==0)
    {
        if (flags.is_lowerlv_bitmap==1)
        {
           lv3_PgCBHeader->base.bitmap= new lowerlv_bitmap;
           if (lv3_PgCBHeader->base.bitmap==nullptr)
           {
            return OS_OUT_OF_MEMORY;
           }
           
           gKpoolmemmgr.clear(lv3_PgCBHeader->base.bitmap);
        }else
        {
            lv3_PgCBHeader->base.lowerlvPgCBtb= new lowerlv_PgCBtb;
            if (lv3_PgCBHeader->base.lowerlvPgCBtb==nullptr)
            {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(lv3_PgCBHeader->base.lowerlvPgCBtb);
        }
        
    }
    if (lv3_PgCBHeader->base.lowerlvPgCBtb == nullptr)
        return OS_OUT_OF_MEMORY;

    return OS_SUCCESS;
}
int PgsMemMgr::PgCBtb_lv2_entry_construct(phyaddr_t addr, pgflags flags)
{

    uint16_t lv2_index=(addr&PDPT_INDEX_MASK_lv2)>>30;
    uint16_t lv3_index=(addr&PML4_INDEX_MASK_lv3)>>39;
    uint64_t lv4_index=(addr&PML5_INDEX_MASK_lv4)>>48;
    
    PgCBlv4header* lv4_PgCBHeader = cpu_pglv == 5 ? &rootlv4PgCBtb[lv4_index] : rootlv4PgCBtb;
    pgflags higher_uninitialized_entry_flags;
    higher_uninitialized_entry_flags.is_exist = 1;
    higher_uninitialized_entry_flags.is_atom = 0;
    higher_uninitialized_entry_flags.is_lowerlv_bitmap = 0;
    higher_uninitialized_entry_flags.physical_or_virtual_pg = 0;
    higher_uninitialized_entry_flags.is_kernel = 1;

    // 确保lv4条目存在
    if (lv4_PgCBHeader->flags.is_exist != 1) {
        higher_uninitialized_entry_flags.pg_lv = 4;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv4_index;
        int status = PgCBtb_lv4_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    // 确保lv3条目存在
    PgCBlv3header* lv3_PgCBHeader = &lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if (lv3_PgCBHeader->flags.is_exist != 1) {
        higher_uninitialized_entry_flags.pg_lv = 3;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv3_index;
        int status = PgCBtb_lv3_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    PgCBlv2header* lv2_PgCBHeader = &lv3_PgCBHeader->base.lowerlvPgCBtb->entries[lv2_index];
    
    // 初始化lv2条目
    lv2_PgCBHeader->flags = flags;
    if (flags.is_atom == 0) {
        if (flags.is_lowerlv_bitmap == 1) {
            lv2_PgCBHeader->base.bitmap = new lowerlv_bitmap;
            if (lv2_PgCBHeader->base.bitmap == nullptr) {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(lv2_PgCBHeader->base.bitmap->bitmap);
        } else {
            lv2_PgCBHeader->base.lowerlvPgCBtb = new lowerlv_PgCBtb;
            if (lv2_PgCBHeader->base.lowerlvPgCBtb == nullptr) {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(lv2_PgCBHeader->base.lowerlvPgCBtb);
        }
    }

    return OS_SUCCESS;
}

int PgsMemMgr::PgCBtb_lv1_entry_construct(phyaddr_t addr, pgflags flags)
{

    uint16_t lv1_index=(addr&PD_INDEX_MASK_lv1)>>21;
    uint16_t lv2_index=(addr&PDPT_INDEX_MASK_lv2)>>30;
    uint16_t lv3_index=(addr&PML4_INDEX_MASK_lv3)>>39;
    uint64_t lv4_index=(addr&PML5_INDEX_MASK_lv4)>>48;
    
    PgCBlv4header* lv4_PgCBHeader = cpu_pglv == 5 ? &rootlv4PgCBtb[lv4_index] : rootlv4PgCBtb;
    pgflags higher_uninitialized_entry_flags;
    higher_uninitialized_entry_flags.is_exist = 1;
    higher_uninitialized_entry_flags.is_atom = 0;
    higher_uninitialized_entry_flags.is_lowerlv_bitmap = 0;
    higher_uninitialized_entry_flags.physical_or_virtual_pg = 0;
    higher_uninitialized_entry_flags.is_kernel = 1;

    // 确保lv4条目存在
    if (lv4_PgCBHeader->flags.is_exist != 1) {
        higher_uninitialized_entry_flags.pg_lv = 4;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv4_index;
        int status = PgCBtb_lv4_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    // 确保lv3条目存在
    PgCBlv3header* lv3_PgCBHeader = &lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if (lv3_PgCBHeader->flags.is_exist != 1) {
        higher_uninitialized_entry_flags.pg_lv = 3;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv3_index;
        int status = PgCBtb_lv3_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    // 确保lv2条目存在
    PgCBlv2header* lv2_PgCBHeader = &lv3_PgCBHeader->base.lowerlvPgCBtb->entries[lv2_index];
    if (lv2_PgCBHeader->flags.is_exist != 1) {
        higher_uninitialized_entry_flags.pg_lv = 2;
        higher_uninitialized_entry_flags.start_index = higher_uninitialized_entry_flags.end_index = lv2_index;
        int status = PgCBtb_lv2_entry_construct(addr, higher_uninitialized_entry_flags);
        if (status != OS_SUCCESS)
            return status;
    }

    PgCBlv1header* lv1_PgCBHeader = &lv2_PgCBHeader->base.lowerlvPgCBtb->entries[lv1_index];
    
    // 初始化lv1条目
    lv1_PgCBHeader->flags = flags;
    if (flags.is_atom == 0) {
        if (flags.is_lowerlv_bitmap == 1) {
            lv1_PgCBHeader->base.bitmap = new lowerlv_bitmap;
            if (lv1_PgCBHeader->base.bitmap == nullptr) {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(lv1_PgCBHeader->base.bitmap->bitmap);
        } else {
            lv1_PgCBHeader->base.lowerlvPgCBtb = new lowerlv_PgCBtb;
            if (lv1_PgCBHeader->base.lowerlvPgCBtb == nullptr) {
                return OS_OUT_OF_MEMORY;
            }
            gKpoolmemmgr.clear(lv1_PgCBHeader->base.lowerlvPgCBtb);
        }
    }

    return OS_SUCCESS;
}


int PgsMemMgr::PgCBtb_lv0_entry_construct(phyaddr_t addr, pgflags flags)
{
    int status=0;
    uint16_t lv0_index=(addr&PT_INDEX_MASK_lv0)>>12;
    uint16_t lv1_index=(addr&PD_INDEX_MASK_lv1)>>21;
    uint16_t lv2_index=(addr&PDPT_INDEX_MASK_lv2)>>30;
    uint16_t lv3_index=(addr&PML4_INDEX_MASK_lv3)>>39;
    uint64_t lv4_index=(addr&PML5_INDEX_MASK_lv4)>>48;
    pgflags higher_uninitialized_entry_flags;
    higher_uninitialized_entry_flags.is_exist=1;
    higher_uninitialized_entry_flags.is_atom=0;
    higher_uninitialized_entry_flags.is_lowerlv_bitmap=0;
    higher_uninitialized_entry_flags.physical_or_virtual_pg=0;
    higher_uninitialized_entry_flags.is_kernel=1;
    PgCBlv4header*lv4_PgCBHeader=cpu_pglv==5?&rootlv4PgCBtb[lv4_index]:rootlv4PgCBtb;
    if(lv4_PgCBHeader->flags.is_exist!=1){
        
        higher_uninitialized_entry_flags.pg_lv=4;
        higher_uninitialized_entry_flags.start_index=higher_uninitialized_entry_flags.end_index=lv4_index;
        status=PgCBtb_lv4_entry_construct(addr,higher_uninitialized_entry_flags);
        if (status!=OS_SUCCESS)
        {
            return status;
        }
        
    }
    PgCBlv3header*lv3_PgCBHeader=&lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if(lv3_PgCBHeader->flags.is_exist!=1){
        higher_uninitialized_entry_flags.pg_lv=3;
        higher_uninitialized_entry_flags.start_index=higher_uninitialized_entry_flags.end_index=lv3_index;
        status=PgCBtb_lv3_entry_construct(addr,higher_uninitialized_entry_flags);
        if (status!=OS_SUCCESS)
        {
            return status;
        }
    }
    PgCBlv2header*lv2_PgCBHeader=&lv3_PgCBHeader->base.lowerlvPgCBtb->entries[lv2_index];
    if(lv2_PgCBHeader->flags.is_exist!=1){
        higher_uninitialized_entry_flags.pg_lv=2;
        higher_uninitialized_entry_flags.start_index=higher_uninitialized_entry_flags.end_index=lv2_index;
        status=PgCBtb_lv2_entry_construct(addr,higher_uninitialized_entry_flags);
        if (status!=OS_SUCCESS)
        {
            return status;
        }
    }
    PgCBlv1header*lv1_PgCBHeader=&lv2_PgCBHeader->base.lowerlvPgCBtb->entries[lv1_index];
    if(lv1_PgCBHeader->flags.is_exist!=1){
        higher_uninitialized_entry_flags.pg_lv=1;
        higher_uninitialized_entry_flags.start_index=higher_uninitialized_entry_flags.end_index=lv1_index;
        status=PgCBtb_lv1_entry_construct(addr,higher_uninitialized_entry_flags);
        if (status!=OS_SUCCESS)
        {
            return status;
        }
    }
    PgCBlv0header*lv0_PgCBHeader=&lv1_PgCBHeader->base.lowerlvPgCBtb->entries[lv0_index];
    lv0_PgCBHeader->flags=flags;
    return OS_SUCCESS;
}
int PgsMemMgr::construct_pgsbasedon_phy_memDescriptor(phy_memDesriptor memDescriptor)
{
    int status;
    PHY_MEM_TYPE type = (PHY_MEM_TYPE)memDescriptor.Type;
    phyaddr_t base = memDescriptor.PhysicalStart;
    uint64_t numof_4kbgs = memDescriptor.NumberOfPages;
    uint64_t seg_flags = memDescriptor.Attribute;
    phyaddr_t end_addr = base + numof_4kbgs * PAGE_SIZE_IN_LV[0] ; // 结束地址是最后一个字节的地址
    
    phymem_pgs_queue* queue = new phymem_pgs_queue;
    if (queue == nullptr)
    {
        return OS_OUT_OF_MEMORY;
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
            return OS_INVALID_ADDRESS;
        }
        
        // 添加到队列
        if (queue->entry_count == 0 || 
            queue->entry[queue->entry_count - 1].pgs_lv != pglv_index)
        {
            // 新类型的页
            if (queue->entry_count >= 10)
            {
                delete queue;
                return OS_OUT_OF_MEMORY; // 队列已满
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
    
    // 设置页标志
    pgflags flags;
    flags.physical_or_virtual_pg = 0; // 物理页
    flags.is_exist = 1; // 存在
    flags.is_atom = 1; // 原子节点
    flags.is_dirty = 0; // 初始未修改
    flags.is_lowerlv_bitmap = 0; // 不使用位图
    flags.is_locked = 0; // 未锁定
    flags.is_shared = 0; // 未共享
    flags.is_reserved = (type == EFI_RESERVED_MEMORY_TYPE || 
                        type == EFI_UNUSABLE_MEMORY || 
                        type == EFI_ACPI_MEMORY_NVS||
                        type == EFI_RUNTIME_SERVICES_CODE||
                    type==EFI_RUNTIME_SERVICES_DATA||
                    type==EFI_MEMORY_MAPPED_IO||
                    type==EFI_MEMORY_MAPPED_IO_PORT_SPACE||
                    type==EFI_PAL_CODE||
                    type==EFI_PERSISTENT_MEMORY||
                type==EFI_ACPI_RECLAIM_MEMORY) ? 1 : 0; // 保留内存
    flags.is_occupied = (type == freeSystemRam || 
                        type == EFI_UNACCEPTED_MEMORY_TYPE) ? 0 : 1; // 是否被占用
    flags.is_kernel = 1; // 内核内存
    flags.is_readable = 1; // 可读
    
    // 设置可写标志
    flags.is_writable = (type == EFI_LOADER_DATA || 
                        type == EFI_BOOT_SERVICES_DATA || 
                        type == EFI_RUNTIME_SERVICES_DATA || 
                        type == freeSystemRam || 
                        type == OS_KERNEL_DATA || 
                        type == OS_KERNEL_STACK) ? 1 : 0;
    
    // 设置可执行标志
    flags.is_executable = (type == EFI_LOADER_CODE || 
                          type == EFI_BOOT_SERVICES_CODE || 
                          type == EFI_RUNTIME_SERVICES_CODE || 
                          type == OS_KERNEL_CODE) ? 1 : 0;
    
    flags.is_run_len_encoding_enabled = 0; // 初始禁用游程编码
    flags.start_index = 0;
    flags.end_index = 0;
    
    // 根据队列创建页表项
    scan_addr = base;
    for (int i = 0; i < queue->entry_count; i++)
    {
        for (int j = 0; j < queue->entry[i].pgs_count; j++)
        {
            flags.pg_lv = queue->entry[i].pgs_lv;
            
            switch (queue->entry[i].pgs_lv)
            {
            case 0:
                status = PgCBtb_lv0_entry_construct(scan_addr, flags);
                break;
            case 1:
                status = PgCBtb_lv1_entry_construct(scan_addr, flags);
                break;
            case 2:
                status = PgCBtb_lv2_entry_construct(scan_addr, flags);
                break;
            case 3:
                status = PgCBtb_lv3_entry_construct(scan_addr, flags);
                break;
            case 4:
                status = PgCBtb_lv4_entry_construct(scan_addr, flags);
                break;
            default:
                status = OS_INVALID_PARAMETER;
                break;
            }
            
            if (status != OS_SUCCESS)
            {
                gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
                delete queue;
                return status;
            }
            
            scan_addr += PAGE_SIZE_IN_LV[queue->entry[i].pgs_lv];
        }
    }
    
    delete queue;
    return OS_SUCCESS;
}
// 在 PgsMemMgr 类中添加以下公共方法
void PgsMemMgr::PrintPgsMemMgrStructure()
{
    kputsSecure("=== Page Table Structure ===\n");
    
    // 打印基本信息
    kputsSecure("CPU Page Level: ");
    kpnumSecure(&cpu_pglv, UNDEC, 1);
    kputsSecure(" (");
    kpnumSecure(&cpu_pglv, UNHEX, 1);
    kputsSecure(")\n");
    
    kputsSecure("Kernel Space CR3: ");
    kpnumSecure(&kernel_space_cr3, UNHEX, 8);
    kputsSecure("\n");
    
    // 打印标志位
    kputsSecure("Flags: RunLengthEncoding=");
    uint64_t temp_flag = flags.is_run_len_encoding_enabled;
    kpnumSecure(&temp_flag, UNDEC, 1);
    kputsSecure(", PgtbSituate=");
    uint64_t temp_flag2 = flags.Pgtb_situate;
    kpnumSecure(&temp_flag2, UNDEC, 1);
    kputsSecure("\n\n");
    
    // 打印各级页表结构
    if (cpu_pglv == 5)
    {
        // 五级页表模式
        for (int i = 0; i < 512; i++)
        {
            if (rootlv4PgCBtb[i].flags.is_exist)
            {
                kputsSecure("PML5[");
                kpnumSecure(&i, UNDEC, 3);
                kputsSecure("]: ");
                PrintPageTableEntry(&rootlv4PgCBtb[i], 4);
                
                // 打印下一级
                if (!rootlv4PgCBtb[i].flags.is_atom && rootlv4PgCBtb[i].base.lowerlvPgCBtb)
                {
                    PrintLevel4Table(rootlv4PgCBtb[i].base.lowerlvPgCBtb, i);
                }
                kputsSecure("\n");
            }
        }
    }
    else
    {
        // 四级页表模式
        kputsSecure("PML4[0]: ");
        PrintPageTableEntry(rootlv4PgCBtb, 4);
        
        // 打印下一级
        if (!rootlv4PgCBtb->flags.is_atom && rootlv4PgCBtb->base.lowerlvPgCBtb)
        {
            PrintLevel4Table(rootlv4PgCBtb->base.lowerlvPgCBtb, 0);
        }
        kputsSecure("\n");
    }
}

// 辅助函数：打印四级表
void PgsMemMgr::PrintLevel4Table(lowerlv_PgCBtb* table, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if ((table->entries[i]).flags.is_exist)
        {
            kputsSecure("  PML4[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PDPT[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 3);
            
            // 打印下一级
            if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel3Table(table->entries[i].base.lowerlvPgCBtb, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印三级表
void PgsMemMgr::PrintLevel3Table(lowerlv_PgCBtb* table, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("    PDPT[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PD[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 2);
            
            // 打印下一级
            if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel2Table(table->entries[i].base.lowerlvPgCBtb, grandParentIndex, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印二级表
void PgsMemMgr::PrintLevel2Table(lowerlv_PgCBtb* table, int greatGrandParentIndex, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("      PD[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PT[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 1);
            
            // 打印下一级
            if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel1Table(table->entries[i].base.lowerlvPgCBtb, greatGrandParentIndex, grandParentIndex, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印一级表
void PgsMemMgr::PrintLevel1Table(lowerlv_PgCBtb* table, int greatGreatGrandParentIndex, int greatGrandParentIndex, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("        PT[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->Page[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 0);
            
            // 计算并打印物理地址
            uint64_t physicalAddr = CalculatePhysicalAddress(
                greatGreatGrandParentIndex, greatGrandParentIndex, 
                grandParentIndex, parentIndex, i, 0);
            
            kputsSecure(" PhysAddr: ");
            kpnumSecure(&physicalAddr, UNHEX, 8);
            kputsSecure("\n");
        }
    }
}

// 辅助函数：计算物理地址
uint64_t PgsMemMgr::CalculatePhysicalAddress(int index5, int index4, int index3, int index2, int index1, int index0)
{
    uint64_t addr = 0;
    
    if (cpu_pglv == 5)
    {
        addr |= (static_cast<uint64_t>(index5) << 48);
    }
    
    addr |= (static_cast<uint64_t>(index4) << 39);
    addr |= (static_cast<uint64_t>(index3) << 30);
    addr |= (static_cast<uint64_t>(index2) << 21);
    addr |= (static_cast<uint64_t>(index1) << 12);
    addr |= (static_cast<uint64_t>(index0));
    
    return addr;
}

// 辅助函数：打印页表项信息
void PgsMemMgr::PrintPageTableEntry(PgControlBlockHeader* entry, int level)
{
    // 打印级别
    kputsSecure((char*)"L");
    kpnumSecure(&level, UNDEC, 1);
    kputsSecure((char*)": ");
    
    // 打印基本标志
    kputsSecure(entry->flags.physical_or_virtual_pg ? (char*)"Virtual " : (char*)"Physical ");
    kputsSecure(entry->flags.is_exist ? (char*)"Exist " : (char*)"NotExist ");
    kputsSecure(entry->flags.is_atom ? (char*)"Atom " : (char*)"NonAtom ");
    kputsSecure(entry->flags.is_dirty ? (char*)"Dirty " : (char*)"Clean ");
    kputsSecure(entry->flags.is_lowerlv_bitmap ? (char*)"Bitmap " : (char*)"Table ");
    kputsSecure(entry->flags.is_reserved ? (char*)"Reserved " : (char*)"NonReserved ");
    
    // 打印权限标志
    kputsSecure(entry->flags.is_readable ? (char*)"R" : (char*)"-");
    kputsSecure(entry->flags.is_writable ? (char*)"W" : (char*)"-");
    kputsSecure(entry->flags.is_executable ? (char*)"X" : (char*)"-");
    kputsSecure((char*)" ");
    
    // 打印其他标志
    kputsSecure(entry->flags.is_kernel ? (char*)"Kernel " : (char*)"User ");
    kputsSecure(entry->flags.is_locked ? (char*)"Locked " : (char*)"Unlocked ");
    kputsSecure(entry->flags.is_shared ? (char*)"Shared " : (char*)"Private ");
    kputsSecure(entry->flags.is_occupied ? (char*)"Occupied " : (char*)"Free ");
}