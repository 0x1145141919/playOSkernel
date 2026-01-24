#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include "memory/pgtable45.h"
#include <util/lock.h>
#include "memmodule_err_definitions.h"
#include "util/RB_btree.h"
namespace PAGE_TBALE_LV{
    constexpr bool LV_4=true;
    constexpr bool LV_5=false;
}
constexpr uint16_t KERNEL_SPACE_PCID=0;
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
            phyaddr_t phybase;
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
    uint64_t SEG_SIZE_ONLY_UES_IN_BASIC_SEG;//只有BASIC_SEG才可用此字段，且非0，非BASIC_SEG此字段必须为0
    enum map_type_t : uint8_t {
        MAP_NONE = 0,     // 未分配物理页（仅占位）
        MAP_PHYSICAL,     // 连续物理页,只有内核因为立即要求而使用，用户空间不能用
        MAP_FILE,         // 文件映射
        MAP_ANON,          // 匿名映射（默认用户空间）
    } map_type;
    phyaddr_t phys_start;  // 当 map_type=MAP_PHYSICAL 时有效
                           // MAP_NONE 没有意义
    pgaccess access;       // 页权限/缓存策略
    uint8_t committed_full:1;   // 物理页是否完全已经分配（lazy allocation 用）
    uint8_t is_vaddr_alloced:1;    // 虚拟地址是否由地址空间管理器分配（否则为固定映射）
    uint8_t is_out_bound_protective:1; // 是否有越界保护区,只有is_vaddr_alloced为1的bit此位才有意义，
};
int VM_vaddr_cmp(VM_DESC* a,VM_DESC* b);
namespace MEMMODULE_LOCAIONS{
    namespace ADDRESSPACE_EVENTS{
        constexpr uint8_t EVENT_CODE_INIT=0x0;
        constexpr uint8_t EVENT_CODE_ENABLE_VMENTRY=0x1;
        constexpr uint8_t EVENT_CODE_DISABLE_VMENTRY=0x2;
        constexpr uint8_t EVENT_CODE_TRAN_TO_PHY=0x3;
        constexpr uint8_t EVENT_CODE_PAGES_SET=0x4;
        constexpr uint8_t EVENT_CODE_UNREGIST=0xff;
        namespace ENABLE_VMENTRY_RESULTS{
            namespace FAIL_REASONS{
                constexpr uint16_t REASON_CODE_INVALID_PAGETABLE_ENTRY=0x1;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY_TRY_TO_MAP_LOW_MEM_WHO_NOT_gKernelSpace=0x2;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY=0x3;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY_CANT_SPLIT=0x4;
                constexpr uint16_t REASON_CODE_NOT_SUPPORT_LV5_PAGING=0x100;
                    
                }
            namespace FATAL_REASONS{
                    constexpr uint16_t REASON_CODE_TRY_TO_GET_SUB_PAGE_IN_ATOM_PAGE=0x1;//AddressSpace的设计的能力有限，
                    //只能根据喂进来的VM_DESC指定物理地址虚拟地址建立页表结构，
                    //但是能够对已经确定的大页试图向下建立小页报错，这是种严重错误，
                    //但是诱因可能是外部调用错误，地址段覆盖，所以这个不会内部panic,
                    //但是调用者必须有意识，出现这个错误码可以考虑重开
                    constexpr uint16_t REASON_CODE_PAGES_SET_FALT=0x2;
                    constexpr uint16_t REASON_CODE_INVALID_PAGE_SIZE=0x4;
                }            
        }
        namespace TRAN_TO_PHY_RESULTS_CODE{
            namespace FAIL_REASONS{
                constexpr uint16_t REASON_CODE_NOT_ALLOW_KSPACE_VA=0x1;
                constexpr uint16_t REASON_CODE_NOT_PRESENT_ENTRY=0x2;  
                constexpr uint16_t REASON_CODE_NOT_SUPPORT_LV5_PAGING=0x100;   
                }
        }
        namespace DISABLE_VMENTRY_RESULTS{
            namespace FAIL_REASONS{
                constexpr uint16_t REASON_CODE_INVALID_PAGETABLE_ENTRY=0x1;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY_TRY_TO_MAP_LOW_MEM_WHO_NOT_gKernelSpace=0x2;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY=0x3;
                constexpr uint16_t REASON_CODE_BAD_VMENTRY_CANT_SPLIT=0x4;
                constexpr uint16_t REASON_CODE_NOT_SUPPORT_LV5_PAGING=0x100;
                    
                }
            namespace FATAL_REASONS{
                    constexpr uint16_t REASON_CODE_TRY_TO_GET_SUB_PAGE_IN_ATOM_PAGE=0x1;//AddressSpace的设计的能力有限，
                    //只能根据喂进来的VM_DESC指定物理地址虚拟地址建立页表结构，
                    //但是能够对已经确定的大页试图向下建立小页报错，这是种严重错误，
                    //但是诱因可能是外部调用错误，地址段覆盖，所以这个不会内部panic,
                    //但是调用者必须有意识，出现这个错误码可以考虑重开
                    constexpr uint16_t REASON_CODE_PAGES_SET_FALT=0x2;
                    constexpr uint16_t REASON_CODE_INVALID_PAGE_SIZE=0x4;
                    constexpr uint16_t REASON_CODE_TRY_TO_GET_SUB_PAGE_OF_NOT_PRESENT_PAGE=0x5;
                    constexpr uint16_t REASON_CODE_TRY_TO_CLEAR_UNPRESENT_PAGE=0x6;
                    constexpr uint16_t REASON_CODE_CONSISTENCY_VIOLATION_WHEN_CLEAR_PAGE_TABLE_ENTRY=0x7;
                    constexpr uint16_t REASON_CODE_OTHER_PAGES_SET_FATAL=0x8;
                }   
        }
        
    }
    constexpr uint8_t LOCATION_CODE_KSPACE_MAP_MGR_VMENTRY_RBTREE=17;
    namespace KSPACE_MAP_MGR_EVENTS_CODE{

    }
    constexpr uint8_t LOCATION_CODE_KSPACE_MAP_MGR_PGS_PAGE_TABLE=18;
    namespace KSPACE_MAP_MGR_PGS_PAGE_TABLE_EVENTS_CODE{

    }
};
/**
 * 此类的职责就是创建虚拟地址空间，管理虚拟地址空间，
 * 此类的职责有且仅一个功能，就是管理相应的低一般虚拟地址空间，
 * 本类接口不接受低于64k的虚拟地址空间
 * 此类的职责只有映射虚拟地址空间，不提供虚拟地址空间管理功能
 * 若使用此类请自行实现虚拟地址空间的管理
 * 最多提供一个打印实际映射表的接口
 */
class AddressSpace//到时候进程管理器可以用这个类创建，但是内核空间还是受内核空间管理器管理
{   
    private:
    phyaddr_t pml4_phybase;
    uint64_t occupyied_size;
    constexpr static uint64_t PAGE_LV4_USERSPACE_SIZE=0x00007FFFFFFFFFFF+1;
    constexpr static uint64_t PAGE_LV5_USERSPACE_SIZE=0x00FFFFFFFFFFFFFF+1;
    static constexpr uint32_t _4KB_SIZE=0x1000;
    static constexpr uint32_t _2MB_SIZE=1ULL<<21;
    static constexpr uint32_t _1GB_SIZE=1ULL<<30;
    spinrwlock_cpp_t lock;
    KURD_t   default_kurd();
    KURD_t   default_success();
    KURD_t   default_fail();
    KURD_t   default_fatal();
    int seg_to_pages_info_get(seg_to_pages_info_pakage_t& result,VM_DESC desc);
    public:
    AddressSpace();
    KURD_t enable_VM_desc(VM_DESC desc);
    KURD_t disable_VM_desc(VM_DESC desc);
    struct tlb_invalidate_flags{
        uint64_t hardware_addresspace_id;
        uint16_t if_not_currunt_space:1;
        uint16_t if_hardware_addresspace_id_valid:1;
    };
    static KURD_t invalidate_tlb_of_VM_desc(VM_DESC desc,tlb_invalidate_flags flags);
    KURD_t second_stage_init();//new完之后马上最快的速度调用此接口，并且接受返回值进行分析
    KURD_t build_identity_map_ONLY_IN_gKERNELSPACE();//uefi运行时服务依赖于这个构建的恒等映射
    uint64_t get_occupyied_size(){
        return occupyied_size;
    }
    phyaddr_t vaddr_to_paddr(vaddr_t vaddr,KURD_t&kurd);
    void unsafe_load_pml4_to_cr3(uint16_t pcid);//这个接口会直接把当前页表加载到cr3寄存器
    ~AddressSpace();//如果cr3还装载这这个页表，删除会在堆里释放根表，虽然不会马上报错但是极度危险，最好别这么干
};
extern AddressSpace*gKernelSpace;
constexpr ia32_pat_t DEFAULT_PAT_CONFIG={
    .value=0x0407050600070106
};
cache_table_idx_struct_t cache_strategy_to_idx(cache_strategy_t cache_strategy);
/**
 * 这个类的职责有且仅有一个功能，就是管理内核空间，
 * 通过kspacePML4暴露给AddressSpace::sharing_kernel_space()强制复制高一半pml4e
 * 以及类内的kspaceUPpdpt同步所有进程空间的内核结构
 * 此类全局唯一，只管理高128tb虚拟地址空间
 */
class KspaceMapMgr//使用上面的位域结构体，在初始化函数中直接用，但在后续正式外部暴露接口中对页表项必须用原子操作函数
{
static constexpr PageTableEntryUnion high_half_template={
    .pml4={
        .present=1,
        .RWbit=1,
        .KERNELbit=0,
        .PWT=0,
        .PCD=0,
        .accessed=0,
        .reserved=0,
        .pdpte_addr=0,
        .reserved2=0,
        .EXECUTE_DENY=0
    }
};
private://后面五级页表的时候考虑选择编译
friend AddressSpace;
static PageTableEntryUnion kspaceUPpdpt[256*512];
static phyaddr_t kspace_uppdpt_phyaddr;
static constexpr uint64_t PAGELV4_KSPACE_BASE=0xFFFF800000000000;
static constexpr uint64_t PAGELV5_KSPACE_BASE=0xFF00000000000000;
static constexpr uint64_t PAGELV4_KSPACE_SIZE=1ULL<<(48-1);
static constexpr uint64_t PAGELV5_KSPACE_SIZE=1ULL<<(57-1);

static constexpr uint32_t _4KB_SIZE=0x1000;
static constexpr uint32_t _2MB_SIZE=1ULL<<21;
static constexpr uint32_t _1GB_SIZE=1ULL<<30;

static bool is_default_pat_config_enabled;
static bool is_phypgsmgr_enabled;//这个位影响页框管理的
/**
 * is_phypgsmgr_enabled为false时用堆分配页框
 * 为true时用物理页框管理分配页框
 */
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
static kspace_vm_table_t*kspace_vm_table;
static spinlock_cpp_t GMlock;
friend AddressSpace::AddressSpace();
/**
 * 往kspace_vm_table插入vmentry，
 * 遵守虚拟地址从小到大排序，
 * 内部接口默认外部调用函数持有锁，
 * 不对参数合法性进行校验
 */
static int VM_add(VM_DESC vmentry);
/**
 * 往kspace_vm_table删除虚拟地址为vaddr的VM_DESC项，
 * 遵守虚拟地址从小到大排序，
 * 内部接口默认外部调用函数持有锁
 */
static int VM_del(VM_DESC*entry);
/**
 * 搜索虚拟地址为vaddr的VM_DESC项，
 * 建议采取二分查找
 */
static int VM_search_by_vaddr(vaddr_t vaddr,VM_DESC&result);

static int _4lv_pdpte_1GB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨父页表项指针边界

static int _4lv_pde_2MB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页目录指针边界

static int _4lv_pte_4KB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页目录边界

static void invalidate_seg();

/**
 * 
 */
static int seg_to_pages_info_get(seg_to_pages_info_pakage_t& result,VM_DESC vmentry);

static int enable_VMentry(VM_DESC& vmentry);
//这个函数的职责是根据vmentry的内容撤销对应的页表项映射，只对对应的页表结构进行操作
//失效对应tlb项目在函数外部完成
//以及顺便使用共享信息包填充shared_inval_kspace_VMentry_info
static int disable_VMentry(VM_DESC& vmentry);

static int invalidate_tlb_entry();//这个函数的职责是失效对应的tlb条目,由pgs_remapped_free调用，
//disable_VMentry会把共享信息处理好，直接使用共享信息包所以不需要任何参数

/**
 * 删除对应1个pde下对应的pte项，如果检测到全部pte项被删除则回收对应的pde项
 */
static int _4lv_pte_4KB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨页目录边界

static int _4lv_pde_2MB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨页目录指针边界

static int _4lv_pdpte_1GB_entries_clear(vaddr_t vaddr_base,uint16_t count);//这里要求的是不能跨父页表项指针边界
static void enable_DEFAULT_PAT_CONFIG();
static int v_to_phyaddrtraslation_entry(vaddr_t vaddr,PageTableEntryUnion& result,uint32_t&page_size);
    public:
static constexpr pgaccess PG_RW={1,1,1,0,1,WB};
static constexpr pgaccess PG_RWX ={1,1,1,1,1,WB};
static constexpr pgaccess PG_R ={1,0,1,0,1,WB};
static int pgs_remapped_free(vaddr_t addr);
/**
 * 暴露在外面的接口，其中addr，size必须4k对齐
 */
static void*pgs_remapp(
    phyaddr_t addr,
    uint64_t size,
    pgaccess access,
    vaddr_t vbase=0,
    bool is_protective=false
);//虚拟地址为0时从下到上扫描一个虚拟地址空间映射，非0的话校验通过是内核地址则尝试固定地址映射，当然基本要求4k对齐
static int Init();
static int v_to_phyaddrtraslation(vaddr_t vaddr,phyaddr_t& result);

};
extern shared_inval_VMentry_info_t shared_inval_kspace_VMentry_info;