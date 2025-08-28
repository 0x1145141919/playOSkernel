#include "Memory.h"
#include "VideoDriver.h"
#include "errno.h"
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

int BitmapFreeMemmgr_t::SetBitmapentryiesmulty(uint8_t lv, uint64_t start_index, uint64_t numofEntries, uint8_t state)
{
    if(AllLvsBitmapCtrl[lv].flags.type!=BMLV_STATU_STATIC)
    {
        kputsSecure("SetBitmapentryiesmulty:bitmap not static");
        return EINVAL;
    }
    uint8_t* bitmapbase=AllLvsBitmapCtrl[lv].base.bitmap;
    if (lv)
    {
        /* code */
    }else{
        if(state>2)
        {
            kputsSecure("SetBitmapentryiesmulty:in 0 lv state must be 0 or 1")  ;
            return EINVAL;
        }
        uint8_t byte_entry_content=0-state;
        uint64_t Qword_entry_content=0-state;
        for (uint64_t i = start_index; i < start_index+numofEntries; )
        {
            
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
    phy_memDesriptor*gphymem=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    uint64_t PhyMemdescriptorentrycount=gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    if(lv)
    {
        uint64_t bitmapsEntryCount=AllLvsBitmapCtrl[lv].entryCount;
        uint64_t BitsEntrymanagedSize=AllLvsBitmapCtrl[lv].managedSize;
        uint64_t descriptor_index=0;
        uint64_t bitmap_index=0;
      for (; descriptor_index < PhyMemdescriptorentrycount;descriptor_index++ )
      {
        // 跳过空闲系统内存类型
        if(gphymem[descriptor_index].Type==freeSystemRam)continue;
        uint64_t phystart=gphymem[descriptor_index].PhysicalStart;
        uint64_t segment_size_inbyte=gphymem[descriptor_index].NumberOfPages*PAGE_SIZE;
        uint64_t phyend=phystart+segment_size_inbyte-1;
        uint64_t startBitmapIndex=phystart/BitsEntrymanagedSize;
        uint64_t endBitmapIndex=phyend/BitsEntrymanagedSize;
        
        // 判断起始和结束位置是否对齐到管理的内存块边界
        uint8_t isbeginfull,isendfull;
        if (phystart%BitsEntrymanagedSize==0)
        {
            // 起始地址对齐，且跨越了多个内存块
            if (startBitmapIndex<endBitmapIndex)
            {
                isbeginfull=1;
                        }else               isbeginfull=0;
        }else    isbeginfull=0;
        if((phyend+1)%BitsEntrymanagedSize==0)
        {
            // 结束地址对齐，且跨越了多个内存块
            if (endBitmapIndex>startBitmapIndex)
            {
                isendfull=1;
            }else               isendfull=0;
        }else    isendfull=0;
        
        // 根据内存类型判断该内存段是否可以被回收（重新分配）
        uint8_t entryrecycable=0;
        switch(gphymem[descriptor_index].Type)
        {
            case EFI_LOADER_CODE:
            case EFI_LOADER_DATA:
         case OS_KERNEL_DATA:
          case OS_KERNEL_CODE:
           case OS_KERNEL_STACK:entryrecycable=1;break;
            case EFI_RESERVED_MEMORY_TYPE: 
            case EFI_RUNTIME_SERVICES_CODE:
            case EFI_RUNTIME_SERVICES_DATA:
            case EFI_MEMORY_MAPPED_IO:
            case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
            case EFI_PAL_CODE:
            case EFI_ACPI_MEMORY_NVS:
            case EFI_ACPI_RECLAIM_MEMORY:entryrecycable=0;break;
            default:kputsSecure("[ERROR] BitmapFreeMemmgr_t::pageRecycle: Invalid memory type\n");
            return ;
        }    
       uint8_t*bitmapbase=AllLvsBitmapCtrl[lv].base.bitmap;
       uint8_t*masks=(entryrecycable?PartialUsed:PartialReserved);
       
       // 处理起始内存块
       if(isbeginfull){
        // 起始地址对齐，整个内存块都被占用
        bitmapbase[startBitmapIndex>>2]|=FullUsed_and_Filtermasks[startBitmapIndex&3];
       }else{
        // 起始地址未对齐，只部分占用
        uint8_t bits_entry_value_start=bitmapbase[startBitmapIndex>>2]&FullUsed_and_Filtermasks[startBitmapIndex&3];
        if (bits_entry_value_start==BM_RESERVED_PARTIAL)
        {
            // 如果已经是保留状态，则跳到中间处理
            goto middle_entry;
        }else{
            // 设置部分使用或部分保留状态
            bitmapbase[startBitmapIndex>>2]|=masks[startBitmapIndex&3];
        }
        
       }
       
       // 处理结束内存块
        if(isendfull){
        // 结束地址对齐，整个内存块都被占用
        bitmapbase[endBitmapIndex>>2]|=FullUsed_and_Filtermasks[endBitmapIndex&3];
       }else{
        // 结束地址未对齐，只部分占用
        uint8_t bits_entry_value_end=bitmapbase[endBitmapIndex>>2]&FullUsed_and_Filtermasks[endBitmapIndex&3];
        if (bits_entry_value_end==BM_RESERVED_PARTIAL)
        {
            // 如果已经是保留状态，则跳到中间处理
            goto middle_entry;
        }else{
            // 设置部分使用或部分保留状态
            bitmapbase[endBitmapIndex>>2]|=masks[endBitmapIndex&3];
        }
        
       }
       
       // 处理中间完整的内存块
 middle_entry:       for(uint64_t i = startBitmapIndex+1; i < endBitmapIndex; i++){
        // 中间内存块全部被占用
        bitmapbase[i>>2]|=FullUsed_and_Filtermasks[i&3];
       }
       
    }
    }else{
       for (int i = 0; i < PhyMemdescriptorentrycount; i++)
        {
            if(gphymem[i].Type==freeSystemRam)continue;
            phymementry_to_4kbbitmap(gphymem,i,AllLvsBitmapCtrl[lv].base.bitmap);
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
