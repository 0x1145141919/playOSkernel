#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include <lock.h>
typedef  uint64_t phyaddr_t;

/*这个嵌套类是管理物理页，向后兼容支持numa,
*这个类先当作numa节点写，全局单例
*/
/**
 * 要把这个类等价于一个page_size4kb_t数组，
 * 2mb中页,1gb大页在is_sub_valid=1且state为PARTIAL或者RESERVED_MMIO的时候为非原子页，
 * 必然存在下级页
 * 2mbPARTIAL中页在4kb子表有效且存在时全部非free时应标记为full，RESERVED_MMIO中页则不进行合并操作
 * 1gb大页在2mb中页原子页中全部非free且非原子页全部为full时则应该标记为full，RESERVED_MMIO大页则不进行合并操作
 *  */
class phygpsmemmgr_t{
    public:
    spinlock_cpp_t module_global_lock;//主要是原子页迭代器太复杂，暂时全模块加锁，后续可能的话进行优化
    //技术债
    enum page_state_t:uint8_t {
        FREE = 0,      // 空闲页
        RESERVED,      // 保留页
        PARTIAL,// 仅大页可用，代表子表是否存在state,refcount存在不完全相同
        RESERVED_MMIO,//特殊类型，当一个1gb/2mb大页因为注册mmio被分解为非原子页就标记为这个
        FULL,
        KERNEL,        // 内核使用，内核堆之类的分配
        KERNEL_PERSIST, //持久内核页，内核映像注册
        UEFI_RUNTIME,   //只能Init中注册这种类型
        ACPI_TABLES,    //只能Init中注册这种类型
        USER_FILE,          // 用户进程
        USER_ANONYMOUS,// 用户进程匿名页
        DMA,           // DMA缓冲
        MMIO,
        LOW1MB_FREE,//1MB页保护,在Init逻辑中对于低1mb标记为free的内存注册为后续类型
        LOW1MB_RESERVED,
        LOW1MB_USED
    };
    private:
    static constexpr uint32_t MAX_GB_SUPPORT=4096; 
    phyaddr_t base;
    uint64_t seg_size_in_byte;
    uint64_t seg_support_4kb_page_count;//上面计算得出
    uint64_t seg_support_2mb_page_count;
    uint64_t seg_support_1gb_page_count;
    static constexpr uint32_t _4KB_PG_SIZE = 4096; 
    static constexpr uint32_t _1GB_PG_SIZE = 1024*1024*1024;
    static constexpr uint32_t _2MB_PG_SIZE = 2*1024*1024;
    // 全局常量（类内也可）

    struct page_flags_t
    {
        page_state_t state;
        uint8_t is_sub_valid:1;//在2mb中页,1GB大页中标记地址域是否有效，是否为非1原子页
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
    page_size1gb_t top_1gb_table[MAX_GB_SUPPORT];
    struct seg_to_pages_info_pakage_t{
        struct pages_info_t{
            phyaddr_t base;
            uint64_t page_size_in_byte;
            uint64_t num_of_pages;
        };
        pages_info_t entryies[5];//里面的地址顺序是无序的
    };
    int _4kb_pages_state_set(
        uint64_t entry4kb_base_idx,
        uint64_t num_of_4kbpgs,
        page_state_t state,
        page_size4kb_t*base_entry,
        bool if_inc=false
    );
    int _2mb_pages_state_set(
        uint64_t entry2mb_base_idx,
        uint64_t num_of_2mbpgs,
        page_state_t state,
        page_size2mb_t*base_entry,
        bool if_inc=false
    );
    int _1gb_pages_state_set(
        uint64_t entry_base_idx,
        uint64_t num_of_1gbpgs,
        page_state_t state,
        bool if_inc=false
    );
    int pages_state_set(phyaddr_t base,uint64_t num_of_4kbpgs,page_state_t state,bool if_inc=false);//这个函数对大中页原子free表项的“染色”策略是染色成PARTIAL
    int phymemseg_to_pacage(phyaddr_t base,uint64_t num_of_4kbpgs,seg_to_pages_info_pakage_t& pakage);
    int ensure_1gb_subtable(uint64_t idx_1gb);
    int ensure_2mb_subtable(page_size2mb_t& p2);
    int try_fold_2mb(page_size2mb_t& p2);//返回0失败返回1成功
    int try_fold_1gb(page_size1gb_t& p1);


    int inner_pages_alloc(phyaddr_t &result_base,uint64_t num_of_4kbpgs,page_state_t state,uint8_t align_log2=12);
    //比外部接口多允许UEFI_RUNTIME,ACPI_TABLES,这两个类型，最高支持到30 align_log2对齐

    /**
     * 这个4kb搜索器只考虑用atom_page_ptr进行扫描，毕竟这是最小粒度的对齐
     */
    int align4kb_pages_search(
        phyaddr_t&result_base,
        uint64_t numof_4kbpgs
    );  
    /**
     * 二重循环扫描1gb,2mb两个级别的表项，预估这个是最高频使用的搜索器，要做好1gbfull跳跃
     */
    int align2mb_pages_search(
        phyaddr_t&result_base,
        uint64_t numof_2mbpgs
    );
    /**
     * 1gb级搜索器，只需要扫描1gb表项
     */
    int align1gb_pages_search(
        phyaddr_t&result_base,
        uint64_t numof_1gbpgs
    );
    int pages_recycle_verify(phyaddr_t phybase, uint64_t num_of_4kbpgs);
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
        friend class phygpsmemmgr_t;
    };
    friend class atom_page_ptr;
    int compare_atom_page(atom_page_ptr ptr1,atom_page_ptr ptr2);//ptr1<ptr2返回-1,等于返回0大于返回1
    void phy_to_indices(phyaddr_t p, uint64_t &idx_1gb, uint64_t &idx_2mb, uint64_t &idx_4kb);
    int scan_reserved_contiguous(phyaddr_t phybase, uint64_t max_pages, uint64_t &out_reserved_count);
    int inner_mmio_regist(phyaddr_t phybase,uint64_t numof_4kbpgs);
    public:
    phy_memDescriptor* queryPhysicalMemoryUsage(phyaddr_t base,uint64_t len_in_bytes);
    phy_memDescriptor* getPhyMemoryspace();
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
     */
    phyaddr_t pages_alloc(uint64_t numof_4kbpgs,page_state_t state,uint8_t align_log2=12);
    /**
     * 暴露在外部的mmio注册接口，地址必须4k对齐，并且对应的原子页必须是RESERVED,LOW1MB_RESERVED两种类型
     */
    int pages_mmio_regist(phyaddr_t phybase,uint64_t numof_4kbpgs);//只能从标记为reserved的内存中注册为mmio
    int pages_recycle(phyaddr_t phybase,uint64_t numof_4kbpgs);
    int phypg_refcount_dec(phyaddr_t base);
    int phypg_refcount_inc(phyaddr_t base);
    int phypg_mapcount_dec(phyaddr_t base);
    int phypg_mapcount_inc(phyaddr_t base);
    /** 
     * 这个函数的工作是
     * 1.    phyaddr_t base;
    uint64_t seg_size_in_byte;
    uint64_t seg_support_4kb_page_count;//上面计算得出
    uint64_t seg_support_2mb_page_count;
    uint64_t seg_support_1gb_page_count;
    从gBaseMemMgr中获取物理内存描述符表，初始化上面这些元数据
    2.用标准的注册接口注册那些uefi运行时，acpi表，内核数据，内核代码，uefi内存表中明确记录的mmio空间
    3.对于尾巴剩余的内存，用标准接口注册为reserved
    */
    
    int Init();
    
};//todo:统计量相关子系统以及公开接口，强制统计量刷新的接口
extern phygpsmemmgr_t gPhyPgsMemMgr;