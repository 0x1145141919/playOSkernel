#include "Memory.h"
#include "VideoDriver.h"
#include "errno.h"
#include "utils.h"
#define PAGE_SIZE 0x1000
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
    StaticBitmapInit(mainbitmaplv);
    StaticBitmapInit(subbitmaplv);
    return OS_SUCCESS;
}



/**
 * @brief 将物理内存描述符条目转换为4KB粒度的位图表示
 * 
 * 该函数将UEFI提供的物理内存描述符中的一个条目转换为4KB页面粒度的位图表示。
 * 位图中每个bit表示一个4KB页面的状态：
 * - 1: 页面已被使用或保留
 * - 0: 页面空闲可用
 * 
 * 根据内存段的大小采用不同的优化处理策略：
 * 1. 小于15页：逐位设置，不进行优化
 * 2. 小于127页：按字节对齐优化，尽可能使用0xFF或0x00直接赋值
 * 3. 大于等于127页：按64位对齐优化，尽可能使用0xFFFFFFFFFFFFFFFF或0x0直接赋值
 * 
 * 这种优化可以显著提高大内存段位图初始化的性能。
 * 
 * @param gphymemtbbase 指向全局物理内存描述符表的指针
 * @param index 当前处理的描述符表项索引
 * @param bitmapbase 指向目标位图的指针
 */
inline void BitmapFreeMemmgr_t::phymementry_to_4kbbitmap(phy_memDesriptor *gphymemtbbase, uint16_t index, uint8_t *bitmapbase)
{  uint64_t entryStartIndex=gphymemtbbase[index].PhysicalStart>>12;
    uint64_t entryCount=gphymemtbbase[index].NumberOfPages;
    uint64_t entryEndIndex=entryStartIndex+entryCount-1;
    bool ispgbusy=gphymemtbbase[index].Type==EfiConventionalMemory?FALSE:TRUE;
    if(entryCount<15)//>=15页保证会有一个字节被占全
    {
        if (ispgbusy)
        {
            for (uint64_t i = entryStartIndex; i <= entryEndIndex; i++)
        {
           bitmapbase[i>>3]|=busymasks[i&7];
        }
        }else
        {
            for (uint64_t i = entryStartIndex; i <= entryEndIndex; i++)
        {
           bitmapbase[i>>3]&=~busymasks[i&7];
        }
        }
        
        return;
    }
    if(entryCount<127)
    {
        if (ispgbusy)
        {
            for (uint64_t i = entryStartIndex; i <= entryEndIndex; )
        {
           if (i&7ULL)
           {
 not_3bit_agline1      :     
            bitmapbase[i>>3]|=busymasks[i&7];
            i++;
            continue;
           }else
           {
            if(entryEndIndex-i>=7){
            bitmapbase[i>>3]=0xFF;
            i+=8;
            continue;
        }else{
            goto not_3bit_agline1;
        }
           }
           
        }
        }else{
             for (uint64_t i = entryStartIndex; i <= entryEndIndex; )
        {
           if (i&7ULL)
           {
 not_3bit_agline2      :     
            bitmapbase[i>>3]&=~busymasks[i&7];
            i++;
            continue;
           }else
           {
            if(entryEndIndex-i>=7){
            bitmapbase[i>>3]=0;
            i+=8;
            continue;
        }else{
            goto not_3bit_agline2;
        }
           }
           
        }
        }
        
    }else{
      if(ispgbusy)
            { 
            for (uint64_t i = entryStartIndex; i <= entryEndIndex; )
        {
            if(i&63ULL)
            {
not_alignedin6bits1:               
             if(i&7ULL)
                {
not_alignedin3bits3:
                    bitmapbase[i>>3]|=busymasks[i&7];
                    i++;
                    continue;
                }else{

                if (entryEndIndex-i>=7)
                {
                    bitmapbase[i>>3]=0xff;
                    i+=8;
                    continue;
                }else{
                    goto not_alignedin3bits3;
                }
                }
            }else{
                if (entryEndIndex-i>=63)
                {
                    uint64_t*bitmapbase_of64bit=(uint64_t*)bitmapbase;
                    bitmapbase_of64bit[i>>6]=0xffffffffffffffff;
                    i+=64;
                    continue;
                }else{
                    goto not_alignedin6bits1;
                } 
            }                
        }
        }else{ 
        for (uint64_t i = entryStartIndex; i <= entryEndIndex; )
        {
            if(i&63ULL)
            {
not_alignedin6bits2:               
             if(i&7ULL)
                {
not_alignedin3bits4:
                    bitmapbase[i>>3]&=~busymasks[i&7];
                    i++;
                    continue;
                }else{

                    if (entryEndIndex-i>=7)
                {
                    bitmapbase[i>>3]=0;
                    i+=8;
                    continue;
                }else{
                    goto not_alignedin3bits4;
                }
                }
            }else{
                if (entryEndIndex-i>=63)
                {
                    uint64_t*bitmapbase_of64bit=(uint64_t*)bitmapbase;
                    bitmapbase_of64bit[i>>6]=0;
                    i+=64;
                    continue;
                }else{
                    goto not_alignedin6bits2;
                } 
            }                
        }
        }
    }
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
            uint64_t segment_size_inbyte = gphymem[descriptor_index].NumberOfPages * PAGE_SIZE;
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
                bool start_full = (phystart % BitsEntrymanagedSize == 0);
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
                    // 批量设置中间块为全占用状态
                    uint8_t state_to_set = entryrecycable ? BM_FULL : BM_RESERVED_PARTIAL;
                    SetBitmapentryiesmulty(lv, middle_start, middle_count, state_to_set);
                } else {
                    // 有保留状态，需要逐个设置
                    for (uint64_t i = middle_start; i <= middle_end; i++) {
                        uint8_t current_state = GetBitmapEntryState(lv, i);
                        if (current_state != BM_RESERVED_PARTIAL) {
                            uint8_t state_to_set = entryrecycable ? BM_FULL : BM_RESERVED_PARTIAL;
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
        if (i==phymemtbentryCount-1&&phymemtb[i].Type==PHY_MEM_TYPE::EFI_RESERVED_MEMORY_TYPE)
        {
            break;
        }
        
        // 计算当前内存区域的结束地址
        uint64_t regionEnd = phymemtb[i].PhysicalStart + 
                            (phymemtb[i].NumberOfPages * PAGE_SIZE);
        
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
    entryCount[0] = (maxphyaddr + PAGE_SIZE - 1) / PAGE_SIZE;
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
