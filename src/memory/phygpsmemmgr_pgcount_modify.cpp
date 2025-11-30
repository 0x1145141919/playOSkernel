#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
int phygpsmemmgr_t::phypg_refcount_dec(phyaddr_t base)
{
    uint64_t _1gbidx;
    uint64_t _2mboffset_idx;
    uint64_t _4kboffset_idx;
    phy_to_indices(base,_1gbidx,_2mboffset_idx,_4kboffset_idx);
    module_global_lock.lock();
    atom_page_ptr target(_1gbidx,_2mboffset_idx,_4kboffset_idx);
    phyaddr_t  true_pg_base=this->base+target._1gbtb_idx*_1GB_PG_SIZE+_2MB_PG_SIZE*_2mboffset_idx+_4KB_PG_SIZE*_4kboffset_idx;
    if(true_pg_base==base){
        switch(target.page_size)
        {
            case _1GB_PG_SIZE:{
                page_size1gb_t&p1 =*(page_size1gb_t*)target.page_strut_ptr;
                if(p1.ref_count>0)p1.ref_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
            case _2MB_PG_SIZE:
            {
                page_size2mb_t&p2 =*(page_size2mb_t*)target.page_strut_ptr;
                if(p2.ref_count>0)p2.ref_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
            case _4KB_PG_SIZE:
            {
                page_size4kb_t&p4 =*(page_size4kb_t*)target.page_strut_ptr;
                if(p4.ref_count>0)p4.ref_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
        }
    }else{
        module_global_lock.unlock();
        return OS_INVALID_ADDRESS;
    }
    module_global_lock.unlock();
    return OS_SUCCESS;
}
int phygpsmemmgr_t::phypg_refcount_inc(phyaddr_t base)
{
    uint64_t _1gbidx;
    uint64_t _2mboffset_idx;
    uint64_t _4kboffset_idx;
    phy_to_indices(base,_1gbidx,_2mboffset_idx,_4kboffset_idx);
    module_global_lock.lock();
    atom_page_ptr target(_1gbidx,_2mboffset_idx,_4kboffset_idx);
    phyaddr_t  true_pg_base=this->base+target._1gbtb_idx*_1GB_PG_SIZE+_2MB_PG_SIZE*_2mboffset_idx+_4KB_PG_SIZE*_4kboffset_idx;
    if(true_pg_base==base){
        switch(target.page_size)
        {
            case _1GB_PG_SIZE:{
                page_size1gb_t&p1 =*(page_size1gb_t*)target.page_strut_ptr;
                p1.ref_count++;
                break; // 添加缺失的 break
            }
            case _2MB_PG_SIZE:
            {
                page_size2mb_t&p2 =*(page_size2mb_t*)target.page_strut_ptr;
                p2.ref_count++;  
                break; // 添加缺失的 break
            }
            case _4KB_PG_SIZE:
            {
                page_size4kb_t&p4 =*(page_size4kb_t*)target.page_strut_ptr;
                p4.ref_count++;
                break; // 添加缺失的 break
            }
        }
    }else{
        module_global_lock.unlock();
        return OS_INVALID_ADDRESS;
    }
    module_global_lock.unlock();
    return OS_SUCCESS;
}
int phygpsmemmgr_t::phypg_mapcount_dec(phyaddr_t base)
{
        uint64_t _1gbidx;
    uint64_t _2mboffset_idx;
    uint64_t _4kboffset_idx;
    phy_to_indices(base,_1gbidx,_2mboffset_idx,_4kboffset_idx);
    module_global_lock.lock();
    atom_page_ptr target(_1gbidx,_2mboffset_idx,_4kboffset_idx);
    phyaddr_t  true_pg_base=this->base+target._1gbtb_idx*_1GB_PG_SIZE+_2MB_PG_SIZE*_2mboffset_idx+_4KB_PG_SIZE*_4kboffset_idx;
    if(true_pg_base==base){
        switch(target.page_size)
        {
            case _1GB_PG_SIZE:{
                page_size1gb_t&p1 =*(page_size1gb_t*)target.page_strut_ptr;
                if(p1.map_count>0)p1.map_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
            case _2MB_PG_SIZE:
            {
                page_size2mb_t&p2 =*(page_size2mb_t*)target.page_strut_ptr;
                if(p2.map_count>0)p2.map_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
            case _4KB_PG_SIZE:
            {
                page_size4kb_t&p4 =*(page_size4kb_t*)target.page_strut_ptr;
                if(p4.map_count>0)p4.map_count--;
                else{
                     module_global_lock.unlock();
                     return OS_ALLOCATABLE_MEMORY;
                }
                break; // 添加缺失的 break
            }
        }
    }else{
        module_global_lock.unlock();
        return OS_INVALID_ADDRESS;
    }
    module_global_lock.unlock();
    return OS_SUCCESS;
}
int phygpsmemmgr_t::phypg_mapcount_inc(phyaddr_t base)
{
    uint64_t _1gbidx;
    uint64_t _2mboffset_idx;
    uint64_t _4kboffset_idx;
    phy_to_indices(base,_1gbidx,_2mboffset_idx,_4kboffset_idx);
    module_global_lock.lock();
    atom_page_ptr target(_1gbidx,_2mboffset_idx,_4kboffset_idx);
    phyaddr_t  true_pg_base=this->base+target._1gbtb_idx*_1GB_PG_SIZE+_2MB_PG_SIZE*_2mboffset_idx+_4KB_PG_SIZE*_4kboffset_idx;
    if(true_pg_base==base){
        switch(target.page_size)
        {
            case _1GB_PG_SIZE:{
                page_size1gb_t&p1 =*(page_size1gb_t*)target.page_strut_ptr;
                p1.map_count++;
                break; // 添加缺失的 break
            }
            case _2MB_PG_SIZE:
            {
                page_size2mb_t&p2 =*(page_size2mb_t*)target.page_strut_ptr;
                p2.map_count++;  
                break; // 添加缺失的 break
            }
            case _4KB_PG_SIZE:
            {
                page_size4kb_t&p4 =*(page_size4kb_t*)target.page_strut_ptr;
                p4.map_count++;
                break; // 添加缺失的 break
            }
        }
    }else{
        module_global_lock.unlock();
        return OS_INVALID_ADDRESS;
    }
    module_global_lock.unlock();
    return OS_SUCCESS;
}