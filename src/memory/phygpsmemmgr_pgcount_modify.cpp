#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "VideoDriver.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"

// 新的私有辅助函数，用于处理页面计数的增减操作
int phymemspace_mgr::_phypg_count_modify(phyaddr_t base, bool is_inc, bool is_ref_count) {
    uint64_t _1gbidx;
    uint64_t _2mboffset_idx;
    uint64_t _4kboffset_idx;
    phy_to_indices(base, _1gbidx, _2mboffset_idx, _4kboffset_idx);
    module_global_lock.lock();
    atom_page_ptr target(_1gbidx, _2mboffset_idx, _4kboffset_idx);
    phyaddr_t true_pg_base = target._1gbtb_idx * _1GB_PG_SIZE + _2MB_PG_SIZE * _2mboffset_idx + _4KB_PG_SIZE * _4kboffset_idx;
    
    if (true_pg_base != base) {
        module_global_lock.unlock();
        return OS_INVALID_ADDRESS;
    }
    
    int result = OS_SUCCESS;
    
    switch (target.page_size) {
        case _1GB_PG_SIZE: {
            page_size1gb_t& p1 = *(page_size1gb_t*)target.page_strut_ptr;
            if (is_ref_count) {
                if (is_inc) {
                    p1.ref_count++;
                } else {
                    if (p1.ref_count > 0) {
                        p1.ref_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            } else {
                if (is_inc) {
                    p1.map_count++;
                } else {
                    if (p1.map_count > 0) {
                        p1.map_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            }
            break;
        }
        case _2MB_PG_SIZE: {
            page_size2mb_t& p2 = *(page_size2mb_t*)target.page_strut_ptr;
            if (is_ref_count) {
                if (is_inc) {
                    p2.ref_count++;
                } else {
                    if (p2.ref_count > 0) {
                        p2.ref_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            } else {
                if (is_inc) {
                    p2.map_count++;
                } else {
                    if (p2.map_count > 0) {
                        p2.map_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            }
            break;
        }
        case _4KB_PG_SIZE: {
            page_size4kb_t& p4 = *(page_size4kb_t*)target.page_strut_ptr;
            if (is_ref_count) {
                if (is_inc) {
                    p4.ref_count++;
                } else {
                    if (p4.ref_count > 0) {
                        p4.ref_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            } else {
                if (is_inc) {
                    p4.map_count++;
                } else {
                    if (p4.map_count > 0) {
                        p4.map_count--;
                    } else {
                        result = OS_ALLOCATABLE_MEMORY;
                    }
                }
            }
            break;
        }
        default: {
            result = OS_INVALID_ADDRESS;
            break;
        }
    }
    
    module_global_lock.unlock();
    return result;
}

int phymemspace_mgr::phypg_refcount_dec(phyaddr_t base)
{
    return _phypg_count_modify(base, false, true);
}

int phymemspace_mgr::phypg_refcount_inc(phyaddr_t base)
{
    return _phypg_count_modify(base, true, true);
}

int phymemspace_mgr::phypg_mapcount_dec(phyaddr_t base)
{
    return _phypg_count_modify(base, false, false);
}

int phymemspace_mgr::phypg_mapcount_inc(phyaddr_t base)
{
    return _phypg_count_modify(base, true, false);
}
