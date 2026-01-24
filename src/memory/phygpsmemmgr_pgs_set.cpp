#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "util/kout.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int phymemspace_mgr::pages_state_set(phyaddr_t base,
                                    uint64_t num_of_4kbpgs,
                                    page_state_t state,
                                pages_state_set_flags_t flags)
{//todo 对新flags.is_blackhole_declaim的行为以及对
    //is_blackhole_declaim和is_blackhole_aclaim互斥合法性检验
    if (num_of_4kbpgs == 0) return 0;

    // ---- Step 1: 切段 ----
    seg_to_pages_info_package_t pak;
    int r = phymemseg_to_pacage(base, num_of_4kbpgs, pak);
    if (r != 0) return r;

    // 定义四个lambda函数替换原来的成员函数
    /**
     * 外部保证这个要拆分1GB表项的有效性
     */
auto ensure_1gb_subtable_lambda = [flags](uint64_t idx_1gb) -> int {
    page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
    if (!p1->flags.is_sub_valid)
    {
        page_size2mb_t* sub2 = new page_size2mb_t[PAGES_2MB_PER_1GB];
        if(!sub2){
            return OS_OUT_OF_MEMORY;
        }

        setmem(sub2, PAGES_2MB_PER_1GB * sizeof(page_size2mb_t), 0);

        p1->sub2mbpages = sub2;
        p1->flags.is_sub_valid = 1;
        p1->flags.state = PARTIAL;
        
        // 只有在非acclaim_backhole操作时才初始化子页状态
        if (flags.op != pages_state_set_flags_t::acclaim_backhole) {
            page_state_t state = flags.params.if_mmio ? MMIO_FREE : FREE;
            for(uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++){
                sub2[i].flags.state = state;
            }
        }
    }

    return OS_SUCCESS;
};
   auto ensure_2mb_subtable_lambda = [flags](page_size2mb_t &p2) -> int {
        if (!p2.flags.is_sub_valid)
        {
            page_size4kb_t* sub4 = new page_size4kb_t[PAGES_4KB_PER_2MB];
            if (!sub4){return OS_OUT_OF_MEMORY;}

            setmem(sub4, PAGES_4KB_PER_2MB * sizeof(page_size4kb_t), 0);

            p2.sub_pages = sub4;
            p2.flags.is_sub_valid = 1;
            p2.flags.state = PARTIAL;
            
            // 只有在非acclaim_backhole操作时才初始化子页状态
            if (flags.op != pages_state_set_flags_t::acclaim_backhole) {
                page_state_t state = flags.params.if_mmio ? MMIO_FREE : FREE;
                for(uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++) {
                    sub4[i].flags.state = state;
                }
            }
        }
        return OS_SUCCESS;
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
            if(flags.params.if_mmio)
            {
            if (st == MMIO) all_free = false;
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
            p2.flags.state = flags.params.if_mmio?MMIO_FREE:FREE;
            return 1;
        }

        if (all_full)
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
            if(flags.params.if_mmio==false)
            {
            if (p2.flags.state != FREE) all_free = false;
            }else{
                if (p2.flags.state != MMIO_FREE) all_free = false;
            }if (p2.flags.state != FULL) all_full = false;

            if (!all_free && !all_full)
                break; // 已经确定是 PARTIAL
        }

        if (all_free)
        {
            // 所有 2MB 都是 FREE：删除 1GB 的子表并恢复为原子 FREE
            delete[] p1.sub2mbpages;
            p1.sub2mbpages = nullptr;
            p1.flags.is_sub_valid = 0;
            p1.flags.state = flags.params.if_mmio?MMIO_FREE:FREE;
            return 1;
        }

        if (all_full)
        {
            // 所有 2MB 都是 FULL：标记该 1GB 为 FULL
            // **注意：不删除子表**（保留子表以便未来 refcount 增加或子页操作）
            p1.flags.state =FULL;
            // 保持 p1.flags.is_sub_valid == 1
            return 1;
        }

        // 混合：标记为 PARTIAL，保留子表
        p1.flags.state = PARTIAL;
        return 0;
    };

    auto _1gb_pages_state_set_lambda = [state,flags](uint64_t entry_base_idx, uint64_t num_of_1gbpgs) -> int {
        int status = OS_SUCCESS;
        for (uint64_t i = 0; i < num_of_1gbpgs; i++) {
            page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + i);
            if(_1==nullptr){
                if(flags.op==pages_state_set_flags_t::acclaim_backhole){
                    status=top_1gb_table->enable_idx(entry_base_idx+i);
                    if(status!=OS_SUCCESS){
                        //可能是内存不足或者参数非法越界
                        kio::bsp_kout<<"_1gb_pages_state_set:enable_idx failed for index:"<<entry_base_idx+i<<kio::kendl;
                        return OS_FAIL_PAGE_ALLOC;
                    }
                    _1=top_1gb_table->get(entry_base_idx+i);
                }else{//非黑洞模式不得创建
                    kio::bsp_kout<<"_1gb_pages_state_set:get failed for index:"<<entry_base_idx+i<<kio::kendl;
                    return OS_INVALID_FILE_MODE;
                }
            }
            _1->flags.state = state;
            if(flags.op==pages_state_set_flags_t::normal)if(flags.params.if_init_ref_count)_1->ref_count=1;
        }
        return status;
    };

    auto _4kb_pages_state_set_lambda = [state,flags](uint64_t entry4kb_base_idx, uint64_t num_of_4kbpgs, page_size4kb_t *base_entry) -> int {
        for (uint64_t i = 0; i < num_of_4kbpgs; i++) {
            auto& page_entry = base_entry[entry4kb_base_idx + i];
            page_entry.flags.state = state;
            if(flags.op==pages_state_set_flags_t::normal)if(flags.params.if_init_ref_count)page_entry.ref_count=1;
        }
        return OS_SUCCESS;
    };

    auto _2mb_pages_state_set_lambda = [state,flags](uint64_t entry2mb_base_idx, uint64_t num_of_2mbpgs, page_size2mb_t *base_entry) -> int {
        for (uint64_t i = 0; i < num_of_2mbpgs; i++) {
            auto& page_entry = base_entry[entry2mb_base_idx + i];
            page_entry.flags.state = state;
             if(flags.op==pages_state_set_flags_t::normal)if(flags.params.if_init_ref_count)page_entry.ref_count=1;
        }
        return OS_SUCCESS;
    };
    if(flags.op!=pages_state_set_flags_t::declaim_blackhole)
    {// ---- Step 2: 遍历每个段条目 ----
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entries[i];
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
            int status=0;
            page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
            if(p1==nullptr)
            {
                if(flags.op==pages_state_set_flags_t::acclaim_backhole)
                {
                    status=top_1gb_table->enable_idx(idx_1gb);
                    if(status!=OS_SUCCESS)return status;
                    p1 = top_1gb_table->get(idx_1gb);
                    p1->flags.is_sub_valid=false;
                }else{
                    kio::bsp_kout<<"_4kb_pages_state_set:attempt to create a new entry in no black hole mode"<<kio::kendl;
                    return OS_RESOURCE_CONFILICT;
                }
            }
            if((r=ensure_1gb_subtable_lambda(idx_1gb))!=OS_SUCCESS)
            return r;
            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = p1->sub2mbpages;
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
            int status=0;
            page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
            if(p1==nullptr)
            {
                if(flags.op==pages_state_set_flags_t::acclaim_backhole)
                {
                    status=top_1gb_table->enable_idx(idx_1gb);
                    if(status!=OS_SUCCESS)return status;
                    p1 = top_1gb_table->get(idx_1gb);
                    p1->flags.is_sub_valid=false;
                }else{
                    kio::bsp_kout<<"_4kb_pages_state_set:attempt to create a new entry in no black hole mode"<<kio::kendl;
                    return OS_RESOURCE_CONFILICT;
                }
            }
            if((r=ensure_1gb_subtable_lambda(idx_1gb))!=OS_SUCCESS)return r;
            page_size2mb_t &p2ent = p1->sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            if((r=ensure_2mb_subtable_lambda(p2ent))!=OS_SUCCESS)return r;
          
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            r = _4kb_pages_state_set_lambda(pg4k_start % PAGES_4KB_PER_2MB,
                                     ent.num_of_pages,
                                     p4);
            if (r != 0) return r;
                try_fold_2mb_lambda(p2ent);
                try_fold_1gb_lambda(*p1);
            
        }
    }
    }else{
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entries[i];
        if (ent.num_of_pages == 0) continue;
        
        
        uint64_t pg4k_start = ent.base >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE)
        {
            // ===== 1GB 级 =====
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            r = [idx_1gb, ent,flags]()->int {
                    for(uint64_t i=0;i<ent.num_of_pages;i++)
                        {
                        page_size1gb_t*pg=top_1gb_table->get(idx_1gb+i);
                        if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)){
                            return OS_RESOURCE_CONFILICT;
                        }
                        pg->flags.state=RESERVED;
                        pg->sub2mbpages=nullptr;
                        pg->map_count=0;
                        pg->ref_count=0;
                        top_1gb_table->release(idx_1gb+i);                        
                    }
                    return OS_SUCCESS;
                }();
            if (r != 0) return r;
        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE)
        {
            // ===== 2MB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = top_1gb_table->get(idx_1gb)->sub2mbpages;
            uint16_t real_base2mb_idx=idx_2mb % PAGES_2MB_PER_1GB;
            r = [real_base2mb_idx,ent,p2,flags]()->int {
                for(uint64_t i=0;i<ent.num_of_pages;i++)
                {
                    page_size2mb_t*pg=&p2[real_base2mb_idx+i];
                    if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)&&
                pg->flags.is_sub_valid){
                            return OS_RESOURCE_CONFILICT;
                    }
                    pg->flags.state=RESERVED;
                    pg->sub_pages=nullptr;//按照协议这里就算有指针也是无效的
                    pg->map_count=0;
                    pg->ref_count=0;
                }
                return OS_SUCCESS;
            }();
            if (r != OS_SUCCESS) return r;
        }
        
        else if (ent.page_size_in_byte == _4KB_PG_SIZE)
        {
            // ===== 4KB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            page_size1gb_t &p1 = *top_1gb_table->get(idx_1gb);
            page_size2mb_t &p2ent = p1.sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            uint16_t real_base4kb_idx=pg4k_start % PAGES_4KB_PER_2MB;
            r = [real_base4kb_idx,ent,p4,flags]()->int{
                for(uint64_t i=0;i<ent.num_of_pages;i++)
                {
                    page_size4kb_t*pg=&p4[real_base4kb_idx+i];
                    if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)&&
                pg->flags.is_sub_valid){
                            return OS_RESOURCE_CONFILICT;
                    }
                    pg->flags.state=RESERVED;
                    pg->map_count=0;
                    pg->ref_count=0;
                }
                return OS_SUCCESS;
            }();
            if (r != 0) return r;            
        }
        }

    }

    return OS_SUCCESS;
}