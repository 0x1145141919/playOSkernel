#include "utils.h"
#include "stdint.h"
typedef uint64_t size_t;
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