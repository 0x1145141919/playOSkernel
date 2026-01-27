#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include <util/lock.h>
#include <util/Ktemplats.h>
#include "memory/memmodule_err_definitions.h"
typedef  uint64_t phyaddr_t;
namespace MEMMODULE_LOCAIONS{
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR=8;//[8~15]是phymemspace_mgr的子模块
        namespace PHYMEMSPACE_MGR_EVENTS_CODE{
            constexpr uint8_t EVENT_CODE_INIT=0;
            constexpr uint8_t EVENT_CODE_PAGES_SET=1;
            namespace PAGES_SET_RESULTS_CODE{
                namespace FATAL_REASONS{
                    constexpr uint16_t REASON_CODE_INVALID_PAGE_SIZE=0x1;
                    constexpr uint16_t REASON_CODE_PAGES_SET_ON_NON_DRAM_SEG=0x2;
                    constexpr uint16_t REASON_CODE_PAGES_SET_CROSS_SEGMENT_BOUNDARY=0x3;
                    constexpr uint16_t REASON_CODE_PAGES_SET_ON_UNDECLARED_MEMORY=0x4;
                    constexpr uint16_t REASON_CODE_PAGES_SET_CONFLICT_WITH_EXISTING_MMIO_REGION=0x5;
                }
            }
            constexpr uint8_t EVENT_CODE_PAGES_SET_DRAM_BUDDY=1;
        }

        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_LOW1MB_MGR=9;
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_ATOM_PAGES_COMPLEX_STRUCT=10;
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST=11;
};
/**
 * 要把这个类等价于一个原子页视图，各种大小对齐的原子页
 * 2mb中页,1gb大页在state为NOT_ATOM的时候为非原子页，且is_sub_valid必然为1
 * 必然存在下级页
 * 2mbNOT_ATOM中页在4kb子表有效且存在时全部非free/mmiofree时应标记为full，
 * 1gb大页在2mb中页原子页中全部非free且非原子页全部为full时则应该标记为full，
 * */
/**
 * 由于有伙伴系统的引入，此模块存储着物理页框信息也要进行调整
 * 首先此模块维护了一个物理段链表，而伙伴系统的内存段必须是某个dram物理段的子集
 * 由是所有is_belonged_to_buddy的页必须在某个dram物理段内，
 * 由于是dram段所以不能复用
 * static int pages_state_set(phyaddr_t base,uint64_t num_of_4kbpgs,page_state_t state,pages_state_set_flags_t flags);//这个函数对大中页原子free表项的“染色”策略是染色成PARTIAL
 * 这个接口，必须另起炉灶
 * 显然，在本模块的三个基本扫描算反必须跳过这些is_belonged_to_buddy的页
 */
/**
 *整个模块有两个核心数据结构：
    static PHYSEG_LIST_ITEM*physeg_list;与
    top_1gb_table稀疏数组
    整个模块的所有内存操作必须落在physeg_list指定的一个PHYSEG内，而开始的状态是全部black_hole
    由此提供blackhole_acclaim和blackhole_decclaim两个接口来声明某个内存段
    而struct PHYSEG的type字段标记其类型有
    enum seg_type_t:uint8_t{
        DRAM_SEG,
        FIRMWARE_RESERVED_SEG,
        RESERVED_SEG,
        MMIO_SEG
    };
    只有在DRAM_SEG类型，才会允许使用pages_alloc与pages_recycle分配回收内存
    DRAM_SEG下的原子页视图中FREE算空闲，USER_FILE，USERuint8_t buddy_dram_seg:1;//仅在op为normal时有效，_ANONYMOUS，DMA，KERNEL，KERNEL_PERSIST算占用
    MMIO_SEG下原子页视图中也有空闲占用两种状态，但是MMIO_FREE为空闲，MMIO为占用
    MMIO_SEG段下提供mmio_regist注册/注销的接口，不允许重复注册，需要调用者自己保存注册了什么地址
    一次mmio_regist的地址范围必须是某个MMIO_SEG的子集
*/
/**
 * 此函数全局单例掌握物理内存一手唯一信源，如果出现不一致等情况应该直接panic,并且还要尽可能打印尽可能多的表项信息
 */
class phymemspace_mgr{
    public:
    //主要是原子页迭代器太复杂，暂时全模块加锁，后续可能的话进行优化
    //技术债
    enum page_state_t:uint8_t {
        RESERVED = 0, // 保留页,不能动,特意设计成这个数码来保证在clear之后默认就是如此
        FREE,   // 空闲页 
        NOT_ATOM,// 仅大页可用，代表子表是否存在state,refcount存在不完全相同
        FULL,
        MMIO_FREE,
        KERNEL,        // 内核使用，内核堆之类的分配
        KERNEL_PERSIST, //持久内核页，内核映像注册
        UEFI_RUNTIME,   //只能Init中注册这种类型
        ACPI_TABLES,    //只能Init中注册这种类型
        ACPI_NVS,
        USER_FILE,          // 用户进程
        USER_ANONYMOUS,// 用户进程匿名页
        DMA,           // DMA缓冲
        MMIO,
        LOW1MB
    };
    enum seg_type_t:uint8_t{
        DRAM_SEG,
        FIRMWARE_RESERVED_SEG,
        RESERVED_SEG,
        MMIO_SEG,
        LOW1MB_SEG
    };
    struct blackhole_acclaim_flags_t{
        uint64_t a;
    };
    /**
     * 此结构体为物理段的统计信息结构体，所有类型物理段total_pages字段有有效，而且为seg_size/4096
     * DRAM_SEG下free，kernel，kernel_persisit，user_file，user_anonymous，dma这几个字段是直接统计信息，used为前面几个统计出
     * MMIO_SEG下free，used这两个字段有效，
     * LOW1MB_SEG，RESERVED_SEG，FIRMWARE_RESERVED_SEG这三个只有total_pages字段有效，其它都应该为0
     */
    struct PHYSEG_statistics_t{//都是原子页视图的4kb页
        uint64_t total_pages;
        uint64_t mmio;
        uint64_t kernel;
        uint64_t kernel_persisit;
        uint64_t user_file;
        uint64_t user_anonymous;
        uint64_t dma;
    };
    struct PHYSEG{
        phyaddr_t base;
        uint64_t seg_size;
        uint64_t flags;
        seg_type_t type;
        PHYSEG_statistics_t statistics;
    };
    struct phymemmgr_statistics_t{
        uint64_t total_allocatable;
        uint64_t kernel;
        uint64_t kernel_persisit;
        uint64_t user_file;
        uint64_t user_anonymous;
        uint64_t dma;
        uint64_t total_mmio;
        uint64_t mmio_used;
        uint64_t total_firmware;
        uint64_t total_reserved;
    };
    static constexpr PHYSEG NULL_SEG={0,0,0,RESERVED_SEG};
    private:
    static KURD_t default_kurd();
    static KURD_t default_success();
    static KURD_t default_failure();
    static KURD_t default_fatal();
    static phymemmgr_statistics_t statisitcs;
    class PHYSEG_LIST_ITEM:Ktemplats::list_doubly<PHYSEG>{//保证这个类的PHYSEG不重叠，从前至后基址递增
        public:
        using Ktemplats::list_doubly<PHYSEG>::iterator;
    using Ktemplats::list_doubly<PHYSEG>::begin;
    using Ktemplats::list_doubly<PHYSEG>::end;
    using Ktemplats::list_doubly<PHYSEG>::size;
    using Ktemplats::list_doubly<PHYSEG>::empty;
        int add_seg(PHYSEG& seg);
        int del_seg(phyaddr_t base);
        int get_seg_by_base(phyaddr_t base,PHYSEG& seg);
        int get_seg_by_addr(phyaddr_t addr,PHYSEG& seg);
        bool is_seg_have_cover(phyaddr_t base,uint64_t size);
    };
    static PHYSEG_LIST_ITEM*physeg_list;
    static spinlock_cpp_t module_global_lock;
    static constexpr uint32_t _4KB_PG_SIZE = 4096; 
    static constexpr uint32_t _1GB_PG_SIZE = 1024*1024*1024;
    static constexpr uint32_t _2MB_PG_SIZE = 2*1024*1024;
    // 全局常量（类内也可）

    struct page_flags_t
    {
        page_state_t state;
        uint8_t is_sub_valid:1;//在2mb中页,1GB大页中标记地址域是否有效，是否为非1原子页
        uint8_t is_belonged_to_buddy:1;//标记该页是否是某个BCB的子区间
    };
    struct page_size4kb_t
    {
        uint32_t ref_count;
        uint32_t map_count;
        page_flags_t flags;
    };
    struct page_size2mb_t{
        page_size4kb_t* sub_pages=nullptr;//子表若有效大小必然为512项
        uint32_t ref_count;
        uint32_t map_count;
        page_flags_t flags;
    };
    struct page_size1gb_t{
        page_size2mb_t* sub2mbpages=nullptr;
        uint32_t ref_count;
        uint32_t map_count;
        page_flags_t flags;
    };
    static Ktemplats::sparse_table_2level_no_OBJCONTENT<uint32_t,page_size1gb_t,__builtin_ctz(MAX_PHYADDR_1GB_PGS_COUNT)-9,9>*top_1gb_table;
    
    struct seg_to_pages_info_package_t{
        struct pages_info_t{
            phyaddr_t base;
            uint64_t page_size_in_byte;
            uint64_t num_of_pages;
        };
        pages_info_t entries[5];//里面的地址顺序是无序的
    };
    /**
     * pages_state_set接口的标志结构体，下面讲解有效参数选择组合以及对应的行为
     * 主参数：op有acclaim_backhole，declaim_blackhole，normal类型
     *  normal类型下if_init_ref_count有效,if_mmio有效
     *  if_init_ref_count控制是否在对应原子页视图把refcount初始化为1，否则为0
     *  if_mmio控制在折叠这个操作的时候用什么鉴别占用空闲，在if_mmio语境下MMIO占用，MMIO_FREE表示空闲
     *  acclaim_backhole类型下if_init_ref_count无效，也就是refcount设置为0
     *  if_mmio的含义同上
     *  参数：declaim_blackhole类型下if_init_ref_count无效
     *  会把对应原子页视图的类型强制设置为RESERVED，如果try_fold_1gb_lambda折叠成功会在top_1gb_table中无效化项并且清零1gb表项数据 
     */
    struct pages_state_set_flags_t{
        enum optype_t:uint8_t{
            acclaim_backhole,
            declaim_blackhole,
            normal
        }op;
        struct paras_t
        {
        uint8_t if_init_ref_count:1;
        uint8_t if_mmio:1;//0为dram,1为mmio
        }params;
    };
    static int pages_state_set(phyaddr_t base,uint64_t num_of_4kbpgs,page_state_t state,pages_state_set_flags_t flags);//这个函数对大中页原子free表项的“染色”策略是染色成PARTIAL
    static int phymemseg_to_pacage(phyaddr_t base,uint64_t num_of_4kbpgs,seg_to_pages_info_package_t& pakage);
    struct dram_pages_state_set_flags_t
    {
        page_state_t state;
        enum optype_t:uint8_t{
            buddypages_regist,
            buddypages_unregist,
            normal,
        }op;
        struct paras_t
        {
        uint8_t expect_meet_atom_pages_free:1;
        uint8_t expect_meet_buddy_pages:1;
        uint8_t if_init_ref_count:1;
        }params;
    };
    
    static KURD_t dram_pages_state_set(
        const PHYSEG& current_seg,
        phyaddr_t base,
        uint64_t numof_4kbpgs,
        dram_pages_state_set_flags_t flags
    );
    static int del_no_atomig_1GB_pg(uint64_t _1idx); // 添加新的私有成员函数声明
    //比外部接口多允许UEFI_RUNTIME,ACPI_TABLES,这两个类型，最高支持到30 align_log2对齐
    static int align4kb_pages_search(
        const PHYSEG& current_seg,
        phyaddr_t&result_base,
        uint64_t numof_4kbpgs
    );  
    /**
     * 二重循环扫描1gb,2mb两个级别的表项，预估这个是最高频使用的搜索器，要做好1gbfull跳跃
     */
    static int align2mb_pages_search(
        const PHYSEG& current_seg,
        phyaddr_t&result_base,
        uint64_t numof_2mbpgs
    );
    /**
     * 1gb级搜索器，只需要扫描1gb表项
     */
    static int align1gb_pages_search(
        const PHYSEG& current_seg,
        phyaddr_t&result_base,
        uint64_t numof_1gbpgs
    );
    static int pages_recycle_verify(phyaddr_t phybase, uint64_t num_of_4kbpgs,page_state_t& state);
    class atom_page_ptr
    {
        uint32_t _1gbtb_idx;
        uint16_t _2mbtb_offestidx;
        uint16_t _4kb_offestidx;
        uint32_t page_size;
        void*page_strut_ptr;
        public:
        atom_page_ptr(
            uint32_t _1gbtb_idx,
            uint16_t _2mbtb_offestidx,
            uint16_t _4kb_offestidx
        );
        int the_next();
        friend class phymemspace_mgr;
    };
    static int _phypg_count_modify(phyaddr_t base, bool is_inc, bool is_ref_count);
    friend class atom_page_ptr;
    static void phy_to_indices(phyaddr_t p, uint64_t &idx_1gb, uint64_t &idx_2mb, uint64_t &idx_4kb);
    
    public:
    class low1mb_mgr_t{
        public:
        enum low1mb_seg_type_t:uint8_t{
            LOW1MB_TRAMPOILE_SEG,
            LOW1MB_RESERVED_SEG,
            LOW1MB_MMIO_SEG
        };
        struct low1mb_seg_t{//左闭右开区间
            uint32_t base;
            uint32_t size;
            low1mb_seg_type_t type;
        };
        private:
        static constexpr uint32_t ADDR_TOP = 1024*1024;
        class interval_LinkList:Ktemplats::list_doubly<low1mb_seg_t>{
            public:
            using Ktemplats::list_doubly<low1mb_seg_t>::iterator;
            using Ktemplats::list_doubly<low1mb_seg_t>::begin;
            using Ktemplats::list_doubly<low1mb_seg_t>::end;
            using Ktemplats::list_doubly<low1mb_seg_t>::size;
            int regist_seg(low1mb_seg_t seg);
            int del_seg(uint32_t  base);
            low1mb_seg_t get_seg_by_addr(uint32_t addr);
            bool is_seg_have_cover(phyaddr_t base,uint64_t size);
        };
        static interval_LinkList low1mb_seg_list;
        low1mb_mgr_t();
        friend class phymemspace_mgr;
        public:
        static int regist_seg(low1mb_seg_t seg);
        static int del_seg(uint32_t  base);
        static low1mb_seg_t get_seg_by_addr(uint32_t addr);//落在区间里面的地址返回对应seg副本
        static bool is_seg_have_cover(phyaddr_t base,uint64_t size);
    };
    static low1mb_mgr_t* low1mb_mgr;
    /**
     *  KERNEL,        // 内核使用，内核堆之类的分配
        USER_FILE,          // 用户进程
        USER_ANONYMOUS,// 用户进程匿名页
        DMA,    
        只有这四个类型允许参与分配
        KERNEL_PERSIST, //持久内核页，内核映像注册
        UEFI_RUNTIME,   //只能Init中注册这种类型
        ACPI_TABLES,    //只能Init中注册这种类型
        MMIO,
        RESERVED
        这5个是被注册的，指定位置注册，其中MMIO能被外部注册，剩下四个只能被模块内Init时候注册
        被
        */
    /**
     * 函数的分配逻辑是由前向后面扫的时候优先在PARTIAL/对应相同state的预分配大页中分配，而后再染色free原子大页
     * 基于那个多级表的线性扫描法，不浪费但是时间复杂度O(n)，
     */
    static phyaddr_t pages_alloc(uint64_t numof_4kbpgs,page_state_t state,uint8_t align_log2=12);
    static int pages_mmio_regist(phyaddr_t phybase,uint64_t numof_4kbpgs);//只能从标记为MMIO_FREE的内存中注册为mmio
    static int pages_recycle(phyaddr_t phybase,uint64_t numof_4kbpgs);
    static int pages_mmio_unregist(phyaddr_t phybase,uint64_t numof_4kbpgs);
    struct pages_dram_regist_flags_t{
        seg_type_t type;
        
    };
    static int pages_dram_regist(phyaddr_t phybase,uint64_t numof_4kbpgs);
    static int pages_dram_unregist(phyaddr_t phybase,uint64_t numof_4kbpgs);
    static int phypg_refcount_dec(phyaddr_t base);
    static int phypg_refcount_inc(phyaddr_t base);
    static int phypg_mapcount_dec(phyaddr_t base);
    static int phypg_mapcount_inc(phyaddr_t base);
    struct free_segs_t{
        uint64_t count;
        struct entry_t{ 
            phyaddr_t base;
            uint64_t size;
        };
        entry_t*entries;
    };
    static free_segs_t*free_segs_get();//慎用，此函数会锁住整个模块扫描整个模块的表汇报内容
    static int blackhole_acclaim(
        phyaddr_t base,
        uint64_t numof_4kbpgs,
        seg_type_t type,
        blackhole_acclaim_flags_t flags
    );
    /**
     * 显然，我们期望刚声明时的模样和刚释放时的模样一致，这样只有DRAM_SEG和MMIO_SEG才允许释放
     */
    static int blackhole_decclaim(
        phyaddr_t base        
    );
    static PHYSEG get_physeg_by_addr(phyaddr_t addr);
    static phymemmgr_statistics_t get_statisit_copy();
    /** 
     * 这个函数的工作是
     * 1.    phyaddr_t base;
    uint64_t seg_size_in_byte;
    uint64_t seg_support_4kb_page_count;//上面计算得出
    uint64_t seg_support_2mb_page_count;
    uint64_t seg_support_1gb_page_count;
    从gBaseMemMgr中获取物理内存描述符表，初始化上面这些元数据
     * 2.用标准的注册接口注册那些uefi运行时，acpi表，内核数据，内核代码，uefi内存表中明确记录的mmio空间
     * 3.对于尾巴剩余的内存，用标准接口注册为reserved
     */
    
    static int Init();
    #ifdef USER_MODE
    phymemspace_mgr();
    static int print_all_atom_table();//打印top_1gb_table下所有有效表项以及递归打印非原子表项
    static int print_allseg();//打印 static PHYSEG_LIST_ITEM*physeg_list;所有内容
    #endif
};//todo:统计量相关子系统以及公开接口，强制统计量刷新的接口