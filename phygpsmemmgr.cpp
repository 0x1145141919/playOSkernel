#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "os_error_definitions.h"
const uint8_t masks[8]={128,64,32,16,8,4,2,1};
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


void PgsMemMgr::Init()
{
    uint64_t cr4_tmp;
    int status=0;
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
     /**
      * 接着就是按物理内存描述符表一个一个创建页表项
      */
     

}

int PgsMemMgr::PgCBtb_lv0_entry_construct(phyaddr_t addr, pgflags flags)
{
    uint16_t lv0_index=addr&PT_INDEX_MASK_lv0;
    uint16_t lv1_index=(addr>>9)&PD_INDEX_MASK_lv1;
    uint16_t lv2_index=(addr>>18)&PDPT_INDEX_MASK_lv2;
    uint16_t lv3_index=(addr>>27)&PML4_INDEX_MASK_lv3;
    uint64_t lv4_index=(addr>>36)&PML5_INDEX_MASK_lv4;
    PgCBlv4header*lv4_PgCBHeader=cpu_pglv==5?&rootlv4PgCBtb[lv4_index]:rootlv4PgCBtb;
    if(lv4_PgCBHeader->flags.is_exist!=1);//创建过程检测到不存在就要考虑先创立对应的表项再继续创建
    PgCBlv3header*lv3_PgCBHeader=&lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if(lv3_PgCBHeader->flags.is_exist!=1);//
    PgCBlv2header*lv2_PgCBHeader=&lv3_PgCBHeader->base.lowerlvPgCBtb->entries[lv2_index];
    if(lv2_PgCBHeader->flags.is_exist!=1);
    PgCBlv1header*lv1_PgCBHeader=&lv2_PgCBHeader->base.lowerlvPgCBtb->entries[lv1_index];
    if(lv1_PgCBHeader->flags.is_exist!=1);
    PgCBlv0header*lv0_PgCBHeader=&lv1_PgCBHeader->base.lowerlvPgCBtb->entries[lv0_index];
    lv0_PgCBHeader->flags=flags;
    return 0;
}
