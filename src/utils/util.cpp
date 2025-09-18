#include "OS_utils.h"
#include "stdint.h"
typedef uint64_t size_t;
const uint8_t masks_entry1bit_width[8]={128,64,32,16,8,4,2,1};
const uint8_t masks_entry2bits_width[4]={192,48,12,3};

int strlen(const char *s) {
    int len = 0;
    while (*s++)
        len++;
    return len;
}
 void setmem(void* ptr, uint64_t size_in_byte, uint8_t value) {
    uint8_t* p = static_cast<uint8_t*>(ptr);
    
    // 使用64位写入来加速内存设置
    uint64_t value64 = value;
    value64 |= value64 << 8;
    value64 |= value64 << 16;
    value64 |= value64 << 32;
    
    // 处理前缀不对齐部分
    while (size_in_byte > 0 && (reinterpret_cast<uint64_t>(p) & 7)) {
        *p++ = value;
        size_in_byte--;
    }
    
    // 使用64位写入处理主体部分
    uint64_t* p64 = reinterpret_cast<uint64_t*>(p);
    while (size_in_byte >= 8) {
        *p64++ = value64;
        size_in_byte -= 8;
    }
    
    // 处理剩余部分
    p = reinterpret_cast<uint8_t*>(p64);
    while (size_in_byte > 0) {
        *p++ = value;
        size_in_byte--;
    }
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
    uint64_t entryCount
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
bool getbit_entry1bit_width(_2mb_pg_bitmapof_4kbpgs* bitmap,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    return (map[index>>3]&masks_entry1bit_width[index&7])!=0;
}
void setbit_entry1bit_width(_2mb_pg_bitmapof_4kbpgs*bitmap,bool value,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    if(value)
        map[index>>3]|=masks_entry1bit_width[index&7];
    else
        map[index>>3]&=~masks_entry1bit_width[index&7];
}
void setbits_entry1bit_width(_2mb_pg_bitmapof_4kbpgs*bitmap,bool value,uint16_t Start_index,uint16_t len_in_bits)
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
            setbit_entry1bit_width(bitmap,value,i);
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
// 获取2bit宽度位图中指定索引的值（返回0-3）
uint8_t getentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint16_t index) {
    uint8_t byte = bitmap[index >> 2];  // 每个字节包含4个2bit条目
    uint8_t shift = (index & 3) * 2;    // 计算在字节内的偏移（0,2,4,6）
    return (byte >> shift) & 3;         // 提取2bit值
}

// 设置2bit宽度位图中指定索引的值
void setentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t index) {
    uint8_t& byte = bitmap[index >> 2];     // 获取对应的字节引用
    uint8_t shift = (index & 3) * 2;        // 计算偏移量
    byte = (byte & ~(3 << shift)) |         // 清除原有值
           ((value & 3) << shift);          // 设置新值
}

// 设置2bit宽度位图中连续多个条目的值
// 优化后的批量设置2bit宽度位图函数
void setentries_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t start_index, uint16_t len_in_entries) {
    value &= 3;  // 确保值在0-3范围内
    
    // 创建64位填充模式（每个2bit都是value）
    uint64_t fillpattern = 0;
    for (int i = 0; i < 32; i++) {  // 64位可容纳32个2bit条目
        fillpattern |= (static_cast<uint64_t>(value) << (i * 2));
    }
    
    uint16_t i = start_index;
    uint16_t end_index = start_index + len_in_entries;
    
    // 处理起始未对齐部分（按单个条目设置）
    while (i < end_index && (i & 31)) {
        setentry_entry2bits_width(bitmap, value, i);
        i++;
    }
    
    // 处理中间对齐部分（按64位块设置，每次设置32个条目）
    uint64_t* map_64bit = reinterpret_cast<uint64_t*>(&bitmap[i >> 2]);
    while (i + 31 < end_index) {
        *map_64bit++ = fillpattern;
        i += 32;
    }
    
    // 处理剩余未对齐部分（按单个条目设置）
    while (i < end_index) {
        setentry_entry2bits_width(bitmap, value, i);
        i++;
    }
}