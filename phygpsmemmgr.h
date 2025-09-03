#pragma once
#include "stdint.h"
#include "Memory.h"
typedef  uint64_t phyaddr_t;
typedef uint8_t pgsbitmap[64];


struct pgflags
{
    uint64_t physical_or_virtual_pg:1;//0表示物理页，1表示虚拟页
    uint64_t is_exist:1;//物理页是否存在，虚拟页若是不存在要考虑在交换空间
    uint64_t is_atom:1;//原子节点才能被锁，内核标识位，读写运行权限位有效
    //也只有原子节点才能被游程压缩，虚拟页的游程压缩可能使用
    uint64_t is_dirty:1;
    uint64_t is_lowerlv_bitmap:1;    
    //如果is_lowerlv_bitmap为1,那么对下一个级别的表项是用1个bit的存储信息，
    //这些子表项必然全部为原子节点，其对应的bit项代表的是这个页是否空闲
    //显然只有低一半的恒等映射物理地址空间才会标记内存是否空闲
    //这个位为1说明内存可分配，而且权限是读写
    //对于权限带有可运行的内存页项（按照对应线性地址解析下来的页），则子表不能用位图
    //不带有可运行的内存页项，则子表可以使用子图，而且是继承了上级表项的读写相关权限
    //内核低物理地址恒等映射空间一般只会给uefi运行时服务，内核代码段给予运行权限，
    //而这个表是记录了相关线性地址的权限，后面就算有新的主动加载内核模块也只会加载到高位内核空间
    //更不用说用户态程序，基本上不会在低物理地址恒等映射区间分配具有运行权限的内存
    uint64_t is_locked:1;
    uint64_t is_shared:1;
    uint64_t is_reserved:1;
    /**
     * 对于is_atom=0,is_reserved=1的节点说明子表存在保留的原子节点，不能参与内存分配，并且子表不能用位图表示，最多使用游程压缩
     */
    uint64_t is_occupied:1;
    /**
     * 在physical_or_virtual_pg为物理表，存在，是原子节点的情况下
     * 标记这个表是不是空闲表还是非空闲表
     */
    uint64_t is_kernel:1;
    //低物理地址恒等映射内存默认且必须全部设为内核内存
    uint64_t is_readable:1;
    uint64_t is_writable:1;
    uint64_t is_executable:1;
    uint64_t is_run_len_encoding_enabled:1;
    //控制子一级别表是否开启游程压缩
    uint64_t start_index:9;//游程压缩可能使用
    uint64_t end_index:9;
    uint64_t pg_lv:3;
};
struct psmemmgr_flags_t
{
   uint64_t is_run_len_encoding_enabled:1;//此位开启会让非原子节点中的is_run_len_encoding_enabled
   //起作用，若开启游程压缩则对应子表开启游程压缩
   
   uint64_t Pgtb_situate:2;
};
struct lowerlv_PgCBtb;
struct lowerlv_bitmap;
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
struct lowerlv_PgCBtb
{
    uint8_t* pgextention;//父表项的拓展数据
    PgControlBlockHeader entries[512];
};
struct lowerlv_bitmap
{
    void* pgextention;//父表项的拓展数据
    pgsbitmap bitmap;
};





struct pgs_queue_entry_t
{
    uint64_t pgs_count;
    uint8_t pgs_lv;
    uint8_t reserved;
};

struct phymem_pgs_queue//这是在物理内存描述符表转换为类页表结构中的中间体数据结构，用数组存储存储这段内存需要的页表
/**
 * 
 */
{
    uint8_t entry_count=0;
    pgs_queue_entry_t entry[10]={0};//最极端的情况是4kb,2mb,1gb,512gb,256tb,512gb,1gb,2mb,4kb
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
psmemmgr_flags_t flags;
PgControlBlockHeader*rootlv4PgCBtb=nullptr;
// 辅助函数：打印四级表
void PrintLevel4Table(lowerlv_PgCBtb* table, int parentIndex);
// 辅助函数：打印三级表
void PrintLevel3Table(lowerlv_PgCBtb* table, int grandParentIndex, int parentIndex);
// 辅助函数：打印二级表
void PrintLevel2Table(lowerlv_PgCBtb* table, int greatGrandParentIndex, int grandParentIndex, int parentIndex);
// 辅助函数：打印一级表
void PrintLevel1Table(lowerlv_PgCBtb* table, int greatGreatGrandParentIndex, int greatGrandParentIndex, int grandParentIndex, int parentIndex);
// 辅助函数：计算物理地址
uint64_t CalculatePhysicalAddress(int index5, int index4, int index3, int index2, int index1, int index0);
// 辅助函数：打印页表项信息
void PrintPageTableEntry(PgControlBlockHeader* entry, int level);

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
int construct_pgsbasedon_phy_memDescriptor (phy_memDesriptor memDescriptor);
/**
 * 第0级别表在创建的时候会对高级别的表项进行解析，初始化，
 * 如果存在高级别表项那么就直接使用，不存在那么就创建
 * 比如在完全没有高级别表项的时候，0地址rootlv4PgCBtb一个节点一个节点的开始构建
 * 这几个函数会直接复制flags的表项，不做任何检查
 */
/**
 * 其它级别表项的创建方式与第0级别表项类似，只是解析的表项不同
 */
/**
 * 此函数对全是原子节点的表项进行优化，优先尝试位图，若是不行则考虑游程压缩
 */
int pre_pgtb_optimize();
public:
    //PgsMemMgr();
    //~PgsMemMgr();
    void Init();
    void PrintPgsMemMgrStructure();
};
extern PgsMemMgr gPgsMemMgr;