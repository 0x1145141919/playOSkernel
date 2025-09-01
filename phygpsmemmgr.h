#pragma once
#include "stdint.h"
#include "Memory.h"
typedef  uint64_t phyaddr_t;
typedef uint8_t pgsbitmap[64];


struct pgflags
{
    uint64_t physical_or_virtual_pg:1;//0表示物理页，1表示虚拟页
    uint64_t is_exist:1;//物理页是否存在，虚拟页若是不存在要考虑在交换空间
    uint64_t is_atom:1;//只有为原子节点才能被锁，内核标识位，读写运行权限位才有效
    //也只有原子节点才能被游程压缩，虚拟页的游程压缩可能使用
    uint64_t is_dirty:1;
    uint64_t is_lowerlv_bitmap:1;
    uint64_t is_locked:1;
    uint64_t is_shared:1;
    uint64_t is_reserved:1;
    uint64_t is_free:1;
    uint64_t is_kernel:1;
    uint64_t is_readable:1;
    uint64_t is_writable:1;
    uint64_t is_executable:1;
    uint64_t start_index:9;//游程压缩可能使用
    uint64_t end_index:9;
    uint64_t pg_lv:3;
};
struct lowerlv_PgCBtb
{
    void* pgextention;//父表项的拓展数据
    PgControlBlockHeader entries[512];
};
struct lowerlv_bitmap
{
    void* pgextention;//父表项的拓展数据
    pgsbitmap bitmap;
};


struct  PgControlBlockHeader
{
    pgflags flags;
    union 
    {
        lowerlv_PgCBtb* lowerlvPgCBtb;//如果is_exist:1，is_atom:1那么pgbase理应为0,可以通过前面表的项引索计算出这个项的物理基址
    //如果is_exist:1，is_atom:0那么这个页映射的物理地址下面还有子页，pgbase就是这个页对应页表项的物理基址
    //如果is_exist:0那么这个PgCB对应的物理地址不存在
        lowerlv_bitmap* bitmap;   //如果is_exist:1，is_atom:0那么这个页映射的物理地址下面还有子页，如果is_lowerlv_bitmap:1那么bitmap就是这个页对应页表项的物理基址
        void* pgextentions;
    }base;
    
   
};

typedef struct PgControlBlockHeader PgCBlv4header;
typedef struct PgControlBlockHeader PgCBlv3header;
typedef struct PgControlBlockHeader PgCBlv2header;
typedef struct PgControlBlockHeader PgCBlv1header;
typedef struct PgControlBlockHeader PgCBlv0header;

class PgsMemMgr
{
private:
uint8_t cpu_pglv;//存着cpu处于几级分页模式/四级还是五级
//四级分页最大支持128TB内存，五级分页最大支持64PB内存，留一半映射到高位内存空间作为高位内核空间
uint64_t kernel_space_cr3;
PgControlBlockHeader*rootlv4PgCBtb=nullptr;
int PgCBtb_init(phy_memDesriptor& pmdesc);
/**
 * 根据物理地址通过PML5_INDEX_MASK_lv4提取引索
 * 而后根据其原有项是否存在再说是否为其创建子表，
 * 下面还有几个级别的也是如此
 */
int PgCBtb_lv4_entry_construct(phyaddr_t addr,pgflags flags);
int PgCBtb_lv3_entry_construct(phyaddr_t addr,pgflags flags);
int PgCBtb_lv2_entry_construct(phyaddr_t addr,pgflags flags);
int PgCBtb_lv1_entry_construct(phyaddr_t addr,pgflags flags);
int PgCBtb_lv0_entry_construct(phyaddr_t addr,pgflags flags);
/**
 * 第0级别表在创建的时候会对高级别的表项进行解析，初始化，
 * 如果存在高级别表项那么就直接使用，不存在那么就创建
 * 比如在完全没有高级别表项的时候，0地址rootlv4PgCBtb一个节点一个节点的开始构建
 */
/**
 * 其它级别表项的创建方式与第0级别表项类似，只是解析的表项不同
 */
public:
    PgsMemMgr();
    ~PgsMemMgr();
    void Init();
};
