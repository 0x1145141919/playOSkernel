#pragma once
#include "stdint.h"
#include "Memory.h"
typedef  uint64_t phyaddr_t;
typedef uint8_t pgsbitmap_entry1bit_width[64];
typedef uint8_t pgsbitmap_entry2bits_width[128];
enum Phy_mem_type : uint8_t {
    FREE=0,
    RESERVED=2,
    OCCUPYIED=1
};

struct pgflags
{
    uint64_t physical_or_virtual_pg:1;//0表示物理页，1表示虚拟页
    uint64_t is_exist:1;//物理页是否存在，虚拟页若是不存在要考虑在交换空间
    uint64_t is_atom:1;//原子节点才能被锁，内核标识位，读写运行权限位有效
    //也只有原子节点才能被游程压缩，虚拟页的游程压缩可能使用
   uint64_t is_reserved:1;
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
    uint64_t is_remaped:1;
    //有些物理页必然会被重映射，无论内核还是用户态
    uint64_t pg_lv:3;

};
struct psmemmgr_flags_t
{   

};
struct lowerlv_PgCBtb;
struct lowerlv_bitmap_entry_width1bit;
struct lowerlv_bitmap_entry_width2bits;

struct  PgControlBlockHeader
{
    pgflags flags;
    union 
    {
        lowerlv_PgCBtb* lowerlvPgCBtb;
    }base;
    
   
};
constexpr PgControlBlockHeader NullPgControlBlockHeader={0};
struct lowerlv_PgCBtb
{
    uint8_t* pgextention;//父表项的拓展数据
    PgControlBlockHeader entries[512];
};
struct lowerlv_bitmap_entry_width2bits{
    void* pgextention;
    pgsbitmap_entry2bits_width bitmap;
};
struct lowerlv_bitmap_entry_width1bit
{
    void* pgextention;//父表项的拓展数据
    pgsbitmap_entry1bit_width bitmap;
};







typedef struct PgControlBlockHeader PgCBlv4header;
typedef struct PgControlBlockHeader PgCBlv3header;
typedef struct PgControlBlockHeader PgCBlv2header;
typedef struct PgControlBlockHeader PgCBlv1header;
typedef struct PgControlBlockHeader PgCBlv0header;

class PgsMemMgr
{
private:
struct pgs_queue_entry_t
{
    uint64_t pgs_count;
    uint8_t pgs_lv;
    uint8_t reserved;
};

struct phymem_pgs_queue//这是在物理内存描述符表转换为类页表结构中的中间体数据结构，用数组存储存储这段内存需要的页表
/**
 * 同理，只要是基址+段长都可以转换成这个数据结构
 */
{
    uint8_t entry_count=0;
    pgs_queue_entry_t entry[10]={0};//最极端的情况是4kb,2mb,1gb,512gb,256tb,512gb,1gb,2mb,4kb
};

uint8_t cpu_pglv;//存着cpu处于几级分页模式/四级还是五级
//四级分页最大支持128TB内存，五级分页最大支持64PB内存，留一半映射到高位内存空间作为高位内核空间
uint64_t kernel_space_cr3;
psmemmgr_flags_t flags;
struct pgaccess
{
    uint8_t is_kernel:1;
    uint8_t is_writeable:1;
    uint8_t is_readable:1;
    uint8_t is_executable:1;
};
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
int (PgsMemMgr::*PgCBtb_construct_func[5])(phyaddr_t,pgflags)=
{
    &PgsMemMgr::PgCBtb_lv0_entry_construct,
    &PgsMemMgr::PgCBtb_lv1_entry_construct,
    &PgsMemMgr::PgCBtb_lv2_entry_construct,
    &PgsMemMgr::PgCBtb_lv3_entry_construct,
    &PgsMemMgr::PgCBtb_lv4_entry_construct
};
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
 * 下面是五个查询函数，根据物理地址返回对应的表项
 */
PgControlBlockHeader&PgCBtb_lv4_entry_query(phyaddr_t addr);
PgControlBlockHeader&PgCBtb_lv3_entry_query(phyaddr_t addr);
PgControlBlockHeader&PgCBtb_lv2_entry_query(phyaddr_t addr);
PgControlBlockHeader&PgCBtb_lv1_entry_query(phyaddr_t addr);
PgControlBlockHeader&PgCBtb_lv0_entry_query(phyaddr_t addr);
PgControlBlockHeader&(PgsMemMgr::*PgCBtb_query_func[5])(phyaddr_t)=
{
    &PgsMemMgr::PgCBtb_lv0_entry_query,
    &PgsMemMgr::PgCBtb_lv1_entry_query,
    &PgsMemMgr::PgCBtb_lv2_entry_query,
    &PgsMemMgr::PgCBtb_lv3_entry_query,
    &PgsMemMgr::PgCBtb_lv4_entry_query
};

// 定义函数指针类型
/**
 * 此函数用于把一段内存转换成一个队列，方便后续处理，用完这个队列记得手动释放
 */
phymem_pgs_queue*seg_to_queue(phyaddr_t base,uint64_t size_in_bytes);

phyaddr_t Inner_fixed_addr_manage(phyaddr_t base, phymem_pgs_queue queue,bool alloc_or_free,pgaccess access);
public:

const pgaccess PG_RW={1,1,1,0};
const pgaccess PG_RWX ={1,1,1,1};
const pgaccess PG_R ={1,1,0,0};

    void*pgs_allocate(size_t size_in_byte,pgaccess access,uint8_t align_require=12);
    int pgs_fixedaddr_allocate(IN phyaddr_t addr, IN size_t size_in_byte,IN pgaccess access);
    int pgs_free(phyaddr_t addr,size_t size_in_byte);
    phy_memDesriptor* queryPhysicalMemoryUsage(phyaddr_t base,uint64_t len_in_bytes);
    /**
     * 上面四个页级别物理内存分配器必须在is_pgsallocate_enable开启后才能使用    
     */
    void Init();
    void PrintPgsMemMgrStructure();
};
extern PgsMemMgr gPgsMemMgr;
void print_PgControlBlockHeader(struct PgControlBlockHeader* header);