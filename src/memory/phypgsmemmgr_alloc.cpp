#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "util/kout.h"
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144

// 添加辅助结构体和函数
struct phyaddr_in_idx_t
{
    uint64_t _1gb_idx=0;
    uint16_t _2mb_idx=0;
    uint16_t _4kb_idx=0;
    
    // 添加默认构造函数
    phyaddr_in_idx_t() : _1gb_idx(0), _2mb_idx(0), _4kb_idx(0) {}
};





KURD_t phymemspace_mgr::del_no_atomig_1GB_pg(uint64_t _1idx)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_DEL_NO_ATOM_1GB_PG;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_DEL_NO_ATOM_1GB_PG;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_DEL_NO_ATOM_1GB_PG;

    page_size1gb_t*pg=top_1gb_table->get(_1idx);
    if(pg==nullptr){
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::DEL_NO_ATOM_1GB_PG_RESULTS_CODE::FAIL_REASONS::_1GB_PG_NOT_EXIST;
        return fail;
    }
    if(pg->sub2mbpages==nullptr){
        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::DEL_NO_ATOM_1GB_PG_RESULTS_CODE::FATAL_REASONS::SUBTABLE_NOT_EXIST;
        return fatal;
    }
    page_size2mb_t* _2base=pg->sub2mbpages;
    for(uint16_t i=0;i<512;i++){
        page_size2mb_t*_2=_2base+i;
        if(_2->flags.is_sub_valid){
            if(_2->sub_pages==nullptr){
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::DEL_NO_ATOM_1GB_PG_RESULTS_CODE::FATAL_REASONS::SUBTABLE_NOT_EXIST;
                return fatal;
            }
            page_size4kb_t*_4base=_2->sub_pages;
            for(uint16_t j=0;j<512;j++){
                page_size4kb_t*_4=_4base+j;
                if(_4->flags.state!=RESERVED){
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::DEL_NO_ATOM_1GB_PG_RESULTS_CODE::FATAL_REASONS::PAGES_ILLEAGLE_STATE;
                    return fatal;//按照文档设计这里必须是reserved才合法，不然应该回炉重造
                }
            }
            delete[] _4base;
        }
        else  if(_2->flags.state!=RESERVED){
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::DEL_NO_ATOM_1GB_PG_RESULTS_CODE::FATAL_REASONS::PAGES_ILLEAGLE_STATE;
            return fatal;
        }
    }
    delete[] _2base;
    top_1gb_table->release(_1idx);
    return success;
}
static inline phyaddr_t min(phyaddr_t a, phyaddr_t b) {
    return (a < b) ? a : b;
}

static inline phyaddr_t max(phyaddr_t a, phyaddr_t b) {
    return (a > b) ? a : b;
}
int phymemspace_mgr::phymemseg_to_pacage(
    phyaddr_t base,
    uint64_t num_of_4kbpgs,
    seg_to_pages_info_package_t& pak)
{
    // 清空
    for (int i = 0; i < 5; i++) {
        pak.entries[i].base = 0;
        pak.entries[i].num_of_pages = 0;
        pak.entries[i].page_size_in_byte = 0;
    }

    uint64_t start = base;
    uint64_t end   = base + num_of_4kbpgs * _4KB_PG_SIZE;
    phyaddr_t start_up_2mb = align_up(start, _2MB_PG_SIZE);
    phyaddr_t end_down_2mb = align_down(end, _2MB_PG_SIZE);
    
    phyaddr_t start_up_1gb = align_up(start, _1GB_PG_SIZE);
    phyaddr_t end_down_1gb = align_down(end, _1GB_PG_SIZE);

    bool is_cross_2mb_boud = false;
    bool is_cross_1gb_boud = false;
    
    if (start_up_2mb <= end_down_2mb) {
        is_cross_2mb_boud = true;
        if (start_up_1gb <= end_down_1gb) {
            is_cross_1gb_boud = true;
        } else {
            is_cross_1gb_boud = false;
        }
    } else {
        is_cross_1gb_boud = false;
        is_cross_2mb_boud = false;
    }
    
    if(is_cross_2mb_boud){
        if(is_cross_1gb_boud){
            uint64_t countmid1gb=(end_down_1gb - start_up_1gb)/_1GB_PG_SIZE;
            pak.entries[0].base = start_up_1gb;
            pak.entries[0].num_of_pages = countmid1gb;
            pak.entries[0].page_size_in_byte = _1GB_PG_SIZE;
            uint64_t count_down_2mb=(start_up_1gb - start_up_2mb)/_2MB_PG_SIZE;
            pak.entries[1].base = start_up_2mb;
            pak.entries[1].num_of_pages = count_down_2mb;
            pak.entries[1].page_size_in_byte = _2MB_PG_SIZE;
            uint64_t count_up_2mb=(end_down_2mb - end_down_1gb)/_2MB_PG_SIZE;
            pak.entries[2].base = end_down_1gb;
            pak.entries[2].num_of_pages = count_up_2mb;
            pak.entries[2].page_size_in_byte = _2MB_PG_SIZE; 
        }else{
            
            uint64_t count_2mb=(end_down_2mb - start_up_2mb)/_2MB_PG_SIZE;
            pak.entries[2].base = start_up_2mb;
            pak.entries[2].num_of_pages = count_2mb;
            pak.entries[2].page_size_in_byte = _2MB_PG_SIZE; 
        }
        uint64_t countdown4kb=(start_up_2mb-start)/_4KB_PG_SIZE;
        pak.entries[3].base = start;
        pak.entries[3].num_of_pages = countdown4kb;
        pak.entries[3].page_size_in_byte = _4KB_PG_SIZE;
        uint64_t countup4kb=(end-end_down_2mb)/_4KB_PG_SIZE;
        pak.entries[4].base = end_down_2mb;
        pak.entries[4].num_of_pages = countup4kb;
        pak.entries[4].page_size_in_byte = _4KB_PG_SIZE;
    }else{
        uint64_t count4kbpgs=(end-start)/_4KB_PG_SIZE;
        pak.entries[4].base = start;
        pak.entries[4].num_of_pages = count4kbpgs;
        pak.entries[4].page_size_in_byte = _4KB_PG_SIZE;
    }
    
    return OS_SUCCESS;
}
phyaddr_t phymemspace_mgr::pages_linear_scan_and_alloc(uint64_t numof_4kbpgs,KURD_t&kurd, phymemspace_mgr::page_state_t state, uint8_t align_log2)
{
    if(state==KERNEL||state==USER_ANONYMOUS||state==USER_FILE||state==DMA)
    {
        module_global_lock.lock();
        
        if (numof_4kbpgs == 0) {
            module_global_lock.unlock();
            return 0;
        }
        if(align_log2 > 30) {
            module_global_lock.unlock();
            return 0;
        }
        
        // 自动向上取最近对齐粒度
        uint8_t a = [align_log2]()->uint8_t{
            if (align_log2 <= 12) return 12;          // 最小对齐是 4KB
            if (align_log2 <= 21) return 21;         // 12~20 → 上调到 2MB
            if (align_log2 <= 30) return 30;         // 21~29 → 上调到 1GB

            // 30以上按1GB处理（你的要求）
            return 30;
        }();
        
        phyaddr_t result = 0;
        KURD_t status = KURD_t();
        auto it = physeg_list->begin();
        PHYSEG current_seg = *it;
        do{
            
            if(current_seg.type == DRAM_SEG) {
                if(a == 12) {
                    status = align4kb_pages_search(current_seg, result, numof_4kbpgs);
                    if(status.result==result_code::SUCCESS) goto search_success;
                } else if (a == 21) {
                    status = align2mb_pages_search(current_seg, result, align_up(numof_4kbpgs, PAGES_4KB_PER_2MB) / PAGES_4KB_PER_2MB);
                    if(status.result==result_code::SUCCESS) goto search_success;
                } else if(a == 30) {
                    uint64_t num_of_1gbpgs = align_up(numof_4kbpgs, PAGES_4KB_PER_1GB) / PAGES_4KB_PER_1GB;
                    status = align1gb_pages_search(current_seg, result, num_of_1gbpgs);
                    if(status.result==result_code::SUCCESS) goto search_success;
                } else {
                    module_global_lock.unlock();
                    return 0;
                }
                
            }++it;
            current_seg = *it;
            
        }while(it != physeg_list->end());
        kurd=status;
        // 未找到合适的内存区域
        module_global_lock.unlock();
        return 0;
        
        search_success:
        pages_state_set_flags_t flag = {
            .op=pages_state_set_flags_t::normal,
            .params={
                .if_init_ref_count=1,
                .if_mmio=0
            }};
        status = pages_state_set(result, numof_4kbpgs, state, flag);
        switch(state){
            case KERNEL:{
                current_seg.statistics.kernel+=numof_4kbpgs;
                statisitcs.kernel+=numof_4kbpgs;break;
            }
            case USER_FILE:{
                current_seg.statistics.user_file+=numof_4kbpgs;
                statisitcs.user_file+=numof_4kbpgs;break;}
            case USER_ANONYMOUS:{
                current_seg.statistics.user_anonymous+=numof_4kbpgs;
                statisitcs.user_anonymous+=numof_4kbpgs;break;}
            case DMA:{
                current_seg.statistics.dma+=numof_4kbpgs;
                statisitcs.dma+=numof_4kbpgs;break;
            }
            default:{
                //panic
            }
        }
        kurd=status;
        module_global_lock.unlock();
        if(status.result==result_code::SUCCESS) return result;
        return 0;
    }    
    kurd=default_failure();
    kurd.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_LINEAR_SCAN_AND_ALLOC;
    kurd=set_result_fail_and_error_level(kurd);
    kurd.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_LINEAR_SCAN_AND_ALLOC_RESULTS_CODE::FAIL_REASONS::REASON_CODE_INVALID_STATE;
    return 0;
}

KURD_t phymemspace_mgr::pages_mmio_regist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail_result=default_failure();
    fail_result.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_MMIO_REGIST;
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=MMIO_SEG){
        fail_result.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::MMIO_REGIST_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MMIOSEG_NOT_EXIST;
        module_global_lock.unlock();
        return fail_result;
    }
    pages_state_set_flags_t flags={
        .op=pages_state_set_flags_t::normal,
        .params={
            .if_init_ref_count=1,
            .if_mmio=1
        }
    };
    
    KURD_t status=pages_state_set(phybase, numof_4kbpgs, MMIO, flags);  
    module_global_lock.unlock();
    return status;
}

static inline uint8_t normalize_align_log2(uint8_t log2)
{
    if (log2 < 12) return 12;          // 最小对齐是 4KB
    if (log2 <= 20) return 21;         // 12~20 → 上调到 2MB
    if (log2 <= 29) return 30;         // 21~29 → 上调到 1GB

    // 30以上按1GB处理（你的要求）
    return 30;
}
phymemspace_mgr::PHYSEG phymemspace_mgr::get_physeg_by_addr(phyaddr_t addr)
{
    PHYSEG result=NULL_SEG;
    physeg_list->get_seg_by_addr(addr,result);
    return result;
}
KURD_t phymemspace_mgr::blackhole_acclaim(phyaddr_t base, uint64_t numof_4kbpgs, seg_type_t type, blackhole_acclaim_flags_t flags)
{
    KURD_t fail=default_failure();
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_BLACK_HOLE_ACCLAIM;
    if(base%4096!=0){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::BLACK_HOLE_ACCLAIM_RESULTS_CODE::FAIL_REASONS::BASE_NOT_ALIGNED;
        return fail;
    }
    PHYSEG newseg={.base=base, .seg_size=numof_4kbpgs*4096,.flags=0,.type=type, };
    module_global_lock.lock();
    KURD_t status=physeg_list->add_seg(newseg);
    if(status.event_code!=result_code::SUCCESS)
    {
        module_global_lock.unlock();
        return status;
    }
    bool is_mmio=(type==MMIO_SEG);
    pages_state_set_flags_t flag_set_page={
        .op=pages_state_set_flags_t::acclaim_backhole,
        .params={
            .if_init_ref_count=0,
            .if_mmio=is_mmio
        }
    };
    page_state_t to_fill_default_state=[type,numof_4kbpgs]()->page_state_t{
        switch (type)
        {
            case DRAM_SEG: {
                statisitcs.total_allocatable+=numof_4kbpgs;
                return FREE;
            }
            case MMIO_SEG: {
                statisitcs.total_mmio+=numof_4kbpgs;
                return MMIO_FREE;
            }
            case LOW1MB_SEG: {
                return LOW1MB;
            }
            case FIRMWARE_RESERVED_SEG:{
                statisitcs.total_firmware+=numof_4kbpgs;
                return RESERVED;
            }
            default: {
                statisitcs.total_reserved+=numof_4kbpgs;
                return RESERVED;
            }
        }
    }();
    status=pages_state_set(
        base,
        numof_4kbpgs,
        to_fill_default_state,
        flag_set_page
    );
    module_global_lock.unlock();
    return status;
}
KURD_t phymemspace_mgr::blackhole_decclaim(phyaddr_t base)
{
    KURD_t fail=default_failure();
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_BLACK_HOLE_DECCLAIM;
    if(base%4096!=0){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::BLACK_HOLE_DECCLAIM_RESULTS_CODE::FAIL_REASONS::BASE_NOT_ALIGNED;
        return fail;
    }
    module_global_lock.lock();
    PHYSEG seg;
    KURD_t status=physeg_list->get_seg_by_base(base,seg);
    if(status.result!=result_code::SUCCESS)
    {
        module_global_lock.unlock();
        return status;
    }
    
    if(seg.type!=DRAM_SEG&&seg.type!=MMIO_SEG){
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::BLACK_HOLE_DECCLAIM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NOT_FOUND;
        return fail;
    }
    bool is_mmio=(seg.type==MMIO_SEG);
    if(is_mmio)statisitcs.total_mmio-=seg.statistics.total_pages;
    else statisitcs.total_allocatable-=seg.statistics.total_pages;  
    pages_state_set_flags_t flag_set_page={
        .op=pages_state_set_flags_t::declaim_blackhole,//撤销成黑洞，但是非1GB原子页不能处理
        .params={
            .if_init_ref_count=0,
            .if_mmio=is_mmio
        }
    };
    status=pages_state_set(base,seg.seg_size,RESERVED,flag_set_page);//这个函数里面负责撤销1GB原子页和其它级别的原子页
    if(status.result!=result_code::SUCCESS){
        module_global_lock.unlock();
        return status;
    }
    status = physeg_list->del_seg(base);
    if(status.result!=result_code::SUCCESS) {
        module_global_lock.unlock();
        return status;
    }

    // 检查并删除可能悬空的1GB页表项
    phyaddr_t end = base + seg.seg_size;
    phyaddr_t align_down_base = align_down(base, _1GB_PG_SIZE);
    phyaddr_t align_up_end = align_up(end, _1GB_PG_SIZE);
    if((align_up_end-align_down_base)==_1GB_PG_SIZE){
        if(!physeg_list->is_seg_have_cover(align_down_base,_1GB_PG_SIZE)){
            status=del_no_atomig_1GB_pg(align_down_base/_1GB_PG_SIZE);
            if(status.result!=result_code::SUCCESS)
            {
                kio::bsp_kout<<"[KPHYGPSMEMMGR] del_no_atomig_1GB_pg fail"<<kio::kendl;
                module_global_lock.unlock();
                return status;
            }
        }
    }else{
        if(!physeg_list->is_seg_have_cover(align_down_base,_1GB_PG_SIZE)){
            status=del_no_atomig_1GB_pg(align_down_base/_1GB_PG_SIZE);
            if(status.result!=result_code::SUCCESS)
            {
                kio::bsp_kout<<"[KPHYGPSMEMMGR] del_no_atomig_1GB_pg fail"<<kio::kendl;
                module_global_lock.unlock();
                return status;
            }
        }
        if(!physeg_list->is_seg_have_cover(align_up_end-_1GB_PG_SIZE,_1GB_PG_SIZE))
        {
            status=del_no_atomig_1GB_pg((align_up_end-_1GB_PG_SIZE)/_1GB_PG_SIZE);
            if(status.result!=result_code::SUCCESS)
            {
                kio::bsp_kout<<"[KPHYGPSMEMMGR] del_no_atomig_1GB_pg fail"<<kio::kendl;
                module_global_lock.unlock();
                return status;
            }
        }
    }
    module_global_lock.unlock();
    return status;
}
