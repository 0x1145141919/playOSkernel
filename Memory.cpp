#include "Memory.h"
#include "VideoDriver.h"
#include "errno.h"
#define KEFLBASE 0x4000000
#define DIRTY_ENTRY 1
#define CLEAN_ENTRY 0
#define PAGE_SIZE 0x1000
//efi内存描述符表id为0,操作系统自己维护的内存描述符表id为1
// 默认构造函数
GlobalMemoryPGlevelMgr_t::GlobalMemoryPGlevelMgr_t() {
    // 初始化成员变量
    rootPhyMemDscptTbBsPtr = nullptr;
    EfiMemMapEntryCount = 0;

}


void ksystemramcpy(void*src,void*dest,size_t length)
//最好用于内核内存空间内的内存拷贝，不然会出现未定义行为
{  uint64_t remainder=length&0x7;
    uint64_t count=length>>3;
    //先范围重复判断
    if(uint64_t(src)>uint64_t(dest)){
    low_to_high:
    for(uint64_t i=0;i<count;i++)
    {
        ((uint64_t*)dest)[i]=((uint64_t*)src)[i];
    }
    for(uint64_t i=0;i<remainder;i++)
    {
        ((uint8_t*)dest)[length-remainder+i]=((uint8_t*)src)[length-remainder+i];
    }
    return ;
}else//源地址低目标地址高的时候就需要内存由高到低复制
//大多数情况源地址目标地址都对齐的情况下，先复制余数项（一次一字节）再续复制非余数项（一次八字节）
{
if((uint64_t(src)+length>uint64_t(dest)))
{
    for(uint64_t i=0;i<remainder;i++)
    {
        ((uint8_t*)dest)[length-i-1]=((uint8_t*)src)[length-i-1];
    }
    for (int i = count-1; i >= 0; i--)
    {
        ((uint64_t*)dest)[i]=((uint64_t*)src)[i];
    }
    
}else goto low_to_high;
}
}
/**
 * 此函数只会对物理内存描述符表中的物理内存起始地址按照低到高排序
 * 一般来说这个表会是分段有序的，采取插入排序
 * 不过前面会有一大段有序的表，所以直接遍历到第一个有序子表结束后再继续遍历
 */

// 带参数的构造函数（示例）
void linearTBSerialDelete(//这是一个对于线性表删除一段连续项,起始索引a,结束索引b的函数
    uint64_t*TotalEntryCount,
    uint64_t a,
    uint64_t b,
    void*linerTbBase,
    uint32_t entrysize
)
{ 
    char*bs=(char*)linerTbBase;
    char*srcadd=bs+entrysize*(b+1);
    char*destadd=bs+entrysize*a;
    uint64_t deletedEntryCount=b-a+1;
    ksystemramcpy((void*)srcadd,(void*)destadd,entrysize*(*TotalEntryCount-b-1));
    *TotalEntryCount-=deletedEntryCount;
}
/**
 * 在描述符表中插入一个或多个连续条目
 * 
 * @param TotalEntryCount 表项总数的指针（插入后会更新）
 * @param insertIndex     插入位置的起始索引（0-based）
 * @param newEntry        要插入的新条目（单个或多个连续条目）
 * @param linerTbBase     描述符表基地址
 * @param entrysize       单个条目的大小
 * @param entryCount      要插入的条目数量（默认为1）
 */
void linearTBSerialInsert(
    uint64_t* TotalEntryCount,
    uint64_t insertIndex,
    void* newEntry,
    void* linerTbBase,
    uint32_t entrysize,
    uint64_t entryCount = 1
) {
    if (insertIndex > *TotalEntryCount) {
        // 插入位置超出当前表范围，直接追加到末尾
        insertIndex = *TotalEntryCount;
    }
    
    char* base = (char*)linerTbBase;
    char* src = (char*)newEntry;
    
    // 计算需要移动的数据量（从插入点到表尾）
    uint64_t moveCount = *TotalEntryCount - insertIndex;
    uint64_t moveSize = moveCount * entrysize;
    
    if (moveSize > 0) {
        // 向后移动现有条目（使用内存安全拷贝）
        char* srcStart = base + insertIndex * entrysize;
        char* destStart = srcStart + entryCount * entrysize;
        ksystemramcpy(srcStart, destStart, moveSize);
    }
    
    // 插入新条目
    for (uint64_t i = 0; i < entryCount; i++) {
        char* dest = base + (insertIndex + i) * entrysize;
        ksystemramcpy(src + i * entrysize, dest, entrysize);
    }
    
    // 更新表项总数
    *TotalEntryCount += entryCount;
}
GlobalMemoryPGlevelMgr_t::GlobalMemoryPGlevelMgr_t(EFI_MEMORY_DESCRIPTORX64* gEfiMemdescriptromap, uint64_t entryCount) {
    EfiMemMap = gEfiMemdescriptromap;
    EfiMemMapEntryCount = entryCount;
    // KSCR3 需要通过其他方式初始化
}
void GlobalMemoryPGlevelMgr_t::Init(EFI_MEMORY_DESCRIPTORX64* gEfiMemdescriptromap, uint64_t entryCount)

{
    EfiMemMap = gEfiMemdescriptromap;
    EfiMemMapEntryCount = entryCount;
    for (int i = entryCount-1; i >=0; i--)
    {
       if (gEfiMemdescriptromap[i].NumberOfPages==0&&gEfiMemdescriptromap[i].Type==EfiReservedMemoryType)
       {
        EfiMemMapEntryCount--;
       }else{
        break;
       }
    }//删除那些留有冗余的表

    reclaimBootTimeMemoryonEfiTb();
    InitrootPhyMemDscptTbBsPtr();
    //再正式回收loadercode,loaderdata类型的内存（只对PhyMemDscptTb这个表回收），参照reclaimBootTimeMemoryonEfiTb()这个函数，注意回收不标记为Runtime参数的项
    for (int i = rootPhymemTbentryCount-1; i >=0; i--)
    {
       if (rootPhyMemDscptTbBsPtr[i].Type==EfiReservedMemoryType)
       {
        rootPhymemTbentryCount--;
        
       }else{
        break;
       }
    }//删除那些留有冗余的表
    Statusflags=1;
}
phy_memDesriptor *GlobalMemoryPGlevelMgr_t::getGlobalPhysicalMemoryInfo()
{
    return rootPhyMemDscptTbBsPtr;
}
uint64_t GlobalMemoryPGlevelMgr_t::getRootPhysicalMemoryDescriptorTableEntryCount()
{
    return rootPhymemTbentryCount;
}
phy_memDesriptor *GlobalMemoryPGlevelMgr_t::queryPhysicalMemoryUsage(uint64_t addr)
{
    for(int i = 0; i < rootPhymemTbentryCount; i++)
    {
        if (rootPhyMemDscptTbBsPtr[i].PhysicalStart<=addr&&addr<
            rootPhyMemDscptTbBsPtr[i].PhysicalStart+rootPhyMemDscptTbBsPtr[i].NumberOfPages*PAGE_SIZE_4KB)
        {
            return rootPhyMemDscptTbBsPtr+i;
        }
        
    }
    return NULL;
}
void GlobalMemoryPGlevelMgr_t::dirtyentrydelete(uint64_t tbid)
{//双指针法从尾部删除脏项
     switch (tbid)
    {
    case 0 :
        int fast_index,slow_index;
        fast_index=slow_index=EfiMemMapEntryCount-1;    
        while (true)    
        {
            if(fast_index==0&&slow_index==0)break;
            bool fastindex_stop=EfiMemMap[fast_index].ReservedUnion.TmpChainList.Flags==DIRTY_ENTRY\
            &&EfiMemMap[fast_index-1].ReservedUnion.TmpChainList.Flags==CLEAN_ENTRY;
            bool slowindex_stop=EfiMemMap[slow_index].ReservedUnion.TmpChainList.Flags==CLEAN_ENTRY\
            &&EfiMemMap[slow_index-1].ReservedUnion.TmpChainList.Flags==DIRTY_ENTRY;
            if (fastindex_stop&&slowindex_stop)
            {
                linearTBSerialDelete(&EfiMemMapEntryCount,fast_index,slow_index-1,(void*)EfiMemMap,sizeof(EFI_MEMORY_DESCRIPTORX64));
                slow_index=fast_index;
            }else
            {
                if (fastindex_stop==false&&fast_index>0)fast_index--;
                if(slowindex_stop==false&&slow_index>0)slow_index--;
            }
            }
            break;
        
        
    case 1 :
        fast_index=slow_index=rootPhymemTbentryCount-1;    
        while (true)    
        {
            if(fast_index==0&&slow_index==0)break;
            bool fastindex_stop=rootPhyMemDscptTbBsPtr[fast_index].ReservedUnion.TmpChainList.Flags==DIRTY_ENTRY\
            &&rootPhyMemDscptTbBsPtr[fast_index-1].ReservedUnion.TmpChainList.Flags==CLEAN_ENTRY;
            bool slowindex_stop=rootPhyMemDscptTbBsPtr[slow_index].ReservedUnion.TmpChainList.Flags==CLEAN_ENTRY\
            &&rootPhyMemDscptTbBsPtr[slow_index-1].ReservedUnion.TmpChainList.Flags==DIRTY_ENTRY;
            if (fastindex_stop&&slowindex_stop)
            {
                linearTBSerialDelete(&rootPhymemTbentryCount,fast_index,slow_index-1,(void*)rootPhyMemDscptTbBsPtr,sizeof(EFI_MEMORY_DESCRIPTORX64));
                slow_index=fast_index;
            }else
            {
                if (fastindex_stop==false&&fast_index>0)fast_index--;
                if(slowindex_stop==false&&slow_index>0)slow_index--;
            }
            }
        break;
    default:
        break;
    }    

}
// tbid对于EFI传下来的表是0,后面系统自建的物理内存描述广义表是1
/**
 * @brief 判断两个内存描述符的物理地址空间是否相邻,也就是a的尾部到b有无空洞
 *
 * @param index_a
 * @param index_b
 * @param tbid

 */
inline bool GlobalMemoryPGlevelMgr_t::Ismemspaceneighbors(uint16_t index_a, uint16_t index_b, uint64_t tbid)
{
    switch (tbid)
    {
    case 0 :
        if(EfiMemMap[index_a].PhysicalStart + 4096*EfiMemMap[index_a].NumberOfPages == EfiMemMap[index_b].PhysicalStart)
        return true;
        else return false;
    case 1 :
        if (rootPhyMemDscptTbBsPtr[index_a].PhysicalStart+
        4096*rootPhyMemDscptTbBsPtr[index_a].NumberOfPages == 
        rootPhyMemDscptTbBsPtr[index_b].PhysicalStart)
        {
            return true;
        }else return false;
        
    default:
        break;
    }    

    
}/**
     * 整体思路：
     * 1.EfiMemMap把其中的启动时服务修改成空余块
     * 2.空余块合并(脏项删除搞成另外一个私有函数进行实现)
     * 
     * 
     * 将连续的内存块进行合并涉及到
     * 
     *      * 在表中一个一个根据根后继是否连续是否删除块，
     * 1.若连续则修改后继项的起始物理地址与长度与项类型，对于前驱项在flags处标记为脏
     * 2.若不连续则只改变当前项的类型
     * 由尾部到头部遍历数组删除脏项（线性表的删除）
     * 在根据efi表回收完启动时服务相关内存后，
     * 还要手动分配一个页来复制新的物理内存描述符表，在这之后才能用物理页分配器
     * 并在新建的物理内存描述表上把0x4000000连续的内存块的描述改为OS_KERNEL_CODE
     * OS_KERNEL_DATA，上面的改动必须完全同步到物理内存描述符表与efi表
     */
void GlobalMemoryPGlevelMgr_t::reclaimBootTimeMemoryonEfiTb()
{   
    fillMemoryHolesInEfiMap();
    for(uint64_t i = 0; i < EfiMemMapEntryCount; i++)
    //此循环遍历EFI物理内存描述符表统计启动时服务项数
    {
         if(EfiMemMap[i].Type == EfiBootServicesData||
        EfiMemMap[i].Type == EfiBootServicesCode)
        {
            EfiMemMap[i].Type = freeSystemRam;

        }
    }
    for(uint64_t i = 0; i < EfiMemMapEntryCount-1; i++)//表的最后一项通常不会是启动时服务项所以减1
    //此循环遍历EFI物理内存描述符表
    {   
        if(EfiMemMap[i].Type == freeSystemRam)
        {
            if(EfiMemMap[i+1].Type == freeSystemRam)
            {//后继也是启动时服务项
                if (Ismemspaceneighbors(i,i+1,0))
                {
                    EfiMemMap[i].ReservedUnion.TmpChainList.Flags=DIRTY_ENTRY;
                    EfiMemMap[i+1].PhysicalStart=EfiMemMap[i].PhysicalStart;
                    EfiMemMap[i+1].NumberOfPages+=EfiMemMap[i].NumberOfPages;

                }
                
            } 
        }
    }
    dirtyentrydelete(0);

}
/**
 * @brief 
 * 
 * @param gEfiMemdescriptromap 
 * @param entryCount 
*初始化物理内存描述符表是通过复制已经回收了启动时服务内存的efi表，根据约定标记内核数据，内核代码后再
*在物理内存描述符表中回收loader使用的内存
 */
void GlobalMemoryPGlevelMgr_t::InitrootPhyMemDscptTbBsPtr()
{
        int pgcounts_for_phyramtb=(EfiMemMapEntryCount*sizeof(EFI_MEMORY_DESCRIPTORX64)+
    PAGE_SIZE_4KB-1)/PAGE_SIZE_4KB+15;
    int kernelcodeDesIndex;
    for (int i=0; i < EfiMemMapEntryCount; i++)
    {
        if ((EfiMemMap[i].Type==EfiLoaderCode)&&EfiMemMap[i].PhysicalStart==KEFLBASE)
        {
            kernelcodeDesIndex=i;
            break;
        }
    }
    int kerneldataDesIndex;
        for (int i=kernelcodeDesIndex; i < EfiMemMapEntryCount; i++)
    {
        if ((EfiMemMap[i].Type==EfiLoaderData)&&EfiMemMap[i+1].Type==EfiConventionalMemory)
        {
            kerneldataDesIndex=i;
            break;
        }
    }
   rootPhyMemDscptTbBsPtr=\
   (phy_memDesriptor*)(EfiMemMap[kerneldataDesIndex].PhysicalStart+
    PAGE_SIZE_4KB*EfiMemMap[kerneldataDesIndex].NumberOfPages);
   EfiMemMap[kerneldataDesIndex].NumberOfPages+=pgcounts_for_phyramtb;
   EfiMemMap[kerneldataDesIndex+1].PhysicalStart+=pgcounts_for_phyramtb*PAGE_SIZE_4KB;
   EfiMemMap[kerneldataDesIndex+1].NumberOfPages-=pgcounts_for_phyramtb;
    ksystemramcpy(EfiMemMap,
    rootPhyMemDscptTbBsPtr,
    EfiMemMapEntryCount*sizeof(EfiMemMap[0]));
    rootPhymemTbentryCount=EfiMemMapEntryCount;
    rootPhyMemDscptTbBsPtr[kernelcodeDesIndex].Type=OS_KERNEL_CODE;
    rootPhyMemDscptTbBsPtr[kerneldataDesIndex].Type=OS_KERNEL_DATA;
    for (size_t i = 0; i < rootPhymemTbentryCount; i++)
    {
        if (rootPhyMemDscptTbBsPtr[i].Type==EfiLoaderData&&
        rootPhyMemDscptTbBsPtr[i].PhysicalStart==0x400000)
        {
            rootPhyMemDscptTbBsPtr[i].Type=OS_KERNEL_STACK;
        }
        
    }  
    reclaimLoaderMemory(); 
}
// 对EFI内存表按物理起始地址排序
static int compareEfiDescriptors(const void* a, const void* b) {
    const EFI_MEMORY_DESCRIPTORX64* descA = (const EFI_MEMORY_DESCRIPTORX64*)a;
    const EFI_MEMORY_DESCRIPTORX64* descB = (const EFI_MEMORY_DESCRIPTORX64*)b;
    
    if (descA->PhysicalStart < descB->PhysicalStart) return -1;
    if (descA->PhysicalStart > descB->PhysicalStart) return 1;
    return 0;
}
void GlobalMemoryPGlevelMgr_t::sortEfiMemoryMapByPhysicalStart() {
    // 使用简单的冒泡排序，因为EFI内存表通常不会太大
    for (uint64_t i = 0; i < EfiMemMapEntryCount - 1; i++) {
        for (uint64_t j = 0; j < EfiMemMapEntryCount - i - 1; j++) {
            if (EfiMemMap[j].PhysicalStart > EfiMemMap[j + 1].PhysicalStart) {
                // 交换两个描述符
                EFI_MEMORY_DESCRIPTORX64 temp = EfiMemMap[j];
                EfiMemMap[j] = EfiMemMap[j + 1];
                EfiMemMap[j + 1] = temp;
            }
        }
    }
}
// 检测并填充内存空洞
void GlobalMemoryPGlevelMgr_t::fillMemoryHolesInEfiMap() {
    // 先对内存表排序
    sortEfiMemoryMapByPhysicalStart();
    
    // 检测内存空洞并插入Reserved类型的描述符
    for (uint64_t i = 0; i < EfiMemMapEntryCount - 1; i++) {
        uint64_t currentEnd = EfiMemMap[i].PhysicalStart + 
                             (EfiMemMap[i].NumberOfPages * EFI_PAGE_SIZE);
        uint64_t nextStart = EfiMemMap[i + 1].PhysicalStart;
        
        // 检查是否存在空洞
        if (currentEnd < nextStart) {
            // 计算空洞大小（以页为单位）
            uint64_t holePages = (nextStart - currentEnd) / EFI_PAGE_SIZE;
            
            // 创建Reserved类型的描述符
            EFI_MEMORY_DESCRIPTORX64 holeDesc = {0};
            holeDesc.Type = EFI_RESERVED_MEMORY_TYPE;
            holeDesc.PhysicalStart = currentEnd;
            holeDesc.NumberOfPages = holePages;
            holeDesc.Attribute = 0;
            
            // 插入到表中
            linearTBSerialInsert(
                &EfiMemMapEntryCount,
                i + 1,
                &holeDesc,
                EfiMemMap,
                sizeof(EFI_MEMORY_DESCRIPTORX64)
            );
            
            // 跳过刚刚插入的项
            i++;
        }
    }
}
// 回收Loader内存（非Runtime属性）
void GlobalMemoryPGlevelMgr_t::reclaimLoaderMemory() {
    // 遍历物理内存描述符表，回收Loader内存
    for (uint64_t i = 0; i < rootPhymemTbentryCount; i++) {
        phy_memDesriptor* desc = &rootPhyMemDscptTbBsPtr[i];
        
        // 只处理Loader类型的内存且不包含Runtime属性
        if ((desc->Type == EFI_LOADER_CODE || desc->Type == EFI_LOADER_DATA) &&
            !(desc->Attribute & EFI_MEMORY_RUNTIME)) {
            // 标记为系统空闲内存
            desc->Type = freeSystemRam;
            
            // 设置临时标志用于后续合并
            desc->ReservedUnion.TmpChainList.Flags = CLEAN_ENTRY;
        }
    }
    
    // 合并相邻的空闲内存块
    bool merged;
    do {
        merged = false;
        for (uint64_t i = 0; i < rootPhymemTbentryCount - 1; i++) {
            if (rootPhyMemDscptTbBsPtr[i].Type == freeSystemRam &&
                rootPhyMemDscptTbBsPtr[i+1].Type == freeSystemRam &&
                Ismemspaceneighbors(i, i+1, 1)) {
                // 合并到当前项
                rootPhyMemDscptTbBsPtr[i].NumberOfPages += rootPhyMemDscptTbBsPtr[i+1].NumberOfPages;
                // 标记下一项为脏
                rootPhyMemDscptTbBsPtr[i+1].ReservedUnion.TmpChainList.Flags = DIRTY_ENTRY;
                merged = true;
            }
        }
        if (merged) {
            dirtyentrydelete(1); // 删除脏项
        }
    } while (merged);
}
int GlobalMemoryPGlevelMgr_t::FixedPhyaddPgallocate(
    IN phyaddr_t addr,
    IN uint64_t size,
    IN PHY_MEM_TYPE type)
{
    if (Statusflags==0)
    {
       return -EINVAL;
    }
    
    const uint64_t requiredPages = (size + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
    
    // 验证地址对齐
    if (addr % PAGE_SIZE_4KB != 0) {
        return -EINVAL;
    }
    
    phy_memDesriptor* desc = queryPhysicalMemoryUsage(addr);
    if (!desc || desc->Type != freeSystemRam) {
        return -ENOMEM;
    }
    
    const uint64_t endAddr = addr + requiredPages * PAGE_SIZE_4KB;
    const uint64_t descEnd = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE_4KB;
    
    if (endAddr > descEnd) {
        return -ENOMEM;
    }
    
    const uint64_t beforePages = (addr - desc->PhysicalStart) / PAGE_SIZE_4KB;
    const uint64_t afterPages = (descEnd - endAddr) / PAGE_SIZE_4KB;
    
    if (beforePages > 0 && afterPages > 0) {
        // ==== 修复1：正确分裂为三个区域 ====
        phy_memDesriptor originalDesc = *desc;  // 保存原始信息
        
        // 1. 修改当前描述符为前部空闲
        desc->NumberOfPages = beforePages;  // 保持freeSystemRam类型
        
        // 2. 创建中间分配描述符
        phy_memDesriptor midDesc = originalDesc;
        midDesc.PhysicalStart = addr;
        midDesc.NumberOfPages = requiredPages;
        midDesc.Type = type;  // 设置分配类型
        
        // 3. 创建后部空闲描述符
        phy_memDesriptor afterDesc = originalDesc;
        afterDesc.PhysicalStart = endAddr;
        afterDesc.NumberOfPages = afterPages;
        
        // 插入两个新描述符（分配块+后部空闲）
        phy_memDesriptor newDescs[2] = {midDesc, afterDesc};
        linearTBSerialInsert(
            &rootPhymemTbentryCount,
            desc - rootPhyMemDscptTbBsPtr + 1,
            newDescs,
            rootPhyMemDscptTbBsPtr,
            sizeof(phy_memDesriptor),
            2  // 插入两个条目
        );
    }
    else if (beforePages > 0) {
        // 情况2：分配尾部区域
        phy_memDesriptor newDesc = *desc;
        newDesc.PhysicalStart = addr;
        newDesc.NumberOfPages = requiredPages;
        newDesc.Type = type;  // 设置分配类型
        
        // 修改原描述符为前部空闲（保持freeSystemRam类型）
        desc->NumberOfPages = beforePages;
        
        // 插入分配块
        linearTBSerialInsert(
            &rootPhymemTbentryCount,
            desc - rootPhyMemDscptTbBsPtr + 1,
            &newDesc,
            rootPhyMemDscptTbBsPtr,
            sizeof(phy_memDesriptor)
        );
        
        // ==== 修复2：删除错误的类型设置 ====
        // desc->Type 保持 freeSystemRam 不变
    }
    else if (afterPages > 0) {
        // 情况3：分配头部区域
        phy_memDesriptor newDesc = *desc;
        newDesc.PhysicalStart = addr;
        newDesc.NumberOfPages = requiredPages;
        newDesc.Type = type;  // 设置分配类型
        
        // 修改原描述符为后部空闲
        desc->PhysicalStart = endAddr;
        desc->NumberOfPages = afterPages;
        
        // 插入分配块
        linearTBSerialInsert(
            &rootPhymemTbentryCount,
            desc - rootPhyMemDscptTbBsPtr,
            &newDesc,
            rootPhyMemDscptTbBsPtr,
            sizeof(phy_memDesriptor)
        );
        
        // ==== 修复3：删除错误的类型设置 ====
        // desc->Type 保持 freeSystemRam 不变
    }
    else {
        // 整个块分配（无前后空闲区域）
        desc->Type = type;
    }
    
    return 0;
}

int GlobalMemoryPGlevelMgr_t::defaultPhyaddPgallocate(
    IN OUT phyaddr_t& addr,  // 通过引用修改addr
    IN uint64_t size,
    IN PHY_MEM_TYPE type)
{
        if (Statusflags==0)
    {
       return -EINVAL;
    }
    const uint64_t requiredPages = (size + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
    
    // 遍历查找合适的空闲块
    for (uint64_t i = 0; i < rootPhymemTbentryCount; ++i) {
        phy_memDesriptor* desc = &rootPhyMemDscptTbBsPtr[i];
        
        if (desc->Type == freeSystemRam && 
            desc->NumberOfPages >= requiredPages) {
            
            addr = desc->PhysicalStart;  // 设置输出地址
            return FixedPhyaddPgallocate(addr, size, type);
        }
    }
    
    return -ENOMEM;  // 没有找到合适的内存块
}

void* GlobalMemoryPGlevelMgr_t::SameNeighborMerge(phyaddr_t addr)
{
        if (Statusflags==0)
    {
       return NULL;
    }
    // 查找包含指定地址的内存描述符
    phy_memDesriptor* current = queryPhysicalMemoryUsage(addr);
    if (!current || current->Type == freeSystemRam) 
        return NULL;

    // 获取当前描述符在表中的索引
    uint64_t currentIdx = current - rootPhyMemDscptTbBsPtr;
    uint64_t startIdx = currentIdx;  // 合并起始索引
    uint64_t endIdx = currentIdx;    // 合并结束索引
    bool merged = false;

    // 向前查找可合并的连续块
    for (int64_t i = currentIdx - 1; i >= 0; i--) {
        if (rootPhyMemDscptTbBsPtr[i].Type == current->Type &&
            Ismemspaceneighbors(i, i+1, 1)) 
        {
            startIdx = i;
        } else {
            break;
        }
    }

    // 向后查找可合并的连续块
    for (uint64_t i = currentIdx + 1; i < rootPhymemTbentryCount; i++) {
        if (rootPhyMemDscptTbBsPtr[i].Type == current->Type &&
            Ismemspaceneighbors(i-1, i, 1)) 
        {
            endIdx = i;
        } else {
            break;
        }
    }

    // 如果不需要合并（只有一个块）
    if (startIdx == endIdx) 
        return current;

    // ==== 合并连续块 ====
    merged = true;
    
    // 1. 计算合并后的总页数
    uint64_t totalPages = 0;
    for (uint64_t i = startIdx; i <= endIdx; i++) {
        totalPages += rootPhyMemDscptTbBsPtr[i].NumberOfPages;
    }
    
    // 2. 更新起始块的描述符
    rootPhyMemDscptTbBsPtr[startIdx].NumberOfPages = totalPages;
    
    // 3. 标记后续块为脏（准备删除）
    for (uint64_t i = startIdx + 1; i <= endIdx; i++) {
        rootPhyMemDscptTbBsPtr[i].ReservedUnion.TmpChainList.Flags = DIRTY_ENTRY;
    }
    
    // 4. 批量删除标记为脏的项
    dirtyentrydelete(1);  // tbid=1表示操作系统内存描述表
    
    return &rootPhyMemDscptTbBsPtr[startIdx];
}
/**
 *页回收函数传入的地址必须为某一个项的物理地址，否则会报错
 *一个地址回收后其地址会尽可能与相邻的其它空闲内存块合并
 */
int GlobalMemoryPGlevelMgr_t::pageRecycle(phyaddr_t EntryStartphyaddr)
{
        if (Statusflags==0)
    {
       return -EINVAL;
    }
    // 1. 查找包含该地址的内存描述符
    phy_memDesriptor* desc = queryPhysicalMemoryUsage(EntryStartphyaddr);
    if (!desc) {
        return -ENOENT; // 地址未找到
    }

    // 2. 验证地址必须是描述符的起始地址
    if (desc->PhysicalStart != EntryStartphyaddr) {
        return -EINVAL; // 地址不是内存块的起始地址
    }

    // 3. 保存原始类型用于合并条件检查
    PHY_MEM_TYPE originalType = static_cast<PHY_MEM_TYPE>(desc->Type);
    
    // 4. 将内存标记为空闲（准备回收）
    desc->Type = freeSystemRam;

    // 5. 尝试向前合并（与前一个空闲块）
    if (desc > rootPhyMemDscptTbBsPtr) {
        phy_memDesriptor* prevDesc = desc - 1;
        if (prevDesc->Type == freeSystemRam && 
            Ismemspaceneighbors(prevDesc - rootPhyMemDscptTbBsPtr, 
                               desc - rootPhyMemDscptTbBsPtr, 1)) 
        {
              desc->NumberOfPages+=prevDesc->NumberOfPages;
              desc->PhysicalStart = prevDesc->PhysicalStart;
            prevDesc->ReservedUnion.TmpChainList.Flags = DIRTY_ENTRY;
        }
    }

    // 6. 尝试向后合并（与后一个空闲块）
    if ((desc - rootPhyMemDscptTbBsPtr) < (rootPhymemTbentryCount - 1)) {
        phy_memDesriptor* nextDesc = desc + 1;
        if (nextDesc->Type == freeSystemRam && 
            Ismemspaceneighbors(desc - rootPhyMemDscptTbBsPtr, 
                              nextDesc - rootPhyMemDscptTbBsPtr, 1)) 
        {
            desc->NumberOfPages += nextDesc->NumberOfPages;
            nextDesc->ReservedUnion.TmpChainList.Flags = DIRTY_ENTRY;
        }
    }

    // 7. 清理脏标记的条目
    dirtyentrydelete(1); // tbid=1 表示操作系统内存描述表

    return 0; // 回收成功
}

void GlobalMemoryPGlevelMgr_t::pageSetValue(phyaddr_t EntryStartphyaddr, uint64_t value)
{
    
    phy_memDesriptor* Entry = queryPhysicalMemoryUsage(EntryStartphyaddr);
    if(Entry)
    {uint64_t* p = (uint64_t*)Entry->PhysicalStart;
        for (int i = 0; i < Entry->NumberOfPages*512; i++)
        {
            p[i]=value;
        }
        
    }else{
        kputsSecure("pageSetValue:Entry is null,Invalid EntryStartphyaddr");
    }
    
}

const char *MemoryTypeToString(UINT32 type)
{
    switch (type) {
        case 0:  return "Reserved";
        case 1:  return "LoaderCode";
        case 2:  return "LoaderData";
        case 3:  return "BootServicesCode";
        case 4:  return "BootServicesData";
        case 5:  return "RuntimeServicesCode";
        case 6:  return "RuntimeServicesData";
        case 7:  return "ConventionalMemory";
        case 8:  return "UnusableMemory";
        case 9:  return "ACPIReclaimMemory";
        case 10: return "ACPIMemoryNVS";
        case 11: return "MemoryMappedIO";
        case 12: return "MemoryMappedIOPortSpace";
        case 13: return "PalCode";
        case 14: return "PERSISTENT_MEMORY";
        case 15: return "EFI_UNACCEPTED_MEMORY_TYPE";
        case 16: return "EFI_MAX_MEMORY_TYPE";
        case 17: return "OS_KERNEL_DATA";
        case 18: return "OS_KERNEL_CODE";
        case 19: return "OS_KERNEL_STACK";

        default: return "UnknownType";
    }
}

// 打印单个内存描述符
void PrintMemoryDescriptor(const EFI_MEMORY_DESCRIPTORX64* desc) {
    if(desc->ReservedUnion.TmpChainList.Flags==DIRTY_ENTRY)return;
    // 1. 打印起始物理地址
    kputsSecure("Start: 0x");
    kpnumSecure((void*)&desc->PhysicalStart, UNHEX, sizeof(EFI_PHYSICAL_ADDRESS));
    
    // 2. 计算并打印终止物理地址
    UINT64 endAddress = desc->PhysicalStart + (desc->NumberOfPages * EFI_PAGE_SIZE);
    kputsSecure(" - End: 0x");
    kpnumSecure((void*)&endAddress, UNHEX, sizeof(UINT64));
    
    // 3. 打印内存类型和属性
    kputsSecure(" Type: ");
    kputsSecure((char*)MemoryTypeToString(desc->Type));
    
    kputsSecure(" Attr: 0x");
    kpnumSecure((void*)&desc->Attribute, UNHEX, sizeof(UINT64));
    
    // 4. 打印页数（可选）
    kputsSecure(" Pages: ");
    kpnumSecure((void*)&desc->NumberOfPages, UNDEC, sizeof(UINT64));
    
    // 换行
    kputsSecure("\n");
}
void GlobalMemoryPGlevelMgr_t::printEfiMemoryDescriptorTable()
{
        kputsSecure("\n========== Memory Map ==========\n");
    
    // 打印表头
    kputsSecure("Physical Range             Type               Attribute    Pages\n");
    kputsSecure("-----------------------------------------------------------------\n");
    
    // 遍历所有条目
    for (UINTN i = 0; i < EfiMemMapEntryCount; i++) {
        kpnumSecure((void*)&i, UNDEC, sizeof(UINTN));
        kputsSecure(": ");
        PrintMemoryDescriptor(EfiMemMap+i);
    }
    
    kputsSecure("========== End of Map ==========\n");
}

void GlobalMemoryPGlevelMgr_t::printPhyMemDesTb()
{
       kputsSecure("\n========== Phy Memory Map ==========\n");
    
    // 打印表头
    kputsSecure("Physical Range             Type               Attribute    Pages\n");
    kputsSecure("-----------------------------------------------------------------\n");
    
    // 遍历所有条目
    for (UINTN i = 0; i < rootPhymemTbentryCount; i++) {
        kpnumSecure((void*)&i, UNDEC, sizeof(UINTN));
        kputsSecure(": ");
        PrintMemoryDescriptor((EFI_MEMORY_DESCRIPTORX64*)(rootPhyMemDscptTbBsPtr+i));
    }
    
    kputsSecure("========== End of Map ==========\n");
}

void GlobalMemoryPGlevelMgr_t::DisableBasicMemService()
{
    Statusflags=0;
}

