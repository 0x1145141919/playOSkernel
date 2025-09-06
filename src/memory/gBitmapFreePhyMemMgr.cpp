#include "Memory.h"
#include "VideoDriver.h"
#include "errno.h"
#include "utils.h"
#define PAGE_SIZE_4KB 0x1000
uint8_t busymasks[8]={128,64,32,16,8,4,2,1};
uint8_t PartialUsed[4]={BM_PARTIAL<<6,BM_PARTIAL<<4,BM_PARTIAL<<2,BM_PARTIAL<<0};
uint8_t PartialReserved[4]={BM_RESERVED_PARTIAL<<6,BM_RESERVED_PARTIAL<<4,BM_RESERVED_PARTIAL<<2,BM_RESERVED_PARTIAL<<0};
uint8_t FullUsed_and_Filtermasks[4]={BM_FULL<<6,BM_FULL<<4,BM_FULL<<2,BM_FULL<<0};
BitmapFreeMemmgr_t gBitmapFreePhyMemPGmgr;
uint8_t BitmapFreeMemmgr_t::GetBitmapEntryState(uint8_t lv, uint64_t index)
{
    PhyMemBitmapCtrloflevel& ctrl = AllLvsBitmapCtrl[lv];
    
    if (lv == 0) {
        // 0级位图使用1bit表示状态
        uint64_t byteIndex = index / 8;
        uint8_t bitIndex = index % 8;
        return (ctrl.base.bitmap[byteIndex] & busymasks[bitIndex]) ? 1 : 0;
    } else {
        // 非0级位图使用2bit表示状态
        uint64_t byteIndex = index / 4;
        uint8_t bitPairIndex = index % 4;
        uint8_t byteValue = ctrl.base.bitmap[byteIndex];
        
        // 提取对应的2bit状态
        return (byteValue >> (6 - 2 * bitPairIndex)) & 0x03;
    }
}
int BitmapFreeMemmgr_t::BitmaplvctrlsInit(uint64_t entryCount[5])
{
  
    for (int i = 0; i < 5; i++)
    { AllLvsBitmapCtrl[i].level=(PageTableLevel)i;
        if (i==mainbitmaplv||i==subbitmaplv)
        { 
            AllLvsBitmapCtrl[i].flags.type=BMLV_STATU_STATIC;
            AllLvsBitmapCtrl[i].entryCount=entryCount[i];
            AllLvsBitmapCtrl[i].managedSize=1ULL<<(i*9+12);
            uint64_t bitmapsize_ing_byte=(entryCount[i]*(i?2:1)+7)/8;
            phyaddr_t bitmapbase=(phyaddr_t)AllLvsBitmapCtrl[i].base.bitmap;
            gBaseMemMgr.defaultPhyaddPgallocate(bitmapbase,bitmapsize_ing_byte,OS_KERNEL_DATA);
            AllLvsBitmapCtrl[i].base.bitmap=(uint8_t*)bitmapbase;
            gBaseMemMgr.pageSetValue((phyaddr_t)AllLvsBitmapCtrl[i].base.bitmap,0);
            
            continue;
        }
        if (i<subbitmaplv)
        {

            AllLvsBitmapCtrl[i].base.subbitmaphashtb=NULL;
            AllLvsBitmapCtrl[i].entryCount=0;
            AllLvsBitmapCtrl[i].flags.type=BMLV_STATU_DYNAMIC;
        }        
    }
    gBaseMemMgr.printPhyMemDesTb();
    StaticBitmapInit(mainbitmaplv);
    StaticBitmapInit(subbitmaplv);
    return OS_SUCCESS;
}






inline int BitmapFreeMemmgr_t::SetBitmapentryiesmulty(uint8_t lv, uint64_t start_index, uint64_t numofEntries, uint8_t state)
{
    if(AllLvsBitmapCtrl[lv].flags.type!=BMLV_STATU_STATIC)
    {
        kputsSecure("SetBitmapentryiesmulty:bitmap not static");
        return EINVAL;
    }
    uint8_t* bitmapbase=AllLvsBitmapCtrl[lv].base.bitmap;//基址由于是使用的页分配器，所以说至少12bit对齐
    if (lv)//非0级位图一个项两个bit
    {
        if(state>3)
        {
            kputsSecure("SetBitmapentryiesmulty:in not 0 entry state only for states: 0,1,2,3")  ;
            return EINVAL;
        }
        uint8_t byte_entry_content=0;
        uint64_t Qword_entry_content=0;
        switch (state)
        {
        case BM_FREE:     
            break;
        case BM_PARTIAL: 
        byte_entry_content=0x55;
        Qword_entry_content=0x5555555555555555;
            break;
        case BM_RESERVED_PARTIAL:
        byte_entry_content=0xAA;
        Qword_entry_content=0xAAAAAAAAAAAAAAAA;
            break;
        case BM_FULL:
        byte_entry_content=0xFF;
        Qword_entry_content=0xFFFFFFFFFFFFFFFF;
            break;
        }
        for (uint64_t i = start_index; i < start_index+numofEntries; )
        {
            if(i&31ULL)
            {
Index_not_alignedin5bits1:
                if (i&3ULL)
                {
Index_not_alignedin2bits1:
                SetBitsinMask<uint8_t>(bitmapbase[i>>2],FullUsed_and_Filtermasks[i&3],state) ;
                i++;
                continue;
            }else{
                if (start_index+numofEntries-i>=4)
                {
                    bitmapbase[i>>2]=byte_entry_content;
                    i+=4;
                    continue;
                }else{
                    goto Index_not_alignedin2bits1;
                }
                
            }
                
            } else{
                if (start_index+numofEntries-i>=32)
                {
                    uint64_t*bitmapbase_of32bit=(uint64_t*)bitmapbase;
                    bitmapbase_of32bit[i>>5]=Qword_entry_content;
                    i+=32;
                    continue;
                }else{
                    goto Index_not_alignedin5bits1;
                }
            }
        }
    }else{//0级位图一个项一个bit
        if(state>1)
        {
            kputsSecure("SetBitmapentryiesmulty:in 0 entry state must be 0 or 1")  ;
            return EINVAL;
        }
        uint8_t byte_entry_content=state?0xff:0;
        uint64_t Qword_entry_content=state?0xffffffffffffffff:0;    
        for (uint64_t i = start_index; i < start_index+numofEntries; )
        {
            if(i&63ULL)
            {
not_alignedin6bits3:
                if(i&7ULL)
                {
not_alignedin3bits5:
                    SetBitsinMask<uint8_t>(bitmapbase[i>>3],busymasks[i&7],state) ;
                    i++;
                    continue;
                }else{
                    if (start_index+numofEntries-i>=8)
                    {
                        bitmapbase[i>>3]=byte_entry_content;
                        i+=8;
                        continue;
                    }else{
                        goto not_alignedin3bits5;
                    }
                    
                }
            }else{
                if (start_index+numofEntries-i>=64)
                {
                    uint64_t*bitmapbase_of64bit=(uint64_t*)bitmapbase;
                    bitmapbase_of64bit[i>>6]=Qword_entry_content;
                    i+=64;
                    continue;
                }else{
                    goto not_alignedin6bits3;
                }

            }
        }
    } 
    return OS_SUCCESS;
}
/**
 * @brief 固定地址分配连续页的分配器
 * 
 * @param addr 要分配的页的起始物理地址
 * @param numofpgs_in4kb 要分配的页数（4kb页）
 * @param Op 操作类型 OPERATION_ALLOCATE 或 OPERATION_RECYCLE
 * @return int 错误码
 * 
 * 内部接口只管改不管验证，但是要对主副级别的位图都进行修改：
 * 那么就先改副图（地址肯定要根据副图的页大小来对齐），再根据副图的变化改主图
 */
int BitmapFreeMemmgr_t::InnerFixedaddrPgManage(IN phyaddr_t addr, IN uint64_t numofpgs_in4kb,OPERATION Op)
{   
    int Status = OS_SUCCESS;
    uint64_t entryIndex_sublv_start=addr/AllLvsBitmapCtrl[subbitmaplv].managedSize;
    uint64_t entryIndex_sublv_end=(addr+(numofpgs_in4kb<<12)-1)/AllLvsBitmapCtrl[subbitmaplv].managedSize;
    switch(statuflags.state)
    {
        case BM_STATU_STATIC_ONLY://静态模式下只有两个级别的位图，如果副级别非0不会对分配未满进行标记，因为没有相关数据结构保存相关信息
        SetBitmapentryiesmulty(subbitmaplv,
            entryIndex_sublv_start,
            entryIndex_sublv_end-entryIndex_sublv_start+1,
            Op==OPERATION_ALLOCATE?
            subbitmaplv?BM_FULL:BM_USED
            :BM_FREE);
        break;
        case BM_STATU_DYNAMIC://后面有了内核池分配器后再考虑动态分配
        { 
        }
        case BM_STATU_UNINITIALIZED:
        return EINTR;
    }
    
    // 根据副图同步到主图
    // 先根据副图的起始，终止索引算出主图的起始，终止索引
    // 然后以主图项遍历为主，逐项遍历对应的副图项（副图级别会因为是否为0遍历方式有所区别，但都是逐64bit遍历）同步
    // 还要考虑到可能主级别图最后一项无法完整对应512项，这时主图项为BM_RESERVED_PARTIAL，不过在初始化函数中没有看到相关的保证
    // 主图项为BM_RESERVED_PARTIAL跳过这个主图项
    uint64_t entryIndex_mainlv_start=entryIndex_sublv_start>>9;
    uint64_t entryIndex_mainlv_end=(addr+numofpgs_in4kb*PAGE_SIZE_4KB-1)/AllLvsBitmapCtrl[mainbitmaplv].managedSize;
    uint64_t* subbitmap=(uint64_t*)AllLvsBitmapCtrl[subbitmaplv].base.bitmap;
    
    // 遍历主图项并同步副图状态
    for(uint64_t i=entryIndex_mainlv_start;i<=entryIndex_mainlv_end;i++){
        // 跳过保留的部分项
        if(GetBitmapEntryState(mainbitmaplv,i)==BM_RESERVED_PARTIAL)continue;
        
        uint64_t filter_content_or=0;
        uint64_t filter_content_and=0xFFFFFFFFFFFFFFFF;
        uint64_t start_sublv_entry_index_to_trivial=i<<9;
        uint64_t entryIndextoTrivial_start_in_64bits=0;
        
        if(subbitmaplv){
            entryIndextoTrivial_start_in_64bits=start_sublv_entry_index_to_trivial>>5;
            bool has_reserved = false;
            const uint64_t high_mask = 0xAAAAAAAAAAAAAAAAULL;
            const uint64_t low_mask = 0x5555555555555555ULL;
            // 对于非0级位图，处理16个64位块
            for(int j = 0; j < 16; j++) {  // 添加num_qwords限界
        uint64_t sub = subbitmap[entryIndextoTrivial_start_in_64bits + j];
        filter_content_or |= sub;
        filter_content_and &= sub;
        has_reserved |= ((sub & high_mask) & ~(sub & low_mask)) != 0;
        if (has_reserved) break;  // 优化early exit
    }
    if (has_reserved) {
        SetBitmapentryiesmulty(mainbitmaplv, i, 1, BM_RESERVED_PARTIAL);
        continue;
    }
        }else{
            entryIndextoTrivial_start_in_64bits=start_sublv_entry_index_to_trivial>>6;
            // 对于0级位图，处理8个64位块
            for(int j=0;j<8;j++){
                filter_content_or|=subbitmap[entryIndextoTrivial_start_in_64bits+j];
                filter_content_and&=subbitmap[entryIndextoTrivial_start_in_64bits+j];
            }
        }
        
        // 根据过滤内容设置主图项状态
        if(filter_content_and==~0ULL){
            SetBitmapentryiesmulty(mainbitmaplv,i,1,BM_FULL);
            continue;
        }
        if(filter_content_or==0){
            SetBitmapentryiesmulty(mainbitmaplv,i,1,BM_FREE);
            continue;
        }
        SetBitmapentryiesmulty(mainbitmaplv,i,1,BM_PARTIAL);
    }
    return Status;
}

BitmapFreeMemmgr_t::BitmapFreeMemmgr_t()
{
    CpuPglevel=0;
    mainbitmaplv=0;
    subbitmaplv=0;  
    statuflags.state=BM_STATU_UNINITIALIZED;
    maxphyaddr=0;
}

/**
 * @brief 初始化指定层级的静态位图
 * 
 * 该函数用于初始化内存管理器中指定层级的静态位图。根据层级的不同，处理方式也不同：
 * 1. 对于非0层级（lv>0）：处理较大粒度的内存块（如2MB、1GB等）
 *    - 遍历所有物理内存描述符
 *    - 根据内存类型判断是否可回收
 *    - 根据内存区域在位图中设置相应标记
 *    - 使用不同的标记表示内存块的使用状态（全用、部分用、保留等）
 * 2. 对于0层级（lv==0）：处理4KB页面粒度的内存
 *    - 直接将物理内存描述符转换为4KB粒度的位图表示
 * 
 * 位图中的每个2位表示一个内存块的状态：
 * - BM_FULL(3): 全部使用的内存块
 * - BM_PARTIAL(1): 部分使用的内存块
 * - BM_RESERVED_PARTIAL(2): 保留的部分使用内存块
 * - BM_FREE(0): 空闲的内存块
 * 
 * @param lv 指定要初始化的位图层级（0-4）
 *           0: 4KB页面粒度
 *           1: 2MB块粒度
 *           2: 1GB块粒度
 *           3-4: 更高粒度（根据CPU页表级别）
 */
void BitmapFreeMemmgr_t::PrintBitmapInfo()
{
    // 打印主级别位图信息
    kputsSecure("Main Level Bitmap Info:\n");
    PhyMemBitmapCtrloflevel& mainCtrl = AllLvsBitmapCtrl[mainbitmaplv];
    uint64_t mainManagedSize = mainCtrl.managedSize;
    
    for (uint64_t i = 0; i < mainCtrl.entryCount; i++) {
        uint8_t state = GetBitmapEntryState(mainbitmaplv, i);
        if(state == BM_FREE)continue;
        uint64_t startAddr = i * mainManagedSize;
        
        kputsSecure("Main Entry ");
        kpnumSecure(&i, UNDEC, 4);
        kputsSecure(": StartAddr=0x");
        kpnumSecure(&startAddr, UNHEX, 6);
        kputsSecure(", State=");
        
        switch(state) {
            case BM_FREE:
                kputsSecure("FREE");
                break;
            case BM_PARTIAL:
                kputsSecure("PARTIAL");
                break;
            case BM_RESERVED_PARTIAL:
                kputsSecure("RESERVED_PARTIAL");
                break;
            case BM_FULL:
                kputsSecure("FULL");
                break;
            default:
                kputsSecure("UNKNOWN");
        }
        kputsSecure("\n");
    }
    
    // 打印副级别位图信息
    if (subbitmaplv != mainbitmaplv) {
        kputsSecure("\nSub Level Bitmap Info:\n");
        PhyMemBitmapCtrloflevel& subCtrl = AllLvsBitmapCtrl[subbitmaplv];
        uint64_t subManagedSize = subCtrl.managedSize;
        
        for (uint64_t i = 0; i < subCtrl.entryCount; i++) {
            uint8_t state = GetBitmapEntryState(subbitmaplv, i);
            if(state == BM_FREE)continue;
            uint64_t startAddr = i * subManagedSize;
            
            kputsSecure("Sub Entry ");
            kpnumSecure(&i, UNDEC,4);
            kputsSecure(": StartAddr=0x");
            kpnumSecure(&startAddr, UNHEX, 6);
            kputsSecure(", State=");
            
            if (subbitmaplv == 0) {
                // 0级位图使用1bit表示状态
                //kputsSecure(state ? "USED" : "FREE");
                if (state)
                {
                    kputsSecure("USED");
                }else{
                    kputsSecure("FREE");
                }
                
            } else {
                // 非0级位图使用2bit表示状态
                switch(state) {
                    case BM_FREE:
                        kputsSecure("FREE");
                        break;
                    case BM_PARTIAL:
                        kputsSecure("PARTIAL");
                        break;
                    case BM_RESERVED_PARTIAL:
                        kputsSecure("RESERVED_PARTIAL");
                        break;
                    case BM_FULL:
                        kputsSecure("FULL");
                        break;
                    default:
                        kputsSecure("UNKNOWN");
                }
            }
            kputsSecure("\n");
        }
    } 
}
inline void BitmapFreeMemmgr_t::StaticBitmapInit(uint8_t lv)
{
    phy_memDesriptor* gphymem = gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    uint64_t PhyMemdescriptorentrycount = gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    
    if (lv) // 非0级位图
    {
        uint64_t BitsEntrymanagedSize = AllLvsBitmapCtrl[lv].managedSize;
        
        for (uint64_t descriptor_index = 0; descriptor_index < PhyMemdescriptorentrycount; descriptor_index++)
        {
            // 跳过空闲系统内存类型
            if (gphymem[descriptor_index].Type == freeSystemRam) continue;
            
            uint64_t phystart = gphymem[descriptor_index].PhysicalStart;
            uint64_t segment_size_inbyte = gphymem[descriptor_index].NumberOfPages * PAGE_SIZE_4KB;
            uint64_t phyend = phystart + segment_size_inbyte - 1;
            uint64_t startBitmapIndex = phystart / BitsEntrymanagedSize;
            uint64_t endBitmapIndex = phyend / BitsEntrymanagedSize;
            
            // 根据内存类型判断该内存段是否可以被回收（重新分配）
            uint8_t entryrecycable = 0;
            switch(gphymem[descriptor_index].Type)
            {
                case EFI_LOADER_CODE:
                case EFI_LOADER_DATA:
                case OS_KERNEL_DATA:
                case OS_KERNEL_CODE:
                case OS_KERNEL_STACK:
                    entryrecycable = 1;
                    break;
                case EFI_RESERVED_MEMORY_TYPE:
                case EFI_RUNTIME_SERVICES_CODE:
                case EFI_RUNTIME_SERVICES_DATA:
                case EFI_MEMORY_MAPPED_IO:
                case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
                case EFI_PAL_CODE:
                case EFI_ACPI_MEMORY_NVS:
                case EFI_ACPI_RECLAIM_MEMORY:
                    entryrecycable = 0;
                    break;
                default:
                    kputsSecure("[ERROR] BitmapFreeMemmgr_t::StaticBitmapInit: Invalid memory type\n");
                    continue;
            }
            
            // 处理起始块（可能部分占用）
            uint8_t start_state = GetBitmapEntryState(lv, startBitmapIndex);
            if (start_state != BM_RESERVED_PARTIAL) {
                bool start_full = (phystart % BitsEntrymanagedSize == 0&&endBitmapIndex>startBitmapIndex);
                uint8_t state_to_set = start_full ? 
                    (entryrecycable ? BM_FULL : BM_RESERVED_PARTIAL) :
                    (entryrecycable ? BM_PARTIAL : BM_RESERVED_PARTIAL);
                
                SetBitmapentryiesmulty(lv, startBitmapIndex, 1, state_to_set);
            }
            
            // 处理结束块（可能部分占用）
            if (endBitmapIndex > startBitmapIndex) {
                uint8_t end_state = GetBitmapEntryState(lv, endBitmapIndex);
                if (end_state != BM_RESERVED_PARTIAL) {
                    bool end_full = ((phyend + 1) % BitsEntrymanagedSize == 0);
                    uint8_t state_to_set = end_full ? 
                        (entryrecycable ? BM_FULL : BM_RESERVED_PARTIAL) :
                        (entryrecycable ? BM_PARTIAL : BM_RESERVED_PARTIAL);
                    
                    SetBitmapentryiesmulty(lv, endBitmapIndex, 1, state_to_set);
                }
            }
            
            // 处理中间完全占用的块（批量设置）
            if (endBitmapIndex > startBitmapIndex + 1) {
                uint64_t middle_start = startBitmapIndex + 1;
                uint64_t middle_end = endBitmapIndex - 1;
                uint64_t middle_count = middle_end - middle_start + 1;
                
                // 检查中间块是否已经有保留状态
                bool has_reserved = false;
                for (uint64_t i = middle_start; i <= middle_end; i++) {
                    if (GetBitmapEntryState(lv, i) == BM_RESERVED_PARTIAL) {
                        has_reserved = true;
                        break;
                    }
                }
                
                if (!has_reserved) {
                    // 批量设置中间块为全占用状
                    SetBitmapentryiesmulty(lv, middle_start, middle_count, BM_FULL);
                } else {
                    // 有保留状态，需要逐个设置
                    for (uint64_t i = middle_start; i <= middle_end; i++) {
                        uint8_t current_state = GetBitmapEntryState(lv, i);
                        if (current_state != BM_RESERVED_PARTIAL) {
                            uint8_t state_to_set = BM_FULL;
                            SetBitmapentryiesmulty(lv, i, 1, state_to_set);
                        }
                    }
                }
            }
        }
    }
    else // 0级位图
    {
        for (int i = 0; i < PhyMemdescriptorentrycount; i++)
        {
            if (gphymem[i].Type == freeSystemRam) continue;
            
            // 计算内存段在位图中的起始索引和页数
            uint64_t entryStartIndex = gphymem[i].PhysicalStart >> 12;
            uint64_t entryCount = gphymem[i].NumberOfPages;
            
            // 根据内存类型设置位图状态
            uint8_t state = (gphymem[i].Type == EfiConventionalMemory) ? 0 : 1;
            
            // 使用 SetBitmapentryiesmulty 设置连续位图项
            SetBitmapentryiesmulty(lv, entryStartIndex, entryCount, state);
        }
    }
}

void BitmapFreeMemmgr_t::Init()
{
 // 通过内联汇编获取当前CPU的分页等级
    uint64_t cr4_value;
    asm volatile (
        "mov %%cr4, %0"
        : "=r" (cr4_value)
    );
    // 检查CR4寄存器的LA57位(bit12)判断分页等级
    CpuPglevel = (cr4_value & (1ULL << 12)) ? 5 : 4; 
        // 获取物理内存描述符表信息
    phy_memDesriptor* phymemtb = gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    uint64_t phymemtbentryCount = gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    
    maxphyaddr = 0;
    // 遍历物理内存描述符表计算最大物理地址
    for(uint64_t i = 0; i < phymemtbentryCount; i++)
    { 
        if (phymemtb[i].Type==PHY_MEM_TYPE::EFI_RESERVED_MEMORY_TYPE)
        {
            continue;
        }
        
        // 计算当前内存区域的结束地址
        uint64_t regionEnd = phymemtb[i].PhysicalStart + 
                            (phymemtb[i].NumberOfPages * PAGE_SIZE_4KB);
        
        // 更新最大物理地址
        if (regionEnd > maxphyaddr) {
            maxphyaddr = regionEnd;
        }
    }
    if(maxphyaddr < 1ULL<<30 )
    {
        kputsSecure("Error: Maximum physical address is less than 1GB,upgrade your memory\n");
        return;
    }
    const uint16_t Idealmainlvenctry = 4096;
    uint64_t entryCount[5] = {0,0,0,0,0};
    
    // 修正层级项数计算逻辑
    entryCount[0] = (maxphyaddr + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
    for (int i = 1; i < 5; i++) {
        entryCount[i] = (entryCount[i-1] + 511) >> 9;  // 向上取整除以512
    }
    for (size_t i = 0; i < 4; i++)
    {
        if (entryCount[i]>=Idealmainlvenctry&&entryCount[i+1]<=Idealmainlvenctry)
        {
            /*32gb下1g主粒度32个，2mb主粒度16k个
            512gb下1g主粒度2^9个，2mb主粒度2^18个
            */
           uint64_t smallerResolutionratial1=entryCount[i]/Idealmainlvenctry;
           uint64_t BiggerResolutionratial2=Idealmainlvenctry/entryCount[i+1];//这里取名有问题，resulotion这里不是分辨率，而是单位大小
           mainbitmaplv=smallerResolutionratial1<BiggerResolutionratial2?i:i+1;
            subbitmaplv=mainbitmaplv-1;
            break;
        }
    }

    //接着就是为主级别与副级别构建静态数据结构的位图了,使用各自的私有成员函数
    BitmaplvctrlsInit(entryCount);
    statuflags.state=BM_STATU_STATIC_ONLY;
}
int BitmapFreeMemmgr_t::PgsAlloc(IN ALLOCATE_TYPE type, IN OUT phyaddr_t paddr, IN uint64_t sizein_bytes)
{//未完全完成
    // 检查管理器状态
    if (statuflags.state == BM_STATU_UNINITIALIZED) {
        kputsSecure("PgsAlloc: Bitmap memory manager not initialized\n");
        return EINTR;
    }
    
    // 检查参数有效性
    if (sizein_bytes == 0) {
        kputsSecure("PgsAlloc: size cannot be zero\n");
        return EINVAL;
    }
    
    // 计算需要分配的4KB页数（向上取整）
    uint64_t numofpgs_in4kb = (sizein_bytes + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
    
    if (type == ALLOCATE_TYPE_FIXED_ADDR) {
        // 固定地址分配：检查地址对齐
        if (paddr % PAGE_SIZE_4KB != 0) {
            kputsSecure("PgsAlloc: fixed address must be 4KB aligned\n");
            return EINVAL;
        }
        
        // 检查地址范围是否在有效物理内存范围内
        phyaddr_t endAddr = paddr + numofpgs_in4kb * PAGE_SIZE_4KB - 1;
        if (endAddr > maxphyaddr) {
            kputsSecure("PgsAlloc: address range exceeds maximum physical address\n");
            return EINVAL;
        }
        
        // 获取全局物理内存描述符表
        phy_memDesriptor* memDescTable = gBaseMemMgr.getGlobalPhysicalMemoryInfo();
        uint64_t entryCount = gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
        
        // 查找起始地址所在的描述符项
        int startIndex = -1;
        for (uint64_t i = 0; i < entryCount; i++) {
            phyaddr_t descStart = memDescTable[i].PhysicalStart;
            phyaddr_t descEnd = descStart + memDescTable[i].NumberOfPages * PAGE_SIZE_4KB - 1;
            
            if (paddr >= descStart && paddr <= descEnd) {
                startIndex = i;
                break;
            }
        }
        
        if (startIndex == -1) {
            kputsSecure("PgsAlloc: start address not found in memory descriptor table\n");
            return EINVAL;
        }
        
        // 查找结束地址所在的描述符项
        int endIndex = -1;
        for (uint64_t i = startIndex; i < entryCount; i++) {
            phyaddr_t descStart = memDescTable[i].PhysicalStart;
            phyaddr_t descEnd = descStart + memDescTable[i].NumberOfPages * PAGE_SIZE_4KB - 1;
            
            if (endAddr >= descStart && endAddr <= descEnd) {
                endIndex = i;
                break;
            }
            
            // 如果当前描述符的起始地址已经超过结束地址，提前退出
            if (descStart > endAddr) {
                break;
            }
        }
        
        if (endIndex == -1) {
            kputsSecure("PgsAlloc: end address not found in memory descriptor table\n");
            return EINVAL;
        }
        
        // 检查从startIndex到endIndex的所有描述符项是否都是空闲系统RAM
        for (int i = startIndex; i <= endIndex; i++) {
            if (memDescTable[i].Type != freeSystemRam) {
                kputsSecure("PgsAlloc: address range includes non-free memory\n");
                return EINVAL;
            }
            
            // 对于起始项，检查paddr是否在该项范围内
            if (i == startIndex) {
                phyaddr_t descStart = memDescTable[i].PhysicalStart;
                if (paddr < descStart) {
                    kputsSecure("PgsAlloc: start address not aligned with memory descriptor\n");
                    return EINVAL;
                }
            }
            
            // 对于结束项，检查endAddr是否在该项范围内
            if (i == endIndex) {
                phyaddr_t descEnd = memDescTable[i].PhysicalStart + 
                                   memDescTable[i].NumberOfPages * PAGE_SIZE_4KB - 1;
                if (endAddr > descEnd) {
                    kputsSecure("PgsAlloc: end address not aligned with memory descriptor\n");
                    return EINVAL;
                }
            }
        }
        
        // TODO: 检查位图状态，确保该范围未被分配
        
        // 调用内部函数进行分配
        return InnerFixedaddrPgManage(paddr, numofpgs_in4kb, OPERATION_ALLOCATE);
    } else if (type == ALLOCATE_TYPE_DEFAULT) {
        // 默认分配：需要实现查找合适地址的算法
        // 这里先返回未实现错误，后续完善
        kputsSecure("PgsAlloc: default allocation not implemented yet\n");
        return ENOSYS;
    } else {
        kputsSecure("PgsAlloc: invalid allocation type\n");
        return EINVAL;
    }
}