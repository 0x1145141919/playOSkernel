#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include "memory/pgtable45.h"
#include <lock.h>
#include "util/RB_btree.h"
namespace PAGE_TBALE_LV{
    constexpr bool LV_4=true;
    constexpr bool LV_5=false;
}
extern bool pglv_4_or_5;//true代表4级页表，false代表5级页表,在KspacMapMgr.cpp存在
enum cache_strategy_t:uint8_t
{
    UC=0,
    WC=1,
    WT=4,
    WP=5,
    WB=6,
    UC_minus=7
};
struct cache_table_idx_struct_t
{
    uint8_t PWT:1;
    uint8_t PCD:1;
    uint8_t PAT:1;
};
struct seg_to_pages_info_pakage_t{
        struct pages_info_t{
            vaddr_t vbase;
            phyaddr_t base;
            uint64_t page_size_in_byte;
            uint64_t num_of_pages;
        };
        pages_info_t entryies[5];//里面的地址顺序是无序的
};
struct shared_inval_VMentry_info_t{
    seg_to_pages_info_pakage_t info_package;
    bool is_package_valid;
    uint32_t completed_processors_count;
};
union ia32_pat_t
{
   uint64_t value;
   cache_strategy_t  mapped_entry[8];
};
struct pgaccess
{
    uint8_t is_kernel:1;
    uint8_t is_writeable:1;
    uint8_t is_readable:1;
    uint8_t is_executable:1;
    uint8_t is_global:1;
    cache_strategy_t cache_strategy;
};
constexpr pgaccess KSPACE_RW_ACCESS={
    .is_kernel=1,
    .is_writeable=1,
    .is_readable=1,
    .is_executable=0,
    .is_global=1,
    .cache_strategy=WB
};
struct vphypair_t
{//三个参数至少4k对齐
    vaddr_t vaddr;
    phyaddr_t paddr;
    uint32_t size;
};

constexpr uint64_t DESC_SMALL_SEG_MAX=0x3200000;//32mb以上为大段，懒加载，以下为小段，直接全部加载
struct VM_DESC
{
    vaddr_t start;    // inclusive
    vaddr_t end;      // exclusive
                      // 区间长度 = end - start
    // 映射物理页的模式
    enum map_type_t : uint8_t {
        MAP_NONE = 0,     // 未分配物理页（仅占位）
        MAP_PHYSICAL,     // 连续物理页,只有内核因为立即要求而使用，用户空间不能用
        MAP_FILE,         // 文件映射
        MAP_ANON,          // 匿名映射（默认用户空间）
    } map_type;
    phyaddr_t phys_start;  // 当 map_type=MAP_PHYSICAL 时有效
                           // MAP_NONE 没有意义
    pgaccess access;       // 页权限/缓存策略
    uint8_t is_bigseg:1;        // 是否为大段映射（延迟加载）
    uint8_t committed_full:1;   // 物理页是否完全已经分配（lazy allocation 用）
    uint8_t is_vaddr_alloced:1;    // 虚拟地址是否由地址空间管理器分配（否则为固定映射）
    uint8_t is_out_bound_protective:1; // 是否有越界保护区,只有is_vaddr_alloced为1的bit此位才有意义，
};
int VM_vaddr_cmp(VM_DESC* a,VM_DESC* b);
/**
 * 此类的职责就是创建虚拟地址空间，管理虚拟地址空间，
 * 此类的职责有且仅一个功能，就是管理相应的低一般虚拟地址空间，
 * 本类接口不接受低于64k的虚拟地址空间
 * 此类的职责只有映射虚拟地址空间，不提供虚拟地址空间管理功能
 * 若使用此类请自行实现虚拟地址空间的管理
 * 最多提供一个打印实际映射表的接口
 */
class AddressSpace//到时候进程管理器可以用这个类创建，但是内核空间还是受内核空间管理器管理
{ private:
    PML4Entry *pml4;//这个是虚拟地址
    phyaddr_t kspace_pml4_phyaddr;
    uint64_t occupyied_size;
    constexpr static uint64_t PAGE_LV4_USERSPACE_SIZE=0x00007FFFFFFFFFFF+1;
    constexpr static uint64_t PAGE_LV5_USERSPACE_SIZE=0x00FFFFFFFFFFFFFF+1;
    static constexpr uint32_t _4KB_SIZE=0x1000;
    static constexpr uint32_t _2MB_SIZE=1ULL<<21;
    static constexpr uint32_t _1GB_SIZE=1ULL<<30;
    void sharing_kernel_space();//直接使用KernelSpacePgsMemMgr的pml4高一半
    spinrwlock_cpp_t lock;
    
    public:
    AddressSpace();
    int enable_VM_desc(VM_DESC desc);
    int disable_VM_desc(VM_DESC desc);
    int Init();
    int second_stage_init();
    uint64_t get_occupyied_size(){
        return occupyied_size;
    }
    phyaddr_t vaddr_to_paddr(vaddr_t vaddr);
    void load_pml4_to_cr3();//这个接口会直接把当前页表加载到cr3寄存器
    ~AddressSpace();
};
extern AddressSpace gKernelSpace;
constexpr ia32_pat_t DEFAULT_PAT_CONFIG={
    .value=0x407050600070106
};
cache_table_idx_struct_t cache_strategy_to_idx(cache_strategy_t cache_strategy);
/**
 * 这个类的职责有且仅有一个功能，就是管理内核空间，
 * 通过kspacePML4暴露给AddressSpace::sharing_kernel_space()强制复制高一半pml4e
 * 以及类内的kspaceUPpdpt同步所有进程空间的内核结构
 * 此类全局唯一，只管理高128tb虚拟地址空间
 */
class KernelSpacePgsMemMgr//使用上面的位域结构体，在初始化函数中直接用，但在后续正式外部暴露接口中对页表项必须用原子操作函数
{
private://后面五级页表的时候考虑选择编译
PageTableEntryUnion*roottbv;
phyaddr_t root_pml4_phyaddr;
phyaddr_t kspace_uppdpt_phyaddr;
static constexpr uint64_t PAGELV4_KSPACE_BASE=0xFFFF800000000000;
static constexpr uint64_t PAGELV5_KSPACE_BASE=0xFF00000000000000;
static constexpr uint64_t PAGELV4_KSPACE_SIZE=1ULL<<(48-1);
static constexpr uint64_t PAGELV5_KSPACE_SIZE=1ULL<<(57-1);

static constexpr uint32_t _4KB_SIZE=0x1000;
static constexpr uint32_t _2MB_SIZE=1ULL<<21;
static constexpr uint32_t _1GB_SIZE=1ULL<<30;

bool is_default_pat_config_enabled=false;


//这个数组按照虚拟地址从小到大排序,规定虚拟地址是主键
class kspace_vm_table_t:public RBTree_t
{
    private:    
    using RBTree_t::root;
    using RBTree_t::cmp;
    using RBTree_t::left_rotate;
    using RBTree_t::right_rotate;
    using RBTree_t::fix_insert;
    using RBTree_t::subtree_min;
    using RBTree_t::fix_remove;
    using RBTree_t::subtree_max;
    using RBTree_t::successor;
    public:
    kspace_vm_table_t():RBTree_t((int (*)(const void*, const void*))VM_vaddr_cmp){
        
    }
    using RBTree_t::search;
    using RBTree_t::insert;
    using RBTree_t::remove;
    vaddr_t alloc_available_space(uint64_t size,uint32_t target_vaddroffset);
};
kspace_vm_table_t*kspace_vm_table;
spinlock_cpp_t GMlock;
friend int AddressSpace::Init();
friend AddressSpace::AddressSpace();
/**
 * 往kspace_vm_table插入vmentry，
 * 遵守虚拟地址从小到大排序，
 * 内部接口默认外部调用函数持有锁，
 * 不对参数合法性进行校验
 */
int VM_add(VM_DESC vmentry);
/**
 * 往kspace_vm_table删除虚拟地址为vaddr的VM_DESC项，
 * 遵守虚拟地址从小到大排序，
 * 内部接口默认外部调用函数持有锁
 */
int VM_del(VM_DESC*entry);
/**
 * 搜索虚拟地址为vaddr的VM_DESC项，
 * 建议采取二分查找
 */
int VM_search_by_vaddr(vaddr_t vaddr,VM_DESC&result);

int _4lv_pdpte_1GB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨父页表项指针边界

int _4lv_pde_2MB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access,bool is_pagetballoc_reserved);//这里要求的是不能跨页目录指针边界

int _4lv_pte_4KB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access,bool is_pagetballoc_reserved);//这里要求的是不能跨页目录边界


/**
 * 
 */
int seg_to_pages_info_get(seg_to_pages_info_pakage_t& result,VM_DESC& vmentry);

int enable_VMentry(VM_DESC& vmentry,bool is_pagetballoc_reserved);
//这个函数的职责是根据vmentry的内容撤销对应的页表项映射，只对对应的页表结构进行操作
//失效对应tlb项目在函数外部完成
//以及顺便使用共享信息包填充shared_inval_kspace_VMentry_info
int disable_VMentry(VM_DESC& vmentry);

int invalidate_tlb_entry();//这个函数的职责是失效对应的tlb条目,由pgs_remapped_free调用，
//disable_VMentry会把共享信息处理好，直接使用共享信息包所以不需要任何参数

/**
 * 删除对应1个pde下对应的pte项，如果检测到全部pte项被删除则回收对应的pde项
 */
int _4lv_pte_4KB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨页目录边界

int _4lv_pde_2MB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨页目录指针边界

int _4lv_pdpte_1GB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨父页表项指针边界
void enable_DEFAULT_PAT_CONFIG();
int v_to_phyaddrtraslation_entry(vaddr_t vaddr,PageTableEntryUnion& result,uint32_t&page_size);
    public:
const pgaccess PG_RW={1,1,1,0,1,WB};
const pgaccess PG_RWX ={1,1,1,1,1,WB};
const pgaccess PG_R ={1,0,1,0,1,WB};
int pgs_remapped_free(vaddr_t addr);
/**
 * 暴露在外面的接口，其中addr，size必须4k对齐
 */
void*pgs_remapp(
    phyaddr_t addr,
    uint64_t size,
    pgaccess access,
    vaddr_t vbase=0,
    bool is_protective=false,
    bool is_pagetballoc_reserved=true
);//虚拟地址为0时从下到上扫描一个虚拟地址空间映射，非0的话校验通过是内核地址则尝试固定地址映射，当然基本要求4k对齐
    int Init();
    int v_to_phyaddrtraslation(vaddr_t vaddr,phyaddr_t& result);

};
extern KernelSpacePgsMemMgr gKspacePgsMemMgr;
extern shared_inval_VMentry_info_t shared_inval_kspace_VMentry_info;