#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "VideoDriver.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int phygpsmemmgr_t::pages_state_set(phyaddr_t base,
                                    uint64_t num_of_4kbpgs,
                                    page_state_t state,
                                pages_state_set_flags_t flags)
{
    if (num_of_4kbpgs == 0) return 0;

    // ---- Step 1: 切段 ----
    seg_to_pages_info_pakage_t pak;
    int r = phymemseg_to_pacage(base, num_of_4kbpgs, pak);
    if (r != 0) return r;

    // 定义四个lambda函数替换原来的成员函数
    auto ensure_1gb_subtable_lambda = [flags](uint64_t idx_1gb) -> int {
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
            p1.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_PARTIAL:PARTIAL;
        }

        return OS_SUCCESS;
    };

    auto ensure_2mb_subtable_lambda = [flags](page_size2mb_t &p2) -> int {
        if (!p2.flags.is_sub_valid)
        {
            page_size4kb_t* sub4 = new page_size4kb_t[PAGES_4KB_PER_2MB];
            if (!sub4){return OS_OUT_OF_MEMORY;}

            kpoolmemmgr_t::clear(sub4);

            p2.sub_pages = sub4;
            p2.flags.is_sub_valid = 1;
            p2.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_PARTIAL:PARTIAL;
        }
        return 0;
    };

    auto try_fold_2mb_lambda = [flags](page_size2mb_t &p2) -> int {
        // 没有子表就是原子 2MB 页，无法向上折叠
        if (!p2.flags.is_sub_valid)
            return 0;

        bool all_free = true;
        bool all_full = true;

        for (uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++)
        {
            uint8_t st = p2.sub_pages[i].flags.state;
            if(flags.if_Split_atomic_in_mmio_partial)
            {
            if (st != MMIO_FREE) all_free = false;
            // all_used_or_full 要求每个子页不是 FREE（即已被占用或被标为 FULL）
            if (st == MMIO_FREE) all_full = false;
            }else{
                if (st != FREE) all_free = false;
                if (st == FREE) all_full = false;
            }

            if (!all_free && !all_full)
                break; // 已经确定是 PARTIAL
        }

        if (all_free)
        {
            // 全部 free：可以删除子表，恢复为原子 FREE 2MB
            delete[] p2.sub_pages;
            p2.sub_pages = nullptr;
            p2.flags.is_sub_valid = 0;
            p2.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_FREE:FREE;
            return 1;
        }

        if (all_full)
        {
            // 全部被占用或被标为 FULL：将该 2MB 标记为 FULL
            // **注意：不删除子表**（后续可能会有引用计数变动）
            p2.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_FULL:FULL;
            // 保持 p2.flags.is_sub_valid == 1
            return 1;
        }

        // 混合：部分占用，标记为 PARTIAL，不删除子表
        p2.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_PARTIAL:PARTIAL;
        return 0;
    };

    auto try_fold_1gb_lambda = [&try_fold_2mb_lambda,flags](page_size1gb_t &p1) -> int {
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
                try_fold_2mb_lambda(p2);
                // try_fold_2mb 会在 all-free 或 all-used 情况下修改 p2.flags.state，
                // 且在 all_free 情况下会释放 p2.sub_pages 并把 is_sub_valid 置 0。
            }

            // 现在基于 p2.flags.state 决策：
            if(flags.if_Split_atomic_in_mmio_partial==false)
            {
            if (p2.flags.state != FREE) all_free = false;
            if (p2.flags.state != FULL) all_full = false;
            }else{
                if (p2.flags.state != MMIO_FREE) all_free = false;
                if (p2.flags.state != MMIO_FULL) all_full = false;
            }

            if (!all_free && !all_full)
                break; // 已经确定是 PARTIAL
        }

        if (all_free)
        {
            // 所有 2MB 都是 FREE：删除 1GB 的子表并恢复为原子 FREE
            delete[] p1.sub2mbpages;
            p1.sub2mbpages = nullptr;
            p1.flags.is_sub_valid = 0;
            p1.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_FREE:FREE;
            return 1;
        }

        if (all_full)
        {
            // 所有 2MB 都是 FULL：标记该 1GB 为 FULL
            // **注意：不删除子表**（保留子表以便未来 refcount 增加或子页操作）
            p1.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_FULL:FULL;
            // 保持 p1.flags.is_sub_valid == 1
            return 1;
        }

        // 混合：标记为 PARTIAL，保留子表
        p1.flags.state = flags.if_Split_atomic_in_mmio_partial?MMIO_PARTIAL:PARTIAL;
        return 0;
    };

    auto _1gb_pages_state_set_lambda = [state,flags](uint64_t entry_base_idx, uint64_t num_of_1gbpgs) -> int {
        int status = OS_SUCCESS;
        for (uint64_t i = 0; i < num_of_1gbpgs; i++) {
            page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + i);
            if(_1==nullptr){
                if(flags.is_blackhole_aclaim){
                    status=top_1gb_table->enable_idx(entry_base_idx+i);
                    if(status!=OS_SUCCESS){
                        //可能是内存不足或者参数非法越界
                        kputsSecure("_1gb_pages_state_set:unable to alloc memory or out of bound\n");
                        return OS_FAIL_PAGE_ALLOC;
                    }
                    _1=top_1gb_table->get(entry_base_idx+i);
                }else{//非黑洞模式不得创建
                    kputsSecure("_1gb_pages_state_set:attempt to create a new entry in no black hole mode\n");
                    return OS_INVALID_FILE_MODE;
                }
            }
            _1->flags.state = state;
            if(flags.if_init_ref_count)_1->ref_count=1;
        }
        return status;
    };

    auto _4kb_pages_state_set_lambda = [state,flags](uint64_t entry4kb_base_idx, uint64_t num_of_4kbpgs, page_size4kb_t *base_entry) -> int {
        for (uint64_t i = 0; i < num_of_4kbpgs; i++) {
            auto& page_entry = base_entry[entry4kb_base_idx + i];
            page_entry.flags.state = state;
            if(flags.if_init_ref_count)page_entry.ref_count=1;
        }
        return OS_SUCCESS;
    };

    auto _2mb_pages_state_set_lambda = [state,flags](uint64_t entry2mb_base_idx, uint64_t num_of_2mbpgs, page_size2mb_t *base_entry) -> int {
        for (uint64_t i = 0; i < num_of_2mbpgs; i++) {
            auto& page_entry = base_entry[entry2mb_base_idx + i];
            page_entry.flags.state = state;
            if(flags.if_init_ref_count)page_entry.ref_count=1;
        }
        return OS_SUCCESS;
    };

    // ---- Step 2: 遍历每个段条目 ----
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entryies[i];
        if (ent.num_of_pages == 0) continue;

        uint64_t pg4k_start = ent.base >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE)
        {
            // ===== 1GB 级 =====
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            r = _1gb_pages_state_set_lambda(idx_1gb, ent.num_of_pages);
            if (r != 0) return r;
        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE)
        {
            // ===== 2MB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            if((r=ensure_1gb_subtable_lambda(idx_1gb))!=OS_SUCCESS)
            return r;

            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = top_1gb_table->get(idx_1gb)->sub2mbpages;
            r = _2mb_pages_state_set_lambda(idx_2mb % PAGES_2MB_PER_1GB,
                                     ent.num_of_pages,
                                     p2);
            if (r != OS_SUCCESS) return r;
            try_fold_1gb_lambda(*top_1gb_table->get(idx_1gb));
        }
        
        else if (ent.page_size_in_byte == _4KB_PG_SIZE)
        {
            // ===== 4KB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            page_size1gb_t &p1 = *top_1gb_table->get(idx_1gb);
            if((r=ensure_1gb_subtable_lambda(idx_1gb))!=OS_SUCCESS)return r;
            page_size2mb_t &p2ent = p1.sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            if((r=ensure_2mb_subtable_lambda(p2ent))!=OS_SUCCESS)return r;
          
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            r = _4kb_pages_state_set_lambda(pg4k_start % PAGES_4KB_PER_2MB,
                                     ent.num_of_pages,
                                     p4);
            if (r != 0) return r;
                try_fold_2mb_lambda(p2ent);
                try_fold_1gb_lambda(p1);
            
        }
    }

    return OS_SUCCESS;
}


int phygpsmemmgr_t::phymemseg_to_pacage(
        phyaddr_t base,
        uint64_t num_of_4kbpgs,
        seg_to_pages_info_pakage_t& pak)
{
    // 清空
    for (int i = 0; i < 5; i++) {
        pak.entryies[i].base = 0;
        pak.entryies[i].num_of_pages = 0;
        pak.entryies[i].page_size_in_byte = 0;
    }

    uint64_t start = base;
    uint64_t end   = base + num_of_4kbpgs * _4KB_PG_SIZE;

    int idx = 0;

    /////////////////////////////////////////////////////////
    // 1) 如果在高 4GB，则尝试对齐并截取 1GB 页
    /////////////////////////////////////////////////////////
    if (start >= 0x100000000ULL) {
        uint64_t gb_aligned_start = align_up(start, _1GB_PG_SIZE);

        // 中间的 4KB/2MB 不丢弃，只是不适用 1GB
        if (gb_aligned_start < end) {

            uint64_t gb_area_end = align_down(end, _1GB_PG_SIZE);
            if (gb_area_end > gb_aligned_start) {

                pak.entryies[idx].base = gb_aligned_start;
                pak.entryies[idx].page_size_in_byte = _1GB_PG_SIZE;
                pak.entryies[idx].num_of_pages =
                    (gb_area_end - gb_aligned_start) / _1GB_PG_SIZE;

                idx++;

                // 截掉 1GB 区域
                if (start < gb_aligned_start)
                    ; // 前部留给 2MB/4KB 处理
                start = gb_area_end;
            }
        }
    }

    /////////////////////////////////////////////////////////
    // 2) 尝试对齐 2MB 大页
    /////////////////////////////////////////////////////////
    {
        uint64_t mb_aligned_start = align_up(start, _2MB_PG_SIZE);
        if (mb_aligned_start < end) {

            uint64_t mb_area_end = align_down(end, _2MB_PG_SIZE);
            if (mb_area_end > mb_aligned_start) {

                pak.entryies[idx].base = mb_aligned_start;
                pak.entryies[idx].page_size_in_byte = _2MB_PG_SIZE;
                pak.entryies[idx].num_of_pages =
                    (mb_area_end - mb_aligned_start) / _2MB_PG_SIZE;

                idx++;

                // 截掉 2MB 区
                if (start < mb_aligned_start)
                    ; // 前部留给 4KB
                start = mb_area_end;
            }
        }
    }

    /////////////////////////////////////////////////////////
    // 3) 剩下全部按 4KB 页
    /////////////////////////////////////////////////////////
    if (start < end) {
        pak.entryies[idx].base = start;
        pak.entryies[idx].page_size_in_byte = _4KB_PG_SIZE;
        pak.entryies[idx].num_of_pages =
            (end - start) / _4KB_PG_SIZE;

        idx++;
    }

    return OS_SUCCESS;
}
phyaddr_t phygpsmemmgr_t::pages_alloc(uint64_t numof_4kbpgs, phygpsmemmgr_t::page_state_t state, uint8_t align_log2)
{
    if(state==KERNEL||state==USER_ANONYMOUS||state==USER_FILE)
    {
        module_global_lock.lock();
        
        // 定义inner_pages_alloc作为lambda表达式
        auto inner_pages_alloc = [numof_4kbpgs,align_log2](
            phyaddr_t &result_base,
            page_state_t state
        ) -> int {
            if (numof_4kbpgs == 0)
                return OS_MEMRY_ALLOCATE_FALT;
            if(align_log2 > 30)return OS_INVALID_PARAMETER;
            // 自动向上取最近对齐粒度
            uint8_t a = [align_log2]()->uint8_t{
                if (align_log2 < 12) return 12;          // 最小对齐是 4KB
                if (align_log2 <= 20) return 21;         // 12~20 → 上调到 2MB
                if (align_log2 <= 29) return 30;         // 21~29 → 上调到 1GB

                // 30以上按1GB处理（你的要求）
                return 30;
            }();
            int status;
            phyaddr_t result = 0;
            PHYSEG current_seg;
            for(auto it = physeg_list->begin(); it != physeg_list->end(); ++it){
                current_seg=*it;
                if(current_seg.type==DRAM_SEG) {
                    struct phyaddr_in_idx_t
                        {
                        uint64_t _1gb_idx=0;
                        uint16_t _2mb_idx=0;
                        uint16_t _4kb_idx=0;
                        
                        // 添加默认构造函数
                        phyaddr_in_idx_t() : _1gb_idx(0), _2mb_idx(0), _4kb_idx(0) {}
                        };
                    auto idx_to_phyaddr=[](phyaddr_in_idx_t phyaddr_in_idx)->phyaddr_t{
                            return (phyaddr_in_idx._1gb_idx*512*512+phyaddr_in_idx._2mb_idx*512+phyaddr_in_idx._4kb_idx)<<12;
                        };  
                        auto phyaddr_to_idx=[](phyaddr_t phyaddr)->phyaddr_in_idx_t{
                            phyaddr_in_idx_t phyaddr_in_idx;
                            phyaddr_in_idx._4kb_idx=(phyaddr>>12)&511;
                            phyaddr_in_idx._2mb_idx=(phyaddr>>21)&511;
                            phyaddr_in_idx._1gb_idx=phyaddr>>30;
                            return phyaddr_in_idx;
                        };
                        auto is_idx_equal=[](phyaddr_in_idx_t a,phyaddr_in_idx_t b)->bool{
                            return a._1gb_idx==b._1gb_idx&&a._2mb_idx==b._2mb_idx&&a._4kb_idx==b._4kb_idx;
                        };
                    auto align4kb_pages_search = [current_seg, &result, idx_to_phyaddr, phyaddr_to_idx]
                    (uint64_t num_of_4kbpgs) -> int {

    const uint64_t P4K = 1ULL << 12;
    const uint64_t P2M = 512ULL * P4K;
    const uint64_t P1G = 512ULL * P2M;

    phyaddr_in_idx_t begin = phyaddr_to_idx(current_seg.base);
    phyaddr_in_idx_t irritator = begin;
    phyaddr_t end_phyaddr_excl = current_seg.base + current_seg.seg_size; // exclusive

    uint64_t accummulated_count = 0;
    phyaddr_in_idx_t candidate_result = irritator;
    phyaddr_t expected_next_phys = idx_to_phyaddr(irritator); // 下一个期望的物理页起始地址

    // iterate over 1GB entries
    for (uint64_t i1 = irritator._1gb_idx; ; ++i1) {
        phyaddr_in_idx_t temp_idx;
        temp_idx._1gb_idx = i1;
        temp_idx._2mb_idx = 0;
        temp_idx._4kb_idx = 0;
        if (!(idx_to_phyaddr(temp_idx) < end_phyaddr_excl)) break;
        
        phyaddr_in_idx_t cur1;
        cur1._1gb_idx = i1;
        cur1._2mb_idx = 0;
        cur1._4kb_idx = 0;
        if (idx_to_phyaddr(cur1) >= end_phyaddr_excl) break;

        page_size1gb_t* p1 = top_1gb_table->get(cur1._1gb_idx);
        if (p1 == nullptr) {
            kputsSecure("Consistency violation between top_1gb_table and physeg_list");
            return OS_MEMRY_ALLOCATE_FALT;
        }

        if (!(p1->flags.is_sub_valid)) { // 原子 1GB
            phyaddr_t p1_base = idx_to_phyaddr(cur1);
            if (p1->flags.state == FREE) {
                // 连续性检查
                if (accummulated_count == 0 || p1_base != expected_next_phys) {
                    candidate_result = cur1;
                    accummulated_count = 512 * 512;
                } else {
                    accummulated_count += 512 * 512;
                }
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1 + 1;
                next_idx._2mb_idx = 0;
                next_idx._4kb_idx = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
                if (accummulated_count >= num_of_4kbpgs) {
                    result = idx_to_phyaddr(candidate_result);
                    return OS_SUCCESS;
                }
            } else if (p1->flags.state == KERNEL || p1->flags.state == USER_ANONYMOUS ||
                       p1->flags.state == USER_FILE || p1->flags.state == KERNEL_PERSIST) {
                accummulated_count = 0;
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1 + 1;
                next_idx._2mb_idx = 0;
                next_idx._4kb_idx = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
            } else  { // 其他注册/保留/非法
                kputsSecure("illegal pagestate when scanning 1gb atomic entry");
                    result =0;
                    return OS_MEMRY_ALLOCATE_FALT;
            }
            continue;
        }else if (p1->flags.state == FULL) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        }
        if(p1->flags.state == PARTIAL)
        {// PARTIAL at 1GB: need to iterate 2MB entries inside this 1GB
        page_size2mb_t* p2base = p1->sub2mbpages;
        if (p2base == nullptr) {
            kputsSecure("Inconsistent PARTIAL 1GB without sub2mbpages");
            return OS_MEMRY_ALLOCATE_FALT;
        }

        uint16_t start2 = 0;
        if (i1 == begin._1gb_idx) start2 = begin._2mb_idx;
        for (uint16_t i2 = start2; i2 < 512; ++i2) {
            phyaddr_in_idx_t cur2;
            cur2._1gb_idx = i1;
            cur2._2mb_idx = i2;
            cur2._4kb_idx = 0;
            if (idx_to_phyaddr(cur2) >= end_phyaddr_excl) break;

            page_size2mb_t* p2 = p2base + i2;
            if (!p2->flags.is_sub_valid) { // 原子 2MB
                phyaddr_t p2_base = idx_to_phyaddr(cur2);
                if (p2->flags.state == FREE) {
                    if (accummulated_count == 0 || p2_base != expected_next_phys) {
                        candidate_result = cur2;
                        accummulated_count = 512;
                    } else {
                        accummulated_count += 512;
                    }
                    expected_next_phys = p2_base + 512ULL * P4K;
                    if (accummulated_count >= num_of_4kbpgs) {
                        result = idx_to_phyaddr(candidate_result);
                        return OS_SUCCESS;
                    }
                } else if (p2->flags.state == KERNEL || p2->flags.state == USER_ANONYMOUS ||
                           p2->flags.state == USER_FILE || p2->flags.state == KERNEL_PERSIST) {
                    accummulated_count = 0;
                    phyaddr_in_idx_t next_idx;
                    next_idx._1gb_idx = i1;
                    next_idx._2mb_idx = i2 + 1;
                    next_idx._4kb_idx = 0;
                    expected_next_phys = idx_to_phyaddr(next_idx);
                } else {//剩下的类型不应该出现在dram里面
                    kputsSecure("illegal pagestate when scanning 2mb entry");
                    result =0;
                    return OS_MEMRY_ALLOCATE_FALT;
                }
                continue;
            }else if (p2->flags.state == FULL) {
                accummulated_count = 0;
                phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1;
            next_idx._2mb_idx = i2 + 1;
            next_idx._4kb_idx = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            }
            if(p2->flags.state == PARTIAL)
            {// PARTIAL at 2MB: iterate 4KB entries
            page_size4kb_t* p4base = p2->sub_pages;
            if (p4base == nullptr) {
                kputsSecure("Inconsistent PARTIAL 2MB without sub_pages");
                return OS_MEMRY_ALLOCATE_FALT;
            }

            uint16_t start4 = 0;
            if (i1 == begin._1gb_idx && i2 == begin._2mb_idx) start4 = begin._4kb_idx;
            for (uint16_t i4 = start4; i4 < 512; ++i4) {
                phyaddr_in_idx_t cur4;
                cur4._1gb_idx = i1;
                cur4._2mb_idx = i2;
                cur4._4kb_idx = i4;
                if (idx_to_phyaddr(cur4) >= end_phyaddr_excl) break;

                page_size4kb_t* p4 = p4base + i4;
                if (p4->flags.is_sub_valid) {
                    // 4KB 层不应当有 sub_valid（按你的注释），视为一致性错误
                    kputsSecure("4KB entry marked is_sub_valid unexpectedly");
                    return OS_MEMRY_ALLOCATE_FALT;
                }

                phyaddr_t p4_base = idx_to_phyaddr(cur4);
                if (p4->flags.state == FREE) {
                    if (accummulated_count == 0 || p4_base != expected_next_phys) {
                        candidate_result = cur4;
                        accummulated_count = 1;
                    } else {
                        ++accummulated_count;
                    }
                    expected_next_phys = p4_base + P4K;
                    if (accummulated_count >= num_of_4kbpgs) {
                        result = idx_to_phyaddr(candidate_result);
                        return OS_SUCCESS;
                    }
                } else if (p4->flags.state == KERNEL || p4->flags.state == USER_ANONYMOUS ||
                           p4->flags.state == USER_FILE || p4->flags.state == KERNEL_PERSIST) {
                    accummulated_count = 0;
                    expected_next_phys = p4_base + P4K;
                } else { //剩下的类型不应该出现在DRAM段里面
                    kputsSecure("illegal pagestate when scanning 1GB entry");
                    result =0;
                    return OS_MEMRY_ALLOCATE_FALT;
                }
            } // end for i4

            }else{
                kputsSecure("illegal pagestate when scanning 2MB entry"); 
                result =0;
                return OS_MEMRY_ALLOCATE_FALT;
            }
        } // end for i2
        }else{
            kputsSecure("illegal pagestate when scanning 1GB entry in dram seg");
            result =0;
            return OS_MEMRY_ALLOCATE_FALT;
        }
    } // end for i1

    // 未找到连续区
    return OS_MEMRY_ALLOCATE_FALT;
};

                    auto align2mb_pages_search = [current_seg, &result, idx_to_phyaddr, phyaddr_to_idx](uint64_t num_of_2mbpgs) -> int {
    phyaddr_t begin_phyaddr_incl = current_seg.base;
    phyaddr_t end_phyaddr_excl = current_seg.base + current_seg.seg_size;
    phyaddr_in_idx_t begin = phyaddr_to_idx(begin_phyaddr_incl);
    phyaddr_in_idx_t irritator = begin;
    phyaddr_t expected_next_phys = begin_phyaddr_incl;
    phyaddr_in_idx_t candidate_result;
    uint64_t accummulated_count = 0;

    for (uint64_t i1 = irritator._1gb_idx; ; ++i1) {
        phyaddr_in_idx_t cur1;
        cur1._1gb_idx = i1;
        cur1._2mb_idx = 0;
        cur1._4kb_idx = 0;
        if (idx_to_phyaddr(cur1) >= end_phyaddr_excl) break;

        page_size1gb_t *p1 = top_1gb_table->get(i1);
        if (p1 == nullptr) continue;

        if (!p1->flags.is_sub_valid) { // 原子 1GB
            phyaddr_t p1_base = idx_to_phyaddr(cur1);
            if (p1->flags.state == FREE) {
                if (accummulated_count == 0 || p1_base != expected_next_phys) {
                    candidate_result = cur1;
                    accummulated_count = PAGES_2MB_PER_1GB; // 512
                } else {
                    accummulated_count += PAGES_2MB_PER_1GB;
                }
                expected_next_phys = p1_base + _1GB_PG_SIZE;
                if (accummulated_count >= num_of_2mbpgs) {
                    result = idx_to_phyaddr(candidate_result);
                    return OS_SUCCESS;
                }
            } else if (p1->flags.state == KERNEL || p1->flags.state == USER_ANONYMOUS ||
                       p1->flags.state == USER_FILE || p1->flags.state == KERNEL_PERSIST) {
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1 + 1;
                next_idx._2mb_idx = 0;
                next_idx._4kb_idx = 0;
                accummulated_count = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
            } else { // 非法
                kputsSecure("illegal pagestate when scanning 1GB atomic entry");
                result = 0;
                return OS_MEMRY_ALLOCATE_FALT;
            }
            continue;
        } else if (p1->flags.state == FULL) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        }

        if (p1->flags.state == PARTIAL || p1->flags.state == MMIO_PARTIAL) {
            page_size2mb_t *p2base = p1->sub2mbpages;
            if (p2base == nullptr) {
                kputsSecure("Inconsistent PARTIAL 1GB without sub2mbpages");
                return OS_MEMRY_ALLOCATE_FALT;
            }

            uint16_t start2 = 0;
            if (i1 == begin._1gb_idx) start2 = begin._2mb_idx;
            for (uint16_t i2 = start2; i2 < 512; ++i2) {
                phyaddr_in_idx_t cur2;
                cur2._1gb_idx = i1;
                cur2._2mb_idx = i2;
                cur2._4kb_idx = 0;
                if (idx_to_phyaddr(cur2) >= end_phyaddr_excl) break;

                page_size2mb_t* p2 = p2base + i2;
                if (!p2->flags.is_sub_valid) { // 原子 2MB
                    phyaddr_t p2_base = idx_to_phyaddr(cur2);
                    if (p2->flags.state == FREE) {
                        if (accummulated_count == 0 || p2_base != expected_next_phys) {
                            candidate_result = cur2;
                            accummulated_count = 1;
                        } else {
                            ++accummulated_count;
                        }
                        expected_next_phys = p2_base + _2MB_PG_SIZE;
                        if (accummulated_count >= num_of_2mbpgs) {
                            result = idx_to_phyaddr(candidate_result);
                            return OS_SUCCESS;
                        }
                    } else if (p2->flags.state == KERNEL || p2->flags.state == USER_ANONYMOUS ||
                               p2->flags.state == USER_FILE || p2->flags.state == KERNEL_PERSIST) {
                        phyaddr_in_idx_t next_idx;
                        next_idx._1gb_idx = i1;
                        next_idx._2mb_idx = i2 + 1;
                        next_idx._4kb_idx = 0;
                        accummulated_count = 0;
                        expected_next_phys = idx_to_phyaddr(next_idx);
                    } else {
                        kputsSecure("illegal pagestate when scanning 2mb entry");
                        result = 0;
                        return OS_MEMRY_ALLOCATE_FALT;
                    }
                    continue;
                } else if (p2->flags.state == FULL) {
                    phyaddr_in_idx_t next_idx;
                    next_idx._1gb_idx = i1;
                    next_idx._2mb_idx = i2 + 1;
                    next_idx._4kb_idx = 0;
                    accummulated_count = 0;
                    expected_next_phys = idx_to_phyaddr(next_idx);
                } else {
                    kputsSecure("illegal pagestate when scanning 2MB entry");
                    result = 0;
                    return OS_MEMRY_ALLOCATE_FALT;
                }
            } // end for i2
        } else {
            kputsSecure("illegal pagestate when scanning 1GB entry in dram seg");
            result = 0;
            return OS_MEMRY_ALLOCATE_FALT;
        }
    } // end for i1

    return OS_MEMRY_ALLOCATE_FALT;
};

auto align1gb_pages_search = [current_seg, &result, idx_to_phyaddr, phyaddr_to_idx](uint64_t num_of_1gbpgs) -> int {
    phyaddr_t begin_phyaddr_incl = current_seg.base;
    phyaddr_t end_phyaddr_excl = current_seg.base + current_seg.seg_size;
    phyaddr_in_idx_t begin = phyaddr_to_idx(begin_phyaddr_incl);
    phyaddr_in_idx_t irritator = begin;
    phyaddr_t expected_next_phys = begin_phyaddr_incl;
    phyaddr_in_idx_t candidate_result;
    uint64_t accummulated_count = 0;

    for (uint64_t i1 = irritator._1gb_idx; ; ++i1) {
        phyaddr_in_idx_t cur1;
        cur1._1gb_idx = i1;
        cur1._2mb_idx = 0;
        cur1._4kb_idx = 0;
        if (idx_to_phyaddr(cur1) >= end_phyaddr_excl) break;

        page_size1gb_t *p1 = top_1gb_table->get(i1);
        if (p1 == nullptr) continue;

        if (!p1->flags.is_sub_valid) { // 原子 1GB
            phyaddr_t p1_base = idx_to_phyaddr(cur1);
            if (p1->flags.state == FREE) {
                if (accummulated_count == 0 || p1_base != expected_next_phys) {
                    candidate_result = cur1;
                    accummulated_count = 1;
                } else {
                    ++accummulated_count;
                }
                expected_next_phys = p1_base + _1GB_PG_SIZE;
                if (accummulated_count >= num_of_1gbpgs) {
                    result = idx_to_phyaddr(candidate_result);
                    return OS_SUCCESS;
                }
            } else if (p1->flags.state == KERNEL || p1->flags.state == USER_ANONYMOUS ||
                       p1->flags.state == USER_FILE || p1->flags.state == KERNEL_PERSIST) {
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1 + 1;
                next_idx._2mb_idx = 0;
                next_idx._4kb_idx = 0;
                accummulated_count = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
            } else { // 非法
                kputsSecure("illegal pagestate when scanning 1GB atomic entry");
                result = 0;
                return OS_MEMRY_ALLOCATE_FALT;
            }
            continue;
        } else if (p1->flags.state == FULL) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        } else {
            kputsSecure("illegal pagestate when scanning 1GB entry in dram seg");
            result = 0;
            return OS_MEMRY_ALLOCATE_FALT;
        }
    } // end for i1

    return OS_MEMRY_ALLOCATE_FALT;
};
                    
                    if(a==12)
                    {
                        status=align4kb_pages_search(numof_4kbpgs);
                        if(status==OS_SUCCESS)goto search_success;
                        else continue;
                    }else if (a==21)
                    {
                        status=align2mb_pages_search(align_up(numof_4kbpgs,PAGES_4KB_PER_2MB)/PAGES_4KB_PER_2MB);
                        if(status==OS_SUCCESS)goto search_success;
                        else continue;
                    }else if(a==30){
                        uint64_t num_of_1gbpgs=align_up(numof_4kbpgs,PAGES_4KB_PER_1GB)/PAGES_4KB_PER_1GB;
                        status=align1gb_pages_search(num_of_1gbpgs);
                        if(status==OS_SUCCESS)goto search_success;
                        else continue;
                    }else{
                        return OS_INVALID_PARAMETER;
                    }
                    search_success:
                    pages_state_set_flags_t flag={.if_init_ref_count=1,.is_blackhole_aclaim=0};
                    status=pages_state_set(result,numof_4kbpgs,state,flag);
                    
                }
            }
            
        };
        
        phyaddr_t result_addr;
        int status=inner_pages_alloc(result_addr,state);
        module_global_lock.unlock();
        if(status==OS_SUCCESS)return (phyaddr_t)result_addr;
        return 0;
    }

    module_global_lock.unlock();    
    return 0;
    
}

int phygpsmemmgr_t::pages_mmio_regist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    module_global_lock.lock();
    uint64_t out_reserveed_count=0;
    int status=scan_reserved_contiguous(phybase,numof_4kbpgs,out_reserveed_count);
    if(status!=OS_SUCCESS||out_reserveed_count!=numof_4kbpgs)
    {
        module_global_lock.unlock();
        return OS_MMIO_REGIST_FAIL;
    }
    status=inner_mmio_regist(phybase,numof_4kbpgs);
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

