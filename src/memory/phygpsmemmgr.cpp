#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
int phygpsmemmgr_t::compare_atom_page(atom_page_ptr ptr1, atom_page_ptr ptr2)
{
    // compare 1GB index
    if (ptr1._1gbtb_idx < ptr2._1gbtb_idx) return -1;
    if (ptr1._1gbtb_idx > ptr2._1gbtb_idx) return +1;

    // compare 2MB index
    if (ptr1._2mbtb_offestidx < ptr2._2mbtb_offestidx) return -1;
    if (ptr1._2mbtb_offestidx > ptr2._2mbtb_offestidx) return +1;

    // compare 4KB index
    if (ptr1._4kb_offestidx < ptr2._4kb_offestidx) return -1;
    if (ptr1._4kb_offestidx > ptr2._4kb_offestidx) return +1;
    return 0;
}
void phygpsmemmgr_t::phy_to_indices(phyaddr_t p, uint64_t &idx_1gb, uint64_t &idx_2mb, uint64_t &idx_4kb)
{//建议修改为匿名函数
    int64_t off = p - base; // base 是 segment base
    idx_1gb = (off>>30)&511;
    idx_2mb = (off>>21)&511;
    idx_4kb = (off>>12)&511;
}

int phygpsmemmgr_t::scan_reserved_contiguous(phyaddr_t phybase, uint64_t max_pages, uint64_t &out_reserved_count)
{
     // 参数校验（不改全局状态）
    if (max_pages == 0) {
        out_reserved_count = 0;
        return OS_INVALID_PARAMETER;
    }
    if (phybase & (uint64_t)(_4KB_PG_SIZE - 1)) {
        out_reserved_count = 0;
        return OS_INVALID_PARAMETER;
    }

    // 计算起始索引（不做任何边界外扩）
    uint64_t idx1 = 0, idx2 = 0, idx4 = 0;
    phy_to_indices(phybase, idx1, idx2, idx4);

    // 总线性 4KB 页边界（基于 MAX_GB_SUPPORT）
    const uint64_t TOTAL_4KB_LIMIT = (uint64_t)MAX_GB_SUPPORT * 512ULL * 512ULL;
    uint64_t start_linear = idx1 * 512ULL * 512ULL + idx2 * 512ULL + idx4;
    if (start_linear >= TOTAL_4KB_LIMIT) {
        out_reserved_count = 0;
        return OS_OUT_OF_RANGE;
    }

    // 迭代器从给定位置开始（构造函数会根据 is_sub_valid 折叠到实际粒度）
    atom_page_ptr cur((uint32_t)idx1, (uint16_t)idx2, (uint16_t)idx4);

    uint64_t counted = 0;

    // 逐原子页检查（注意：对 2MB/1GB 原子页一次性累加对应的 4KB 数）
    while (counted < max_pages) {
        // 非法迭代器
        if (cur.page_size == 0 || cur.page_strut_ptr == nullptr) {
            break;
        }

        if (cur.page_size == _4KB_PG_SIZE) {
            page_size4kb_t *p4 = reinterpret_cast<page_size4kb_t*>(cur.page_strut_ptr);
            if (!p4) break;
            // 必须是 RESERVED 或 LOW1MB_RESERVED 才能继续
            if (p4->flags.state == RESERVED || p4->flags.state == LOW1MB_RESERVED) {
                counted++;
            } else {
                break;
            }
            // 下一原子 4KB
            int nxt = cur.the_next();
            if (nxt != OS_SUCCESS) break;
            continue;
        }

        if (cur.page_size == _2MB_PG_SIZE) {
            page_size2mb_t *p2 = reinterpret_cast<page_size2mb_t*>(cur.page_strut_ptr);
            if (!p2) break;
            // 如果 2MB 原子页本身就是 RESERVED/LOW1MB_RESERVED，则把 512 个 4KB 一次性计入
            if (p2->flags.state == RESERVED || p2->flags.state == LOW1MB_RESERVED) {
                uint64_t remain = max_pages - counted;
                const uint64_t this_block = 512ULL;
                if (remain >= this_block) {
                    counted += this_block;
                    // 跳到下一个原子页
                    int nxt = cur.the_next();
                    if (nxt != OS_SUCCESS) break;
                    continue;
                } else {
                    // 对于2MB页，如果请求的页面数不足512，我们仍然可以接受部分计数
                    counted += remain;
                    break;
                }
            } else {
                // 2MB 原子页不是 RESERVED -> stop
                break;
            }
        }

        if (cur.page_size == _1GB_PG_SIZE) {
            page_size1gb_t *p1 = reinterpret_cast<page_size1gb_t*>(cur.page_strut_ptr);
            if (!p1) break;
            // 若 1GB 原子页是 RESERVED/LOW1MB_RESERVED，则一次性计入 512*512 个 4KB
            if (p1->flags.state == RESERVED || p1->flags.state == LOW1MB_RESERVED) {
                uint64_t this_block = 512ULL * 512ULL;
                uint64_t remain = max_pages - counted;
                if (remain >= this_block) {
                    counted += this_block;
                    int nxt = cur.the_next();
                    if (nxt != OS_SUCCESS) break;
                    continue;
                } else {
                    // 对于1GB页，如果请求的页面数不足完整数量，我们仍然可以接受部分计数
                    counted += remain;
                    break;
                }
            } else {
                break;
            }
        }

        // 不可达
        break;
    }

    out_reserved_count = counted;
    return OS_SUCCESS;
}
phygpsmemmgr_t::atom_page_ptr::atom_page_ptr(
    uint32_t idx1g, uint16_t idx2m, uint16_t idx4k)
{
    phygpsmemmgr_t &mgr = gPhyPgsMemMgr;

    // ---- 保护基本索引 ---------------------------------------
    if (idx1g >= mgr.seg_support_1gb_page_count) {
        // 构造一个“无效迭代器”
        _1gbtb_idx = mgr.seg_support_1gb_page_count;
        page_size = 0;
        page_strut_ptr = nullptr;
        return;
    }

    _1gbtb_idx = idx1g;
    _2mbtb_offestidx = (idx2m < 512 ? idx2m : 0);
    _4kb_offestidx  = (idx4k < 512 ? idx4k : 0);

    page_size1gb_t &p1 = *mgr.top_1gb_table->get(_1gbtb_idx);

    // ====== Case 1: 1GB 原子页 =================================
    if (!p1.flags.is_sub_valid) {
        page_size = _1GB_PG_SIZE;
        _2mbtb_offestidx = 0;
        _4kb_offestidx = 0;
        page_strut_ptr = &p1;
        return;
    }

    // ====== Case 2: 2MB 级别存在 ===============================
    page_size2mb_t &p2 = p1.sub2mbpages[_2mbtb_offestidx];

    if (!p2.flags.is_sub_valid) {
        // 2MB 原子页
        page_size = _2MB_PG_SIZE;
        _4kb_offestidx = 0;
        page_strut_ptr = &p2;
        return;
    }

    // ====== Case 3: 4KB 子表存在 ===============================
    page_size = _4KB_PG_SIZE;
    
    if (_4kb_offestidx >= 512)
        _4kb_offestidx = 0;

    page_strut_ptr = &p2.sub_pages[_4kb_offestidx];
}


int phygpsmemmgr_t::atom_page_ptr::the_next()
{
    phygpsmemmgr_t &mgr = gPhyPgsMemMgr;

    switch (page_size)
    {
    // ----------------------------------------------------------------------
    // 4KB atomic page: increment _4kb offset, or move to next 2MB entry
    // ----------------------------------------------------------------------
    case _4KB_PG_SIZE:
    {
        if (_4kb_offestidx + 1 < 512) {
            _4kb_offestidx++;

            page_size2mb_t &p2 = mgr.top_1gb_table->get(_1gbtb_idx)->sub2mbpages[_2mbtb_offestidx];

            page_strut_ptr = &p2.sub_pages[_4kb_offestidx];
            return OS_SUCCESS;
        }

        // 否则进入下一个 2MB
        _4kb_offestidx = 0;
        // 继续向下执行 2MB 的逻辑
    }
    [[fallthrough]];

    // ----------------------------------------------------------------------
    // 2MB atomic page: increment _2mb offset, or move to next 1GB entry
    // ----------------------------------------------------------------------
    case _2MB_PG_SIZE:
    {
        page_size1gb_t &p1 = *mgr.top_1gb_table->get(_1gbtb_idx);

        if (_2mbtb_offestidx + 1 < 512) {
            _2mbtb_offestidx++;

            page_size2mb_t &p2 = p1.sub2mbpages[_2mbtb_offestidx];

            // 检查下一级子页是否存在

            bool has_sub = p2.flags.is_sub_valid;


            if (has_sub) {
                // 转变为 4KB atomic page
                page_size = _4KB_PG_SIZE;
                _4kb_offestidx = 0;
                page_strut_ptr = &p2.sub_pages[0];
            } else {
                // 仍然是 2MB atomic page
                page_strut_ptr = &p2;
            }
            return 0;
        }

        // 否则进入下一条 1GB
        _2mbtb_offestidx = 0;
        // 继续向下执行 1GB 的逻辑
    }
    [[fallthrough]];

    // ----------------------------------------------------------------------
    // 1GB atomic page: increment _1gb index, or fail
    // ----------------------------------------------------------------------
    case _1GB_PG_SIZE:
    {
        if (_1gbtb_idx + 1 >= mgr.seg_support_1gb_page_count) {
            return OS_OUT_OF_RANGE;   // 超出整个物理段
        }

        _1gbtb_idx++;

        page_size1gb_t &p1 = *mgr.top_1gb_table->get(_1gbtb_idx);

        // 是否有子表？
        bool has_sub = p1.flags.is_sub_valid;

        if (has_sub) {
            // 进入 2MB table
            page_size = _2MB_PG_SIZE;
            _2mbtb_offestidx = 0;
            page_size2mb_t &p2 = p1.sub2mbpages[0];
            bool has_sub_2 = p2.flags.is_sub_valid;
            if (has_sub_2) {
                page_size = _4KB_PG_SIZE;
                _4kb_offestidx = 0;
                page_strut_ptr = &p2.sub_pages[0];
            } else {
                page_strut_ptr = &p2;
            }
        }
        else {
            // 仍然是 1GB atomic page
            page_size = _1GB_PG_SIZE;
            _2mbtb_offestidx = 0;
            _4kb_offestidx = 0;
            page_strut_ptr = &p1;
        }

        return 0;
    }

    default:
        return OS_INVALID_ADDRESS; // 非法 page_size
    }
    return OS_UNREACHABLE_CODE;
}
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }

static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
// 将 4KB 页号(从 segment base 开始的页号) -> 1GB 索引/2MB 索引/4KB offset
inline uint64_t pg4k_to_1gb_idx(uint64_t pg4k_index) {
    return pg4k_index / PAGES_4KB_PER_1GB;
}
inline uint64_t pg4k_to_2mb_idx(uint64_t pg4k_index) {
    return pg4k_index / PAGES_4KB_PER_2MB;
}
inline uint16_t pg4k_to_4kb_offset(uint64_t pg4k_index) {
    return (uint16_t)(pg4k_index & (PAGES_4KB_PER_2MB - 1)); // 0..511
}
inline uint16_t pg2mb_to_4kb_offset_in_2mb(uint64_t pg2mb_index) {
    return 0; // for clarity; used as above if needed
}



int phygpsmemmgr_t::ensure_1gb_subtable(uint64_t idx_1gb)
{
    page_size1gb_t& p1 = *top_1gb_table->get(idx_1gb);
    if (!p1.flags.is_sub_valid)
    {
        page_size2mb_t* sub2 = new page_size2mb_t[PAGES_2MB_PER_1GB];
        if(!sub2){

            return OS_OUT_OF_MEMORY;
            }

        kpoolmemmgr_t::clear(sub2);

        p1.sub2mbpages = sub2;
        p1.flags.is_sub_valid = 1;
        p1.flags.state = PARTIAL;
    }

    return OS_SUCCESS;
}

int phygpsmemmgr_t::ensure_2mb_subtable(page_size2mb_t &p2)
{
    if (!p2.flags.is_sub_valid)
    {
        page_size4kb_t* sub4 = new page_size4kb_t[PAGES_4KB_PER_2MB];
        if (!sub4){return OS_OUT_OF_MEMORY;}

        kpoolmemmgr_t::clear(sub4);

        p2.sub_pages = sub4;
        p2.flags.is_sub_valid = 1;
        p2.flags.state = PARTIAL;
    }
    return 0;
}

int phygpsmemmgr_t::try_fold_2mb(page_size2mb_t &p2)
{
    // 没有子表就是原子 2MB 页，无法向上折叠
    if (!p2.flags.is_sub_valid)
        return 0;

    bool all_free = true;
    bool all_used_or_full = true;

    for (uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++)
    {
        uint8_t st = p2.sub_pages[i].flags.state;
        if (st != FREE) all_free = false;
        // all_used_or_full 要求每个子页不是 FREE（即已被占用或被标为 FULL）
        if (st == FREE) all_used_or_full = false;

        if (!all_free && !all_used_or_full)
            break; // 已经确定是 PARTIAL
    }

    if (all_free)
    {
        // 全部 free：可以删除子表，恢复为原子 FREE 2MB
        delete[] p2.sub_pages;
        p2.sub_pages = nullptr;
        p2.flags.is_sub_valid = 0;
        p2.flags.state = FREE;
        return 1;
    }

    if (all_used_or_full)
    {
        // 全部被占用或被标为 FULL：将该 2MB 标记为 FULL
        // **注意：不删除子表**（后续可能会有引用计数变动）
        p2.flags.state = FULL;
        // 保持 p2.flags.is_sub_valid == 1
        return 1;
    }

    // 混合：部分占用，标记为 PARTIAL，不删除子表
    p2.flags.state = PARTIAL;
    return 0;
}

int phygpsmemmgr_t::try_fold_1gb(page_size1gb_t &p1)
{
    // 没有子表就是原子 1GB 页，无法向上折叠
    if (!p1.flags.is_sub_valid)
        return 0;

    bool all_free = true;
    bool all_full = true;

    for (uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++)
    {
        page_size2mb_t &p2 = p1.sub2mbpages[i];

        // 如果 p2 是非原子（有子表），尝试折叠它（这样可以把能 collapse 的 2MB 变为 FREE/FULL）
        if (p2.flags.is_sub_valid)
        {
            try_fold_2mb(p2);
            // try_fold_2mb 会在 all-free 或 all-used 情况下修改 p2.flags.state，
            // 且在 all_free 情况下会释放 p2.sub_pages 并把 is_sub_valid 置 0。
        }

        // 现在基于 p2.flags.state 决策：
        if (p2.flags.state != FREE) all_free = false;
        if (p2.flags.state != FULL) all_full = false;

        if (!all_free && !all_full)
            break; // 已经确定是 PARTIAL
    }

    if (all_free)
    {
        // 所有 2MB 都是 FREE：删除 1GB 的子表并恢复为原子 FREE
        delete[] p1.sub2mbpages;
        p1.sub2mbpages = nullptr;
        p1.flags.is_sub_valid = 0;
        p1.flags.state = FREE;
        return 1;
    }

    if (all_full)
    {
        // 所有 2MB 都是 FULL：标记该 1GB 为 FULL
        // **注意：不删除子表**（保留子表以便未来 refcount 增加或子页操作）
        p1.flags.state = FULL;
        // 保持 p1.flags.is_sub_valid == 1
        return 1;
    }

    // 混合：标记为 PARTIAL，保留子表
    p1.flags.state = PARTIAL;
    return 0;
}

int phygpsmemmgr_t::pages_recycle(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    module_global_lock.lock();
    int status=pages_recycle_verify(phybase,numof_4kbpgs);
    if(status!=OS_SUCCESS){module_global_lock.unlock();return OS_SUCCESS;}
    status=pages_state_set(phybase,numof_4kbpgs,FREE);
    module_global_lock.unlock();
    return status;

}


int phygpsmemmgr_t::Init()
{
    base=0;
    int status=0;
    seg_size_in_byte=gBaseMemMgr.getMaxPhyaddr();
    seg_support_1gb_page_count=(seg_size_in_byte+_1GB_PG_SIZE-1)/_1GB_PG_SIZE;
    seg_support_2mb_page_count=seg_support_1gb_page_count*512;
    seg_support_4kb_page_count=seg_support_2mb_page_count*512;
    uint64_t tail_4kb_pages_count=
    (seg_support_1gb_page_count*_1GB_PG_SIZE-seg_size_in_byte+_4KB_PG_SIZE-1)/_4KB_PG_SIZE;
    //todo 注册函数
    setmem(top_1gb_table,seg_support_1gb_page_count*sizeof(top_1gb_table),0);
    uint32_t phymemtb_count=gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    phy_memDescriptor*base=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    for(uint32_t i=0;i<phymemtb_count;i++)
    {
        page_state_t seg_state;
        switch(base[i].Type)
        {
            case freeSystemRam:seg_state=FREE;break;
            case EFI_RUNTIME_SERVICES_CODE:
            case EFI_RUNTIME_SERVICES_DATA:seg_state=UEFI_RUNTIME;break;
            case EFI_ACPI_RECLAIM_MEMORY:seg_state=ACPI_TABLES;break;
            case EFI_MEMORY_MAPPED_IO:
            case EFI_MEMORY_MAPPED_IO_PORT_SPACE:seg_state=MMIO;break;
            case OS_KERNEL_DATA:
            case OS_KERNEL_CODE:
            case OS_KERNEL_STACK:seg_state=KERNEL_PERSIST;break;
            case EFI_RESERVED_MEMORY_TYPE:seg_state=RESERVED;break;
            default:seg_state=RESERVED; 
            break;
        } 
        phy_memDescriptor& seg=base[i];
        phyaddr_t segbase=seg.PhysicalStart;
        phyaddr_t segend=seg.PhysicalStart+seg.NumberOfPages*_4KB_PG_SIZE;
        if(segbase<_1MB_PHYADDR)
        {
            if(segend<=_1MB_PHYADDR){
                if(seg_state==FREE){
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,seg.NumberOfPages,LOW1MB_FREE, flags);
                }
                else if(seg_state==EFI_RESERVED_MEMORY_TYPE){
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,seg.NumberOfPages,LOW1MB_RESERVED, flags);
                }
                else {
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,seg.NumberOfPages,LOW1MB_USED, flags);
                }
            }else{
                if(seg_state==FREE){
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,(_1MB_PHYADDR-segbase)/_4KB_PG_SIZE,LOW1MB_FREE, flags);
                }
                else if(seg_state==EFI_RESERVED_MEMORY_TYPE){
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,(_1MB_PHYADDR-segbase)/_4KB_PG_SIZE,LOW1MB_RESERVED, flags);
                }
                else {
                    pages_state_set_flags_t flags = {0, 0};
                    status=pages_state_set(segbase,(_1MB_PHYADDR-segbase)/_4KB_PG_SIZE,LOW1MB_USED, flags);
                }
                
                if(status != OS_SUCCESS) return status;
                
                pages_state_set_flags_t flags = {0, 0};
                status=pages_state_set(_1MB_PHYADDR,(segend-_1MB_PHYADDR)/_4KB_PG_SIZE,seg_state, flags);
            }
            
            if(status != OS_SUCCESS) return status;
        }

    }


    //内核映像注册
    pages_state_set_flags_t flags = {0, 0};
    status=pages_state_set((phyaddr_t)&init_text_begin,(&init_text_end-&init_text_begin)/_4KB_PG_SIZE,LOW1MB_USED, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&init_rodata_begin,(&init_rodata_end-&init_rodata_begin)/_4KB_PG_SIZE,LOW1MB_USED, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&KImgphybase,(phyaddr_t)(text_end-text_begin)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_data_lma,(&_data_end-&_data_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_rodata_lma,(&_rodata_end-&_rodata_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_stack_lma,(&_stack_top-&_stack_bottom)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_heap_bitmap_lma,(&__heap_bitmap_end-&__heap_bitmap_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_heap_lma,(&__heap_end-&__heap_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_pgtb_heap_lma,(&__pgtbhp_end-&__pgtbhp_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_klog_lma,(&__klog_end-&__klog_start)/_4KB_PG_SIZE,KERNEL_PERSIST, flags);
    if(status != OS_SUCCESS) return status;

    status=pages_state_set(seg_size_in_byte,tail_4kb_pages_count,RESERVED, flags);
    return status;
}


// ------------------------
// 仅用于 pages_recycle 的回收前校验
// ------------------------
int phygpsmemmgr_t::pages_recycle_verify(
    phyaddr_t phybase,
    uint64_t num_of_4kbpgs
) {
    if (num_of_4kbpgs == 0) return OS_INVALID_PARAMETER;
    if (phybase & (_4KB_PG_SIZE - 1)) return OS_INVALID_PARAMETER;

    uint64_t idx1, idx2, idx4;
    phy_to_indices(phybase, idx1, idx2, idx4);

    // 构造 atom_page_ptr 自动折叠到正确原子页
    atom_page_ptr cur(idx1, idx2, idx4);

    // 第一页决定类型
    page_state_t orig_state = FREE;
    bool state_initialized = false;

    uint64_t checked = 0;

    while (checked < num_of_4kbpgs) {

        if (cur.page_size == _4KB_PG_SIZE) {

            auto *p4 = reinterpret_cast<page_size4kb_t*>(cur.page_strut_ptr);
            if (!p4) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p4->flags.state;
                state_initialized = true;

                // 必须属于可回收类型
                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p4->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p4->ref_count != 0)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p4->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            checked++;
            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 2MB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _2MB_PG_SIZE) {

            auto *p2 = reinterpret_cast<page_size2mb_t*>(cur.page_strut_ptr);
            if (!p2) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p2->flags.state;
                state_initialized = true;

                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p2->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p2->ref_count != 0)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p2->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            // 原子 2MB 页必须整块被回收
            if (num_of_4kbpgs - checked < 512)
                return OS_INVALID_PARAMETER;

            checked += 512;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 1GB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _1GB_PG_SIZE) {

            auto *p1 = reinterpret_cast<page_size1gb_t*>(cur.page_strut_ptr);
            if (!p1) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p1->flags.state;
                state_initialized = true;

                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p1->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p1->ref_count != 0)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p1->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            // 原子 1GB 必须整块被回收
            const uint64_t block = 512ULL * 512ULL;

            if (num_of_4kbpgs - checked < block)
                return OS_INVALID_PARAMETER;

            checked += block;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }

        // 不可能到这
        return OS_INVALID_ADDRESS;
    }
    
    // 全部通过
    return OS_SUCCESS;
}
int phygpsmemmgr_t::inner_mmio_regist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    if (numof_4kbpgs == 0) return OS_SUCCESS;
    // 切段
    seg_to_pages_info_pakage_t pak;
    int r = phymemseg_to_pacage(phybase, numof_4kbpgs, pak);
    if (r != OS_SUCCESS) return r;

    // 每个段逐个处理
    const uint64_t PAGES_4KB_PER_2MB = 512ULL;
    const uint64_t PAGES_4KB_PER_1GB = 512ULL * 512ULL;
    const uint64_t PAGES_2MB_PER_1GB = 512ULL;

    for (int ei = 0; ei < 5; ei++) {
        auto &ent = pak.entryies[ei];
        if (ent.num_of_pages == 0) continue;

        // linear 4KB start index relative to this->base
        uint64_t pg4k_start = (ent.base - this->base) >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE) {
            // ent.num_of_pages is count of 1GB blocks
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            _1gb_pages_state_set(idx_1gb,ent.num_of_pages, MMIO);
        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE) {
            // ent.base is 2MB aligned region
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;

            // ensure parent 1GB subtable exists to access this 2MB entry (if needed)
            page_size1gb_t &p1 = *top_1gb_table->get(idx_1gb);
            if (!p1.flags.is_sub_valid) {
                // if parent is pure RESERVED and we need to split only part, only then ensure;
                // here caller asked for a 2MB-granularity registration: if parent is RESERVED and we are covering a whole 2MB,
                // we must ensure parent subtable exists before changing child state (per your rule: ensure -> parent -> RESERVED_MMIO)
                r = ensure_1gb_subtable(idx_1gb);
                if (r != OS_SUCCESS) return r;
                p1.flags.state = RESERVED_MMIO;
                p1.flags.is_sub_valid = 1;
            }

            _2mb_pages_state_set(idx_1gb, idx_2mb, MMIO,p1.sub2mbpages);
        }
        else if (ent.page_size_in_byte == _4KB_PG_SIZE) {
            // ent covers some number of 4KB pages contiguously
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            page_size1gb_t &p1 = *top_1gb_table->get(idx_1gb);

            // if parent 1GB is pure (not sub_valid) and we need to change children -> ensure parent and mark RESERVED_MMIO
            if (!p1.flags.is_sub_valid) {
                r = ensure_1gb_subtable(idx_1gb);
                if (r != OS_SUCCESS) return r;
                p1.flags.is_sub_valid = 1;
                p1.flags.state = RESERVED_MMIO;
            }

            page_size2mb_t &p2 = p1.sub2mbpages[idx_2mb % PAGES_2MB_PER_1GB];

            // if parent 2MB is not subdivided and we are going to change 4KB children, ensure and mark RESERVED_MMIO
            if (!p2.flags.is_sub_valid) {
                r = ensure_2mb_subtable(p2);
                if (r != OS_SUCCESS) return r;
                p2.flags.is_sub_valid = 1;
                p2.flags.state = RESERVED_MMIO;
            }

            page_size4kb_t *p4 = p2.sub_pages;
            if (!p4) return OS_INVALID_ADDRESS;

            // set the requested 4KB range to MMIO
            uint64_t start_off = pg4k_start % PAGES_4KB_PER_2MB;
            r = _4kb_pages_state_set(start_off, ent.num_of_pages, MMIO, p4, false);
            if (r != OS_SUCCESS) return r;
        }
        else {
            // unexpected page size
            return OS_INVALID_PARAMETER;
        }
    } // end for entries

    return OS_SUCCESS;
}
