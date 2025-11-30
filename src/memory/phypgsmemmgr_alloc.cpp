#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int phygpsmemmgr_t::pages_state_set(phyaddr_t base,
                                    uint64_t num_of_4kbpgs,
                                    page_state_t state,
                                bool if_inc)
{
    if (num_of_4kbpgs == 0) return 0;

    // ---- Step 1: 切段 ----
    seg_to_pages_info_pakage_t pak;
    int r = phymemseg_to_pacage(base, num_of_4kbpgs, pak);
    if (r != 0) return r;

    // ---- Step 2: 遍历每个段条目 ----
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entryies[i];
        if (ent.num_of_pages == 0) continue;

        uint64_t pg4k_start = (ent.base - this->base) >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE)
        {
            // ===== 1GB 级 =====
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            r = _1gb_pages_state_set(idx_1gb, ent.num_of_pages, state,if_inc);
            if (r != 0) return r;
        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE)
        {
            // ===== 2MB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            if((r=ensure_1gb_subtable(idx_1gb))!=OS_SUCCESS)
            return r;

            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = top_1gb_table[idx_1gb].sub2mbpages;
            r = _2mb_pages_state_set(idx_2mb % PAGES_2MB_PER_1GB,
                                     ent.num_of_pages,
                                     state,
                                     p2,if_inc);
            if (r != OS_SUCCESS) return r;
            try_fold_1gb(top_1gb_table[idx_1gb]);
        }
        
        else if (ent.page_size_in_byte == _4KB_PG_SIZE)
        {
            // ===== 4KB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            page_size1gb_t &p1 = top_1gb_table[idx_1gb];
            if((r=ensure_1gb_subtable(idx_1gb))!=OS_SUCCESS)return r;
            page_size2mb_t &p2ent = p1.sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            if((r=ensure_2mb_subtable(p2ent))!=OS_SUCCESS)return r;
          
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            r = _4kb_pages_state_set(pg4k_start % PAGES_4KB_PER_2MB,
                                     ent.num_of_pages,
                                     state,
                                     p4,if_inc);
            if (r != 0) return r;
                try_fold_2mb(p2ent);
                try_fold_1gb(p1);
            
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
phyaddr_t phygpsmemmgr_t::pages_alloc(uint64_t numof_4kbpgs, page_state_t state, uint8_t align_log2)
{
    if(state==KERNEL||state==USER_ANONYMOUS||state==USER_FILE)
    {
        module_global_lock.lock();
        phyaddr_t result_addr;
        int status=inner_pages_alloc(result_addr,numof_4kbpgs,state,align_log2);
        module_global_lock.unlock();
        if(status==OS_SUCCESS)return (phyaddr_t)result_addr;
        return 0;
    }
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
int phygpsmemmgr_t::align2mb_pages_search(
    phyaddr_t &result_base,
    uint64_t numof_2mbpgs
) {
    phyaddr_t start_addr = base;
    uint64_t accmulated_2mbpgs=0;
    bool is_now_seg_free=false;
    for(uint64_t i=0;i<seg_support_1gb_page_count;i++)
    {
        page_size1gb_t& p1=top_1gb_table[i];
        if(p1.flags.is_sub_valid){
            // 如果1GB页面有子表(已分解为2MB页面)
            if(p1.flags.state==FULL||p1.flags.state==RESERVED_MMIO){
                is_now_seg_free=false;
            }
            else{
                // 遍历该1GB页面下的所有2MB页面
                for(uint64_t j=0;j<PAGES_2MB_PER_1GB;j++){
                    page_size2mb_t& p2=p1.sub2mbpages[j];
                    if(p2.flags.state==FREE){
                        // 如果当前2MB页面空闲
                        if(is_now_seg_free){
                            accmulated_2mbpgs++;
                            if(accmulated_2mbpgs>=numof_2mbpgs)
                            {
                                result_base=start_addr;
                                return OS_SUCCESS;
                            }else{
                                continue;
                            }
                        }else{
                            is_now_seg_free=true;
                            accmulated_2mbpgs=0;
                            start_addr=base+i*_1GB_PG_SIZE+j*_2MB_PG_SIZE;
                        }
                    }else{
                        is_now_seg_free=false;

                    }
                }
            }
        }else{
            // 如果1GB页面没有子表(仍为原子页)
            if(is_now_seg_free){
                accmulated_2mbpgs+=PAGES_2MB_PER_1GB;
                if(accmulated_2mbpgs>=numof_2mbpgs)
                    {
                        result_base=start_addr;
                        return OS_SUCCESS;
                    }else{
                        continue;
                    }
            }else{
                is_now_seg_free=true;
                accmulated_2mbpgs=0;
                start_addr=base+i*_1GB_PG_SIZE;
            }
        }
    }
    return OS_MEMRY_ALLOCATE_FALT; // 未找到
}
static inline uint8_t normalize_align_log2(uint8_t log2)
{
    if (log2 < 12) return 12;          // 最小对齐是 4KB
    if (log2 <= 20) return 21;         // 12~20 → 上调到 2MB
    if (log2 <= 29) return 30;         // 21~29 → 上调到 1GB

    // 30以上按1GB处理（你的要求）
    return 30;
}
int phygpsmemmgr_t::inner_pages_alloc(
    phyaddr_t &result_base,
    uint64_t num_of_4kbpgs,
    page_state_t state,
    uint8_t align_log2
){
    if (num_of_4kbpgs == 0)
        return OS_MEMRY_ALLOCATE_FALT;
    if(align_log2 > 30)return OS_INVALID_PARAMETER;
    // 自动向上取最近对齐粒度
    uint8_t a = normalize_align_log2(align_log2);
    int status;
    phyaddr_t result = 0;

    switch (a) {

    case 12:  // 4KB
        if (align4kb_pages_search(result, num_of_4kbpgs) == OS_SUCCESS)
            {
                status=pages_state_set(result, num_of_4kbpgs, state,true);
                result_base=result;
                return OS_SUCCESS;
            }
        return OS_MEMRY_ALLOCATE_FALT;

    case 21:  // 2MB


        {
            uint64_t n_2mb = (num_of_4kbpgs+PAGES_4KB_PER_2MB-1)/PAGES_4KB_PER_2MB;
            if (align2mb_pages_search(result, n_2mb) == OS_SUCCESS)
                {
                    status=pages_state_set(result,num_of_4kbpgs, state,true);
                    result_base=result;
                    return OS_SUCCESS;
                }
            return OS_MEMRY_ALLOCATE_FALT;
        }

    case 30:  // 1GB
        {
            const uint64_t FOURKB_PER_1GB = 512ULL * 512ULL;
            if (num_of_4kbpgs % FOURKB_PER_1GB != 0)
                return OS_MEMRY_ALLOCATE_FALT;

            uint64_t n_1gb = num_of_4kbpgs / FOURKB_PER_1GB;

            if (align1gb_pages_search(result, n_1gb) == OS_SUCCESS)
                {
                    status=pages_state_set(result,num_of_4kbpgs, state,true);
                    result_base=result;
                    return OS_SUCCESS;
                }

            return OS_MEMRY_ALLOCATE_FALT;
        }

    default:
        return OS_INVALID_PARAMETER;
    }
}

int phygpsmemmgr_t::align4kb_pages_search(
    phyaddr_t &result_base,
    uint64_t numof_4kbpgs
) {
    if (numof_4kbpgs == 0) return OS_MEMRY_ALLOCATE_FALT;
    atom_page_ptr ptr(0, 0, 256); // 从1mb开始
    uint64_t contiguous = 0;
    uint64_t start_addr = 0;
    bool in_run = false;
    
    // 预计算常量以避免重复计算
    const uint64_t FOURKB_PER_1GB = (uint64_t)PAGES_2MB_PER_1GB * 512ULL;
    
    while (true) {
        // 判断 ptr 指向的"原子页"种类并按 4KB 粒度解释
        if (ptr.page_size == _4KB_PG_SIZE) {
            page_size4kb_t *p4 = (page_size4kb_t*) ptr.page_strut_ptr;
            if (p4->flags.state == FREE) {
                if (!in_run) {
                    // 记录连续区起点（当前原子页的物理地址）
                    start_addr = (uint64_t)base
                               + (uint64_t)ptr._1gbtb_idx * _1GB_PG_SIZE
                               + (uint64_t)ptr._2mbtb_offestidx * _2MB_PG_SIZE
                               + (uint64_t)ptr._4kb_offestidx * _4KB_PG_SIZE;
                    in_run = true;
                    contiguous = 0;
                }
                // 增加 1 个 4KB 原子页
                contiguous += 1;
                if (contiguous >= numof_4kbpgs) {
                    result_base = (phyaddr_t) start_addr;
                    return OS_SUCCESS;
                }
            } else {
                // 非 FREE：断开连续段
                in_run = false;
                contiguous = 0;
            }
        }
        else if (ptr.page_size == _2MB_PG_SIZE) {
            // 这是一个 2MB 原子页（构造器保证此时没有下级 table）
            page_size2mb_t *p2 = (page_size2mb_t*) ptr.page_strut_ptr;
            if (p2->flags.state == FREE) {
                // 2MB 原子页 == 512 * 4KB 原子页
                if (!in_run) {
                    start_addr = (uint64_t)base
                               + (uint64_t)ptr._1gbtb_idx * _1GB_PG_SIZE
                               + (uint64_t)ptr._2mbtb_offestidx * _2MB_PG_SIZE;
                    in_run = true;
                    contiguous = 0;
                }
                // 增加 512 个 4KB 单位
                contiguous += 512;
                if (contiguous >= numof_4kbpgs) {
                    result_base = (phyaddr_t) start_addr;
                    return OS_SUCCESS;
                }
            } else {
                // 非 FREE -> 中断连续
                in_run = false;
                contiguous = 0;
            }
        }
        else if (ptr.page_size == _1GB_PG_SIZE) {
            // 这是一个 1GB 原子页（构造器保证此时没有下级 table）
            page_size1gb_t *p1 = (page_size1gb_t*) ptr.page_strut_ptr;
            if (p1->flags.state == FREE) {
                // 1GB 原子页 == 512 * 2MB == 512*512 * 4KB
                if (!in_run) {
                    start_addr = (uint64_t)base + (uint64_t)ptr._1gbtb_idx * _1GB_PG_SIZE;
                    in_run = true;
                    contiguous = 0;
                }
                contiguous += FOURKB_PER_1GB;
                if (contiguous >= numof_4kbpgs) {
                    result_base = (phyaddr_t) start_addr;
                    return OS_SUCCESS;
                }
            } else {
                in_run = false;
                contiguous = 0;
            }
        }
        else {
            // 非法 page_size（不应出现）
            in_run = false;
            contiguous = 0;
        }

        // 前进到下一个原子页；the_next 返回非 0 表示出界或错误
        int nxt = ptr.the_next();
        if (nxt != 0) {
            return OS_MEMRY_ALLOCATE_FALT;
        }
    }

    return OS_UNREACHABLE_CODE;
}
int phygpsmemmgr_t::align1gb_pages_search(
    phyaddr_t & result_base,
    uint64_t numof_1gbpgs
){
    if (numof_1gbpgs == 0) return -1;

    uint64_t cnt = 0;
    uint64_t start = 0;

    // 假定已经在全局锁内部调用
    for(uint32_t i = 0; i < seg_support_1gb_page_count; i++){
        page_size1gb_t &p1 = top_1gb_table[i];

        if(p1.flags.state == FREE){
            if(cnt == 0) start = i;
            cnt++;

            if(cnt == numof_1gbpgs){
                // 成功
                result_base = base+(phyaddr_t)start *_1GB_PG_SIZE;
                return OS_SUCCESS;
            }
        }else{
            // USED / SPLIT / PARTIAL 全都当作不可用
            cnt = 0;
        }
    }

    return OS_MEMRY_ALLOCATE_FALT; // 没找到
}