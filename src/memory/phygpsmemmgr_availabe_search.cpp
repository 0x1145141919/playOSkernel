#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "util/kptrace.h"
#include "util/kout.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
struct phyaddr_in_idx_t
{
    uint64_t _1gb_idx=0;
    uint16_t _2mb_idx=0;
    uint16_t _4kb_idx=0;
    
    // 添加默认构造函数
    phyaddr_in_idx_t() : _1gb_idx(0), _2mb_idx(0), _4kb_idx(0) {}
};
static phyaddr_t idx_to_phyaddr(phyaddr_in_idx_t phyaddr_in_idx){
    return (phyaddr_in_idx._1gb_idx*512*512+phyaddr_in_idx._2mb_idx*512+phyaddr_in_idx._4kb_idx)<<12;
}  

static phyaddr_in_idx_t phyaddr_to_idx(phyaddr_t phyaddr){
    phyaddr_in_idx_t phyaddr_in_idx;
    phyaddr_in_idx._4kb_idx=(phyaddr>>12)&511;
    phyaddr_in_idx._2mb_idx=(phyaddr>>21)&511;
    phyaddr_in_idx._1gb_idx=phyaddr>>30;
    return phyaddr_in_idx;
}

static bool is_idx_equal(phyaddr_in_idx_t a,phyaddr_in_idx_t b){
    return a._1gb_idx==b._1gb_idx&&a._2mb_idx==b._2mb_idx&&a._4kb_idx==b._4kb_idx;
}

// 实现类的静态成员函数
KURD_t phymemspace_mgr::align4kb_pages_search(
    const PHYSEG& current_seg,
    phyaddr_t&result_base,
    uint64_t num_of_4kbpgs)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;

    const uint64_t P4K = 1ULL << 12;

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
            kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]top_1gb_table is null in align4kb_pages_search idx = "<<cur1._1gb_idx<<kio::kendl;
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_TOP1GB_ENTRY_INVALID;
            return fatal;
        }

        if (!(p1->flags.is_sub_valid)&&!(p1->flags.is_belonged_to_buddy)) { // 原子 1GB
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
                    result_base = idx_to_phyaddr(candidate_result);
                    return success;
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
                kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]illegal pagestate when scanning 1gb atomic entry which 1gbidx:"<<cur1._1gb_idx<<kio::kendl;
                result_base = 0;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                return fatal;
            }
            continue;
        }else if ((p1->flags.state == FULL)||(p1->flags.is_belonged_to_buddy)) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        }else  if(p1->flags.state == NOT_ATOM)
        {// NOT_ATOM at 1GB: need to iterate 2MB entries inside this 1GB
        page_size2mb_t* p2base = p1->sub2mbpages;
        if (p2base == nullptr) {
            kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Inconsistent NOT_ATOM 1GB without sub2mbpages, at index: "
                         <<cur1._1gb_idx<<", base address: 0x"<<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_SUBTABLE_NULLPTR;
            return fatal;
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
            if ((!p2->flags.is_sub_valid)&&!(p2->flags.is_belonged_to_buddy)) { // 原子 2MB
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
                        result_base = idx_to_phyaddr(candidate_result);
                        return success;
                    }
                } else if (p2->flags.state == KERNEL || p2->flags.state == USER_ANONYMOUS ||
                           p2->flags.state == USER_FILE || p2->flags.state == KERNEL_PERSIST||
                        p2->flags.is_belonged_to_buddy) {
                    accummulated_count = 0;
                    phyaddr_in_idx_t next_idx;
                    next_idx._1gb_idx = i1;
                    next_idx._2mb_idx = i2 + 1;
                    next_idx._4kb_idx = 0;
                    expected_next_phys = idx_to_phyaddr(next_idx);
                    
                } else {//剩下的类型不应该出现在dram里面
                    kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Illegal page state ("<<p2->flags.state<<") when scanning 2MB atomic entry at index: "
                                 <<"1GB:"<<cur2._1gb_idx<<", 2MB:"<<cur2._2mb_idx
                                 <<", base address: "<<(void*)idx_to_phyaddr(cur2)<<kio::kendl;
                    result_base = 0;
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                    return fatal;
                }
                continue;
            }else if ((p2->flags.state == FULL)||(p2->flags.is_belonged_to_buddy)) {
                accummulated_count = 0;
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1;
                next_idx._2mb_idx = i2 + 1;
                next_idx._4kb_idx = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
            }else if(p2->flags.state == NOT_ATOM)
            {// NOT_ATOM at 2MB: iterate 4KB entries
            page_size4kb_t* p4base = p2->sub_pages;
            if (p4base == nullptr) {
                kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Inconsistent NOT_ATOM 2MB without sub_pages, at index: "
                             <<"1GB:"<<cur2._1gb_idx<<", 2MB:"<<cur2._2mb_idx
                             <<", base address: "<<(void*)idx_to_phyaddr(cur2)<<kio::kendl;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_SUBTABLE_NULLPTR;
                return fatal;
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
                    kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]4KB entry marked is_sub_valid unexpectedly, at index: "
                                 <<"1GB:"<<cur4._1gb_idx<<", 2MB:"<<cur4._2mb_idx<<", 4KB:"<<cur4._4kb_idx
                                 <<", base address: "<<(void*)idx_to_phyaddr(cur4)<<kio::kendl;
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_4KBPAGE_SUB_BIT_VALID;
                    return fatal;
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
                        result_base = idx_to_phyaddr(candidate_result);
                        return success;
                    }
                } else if (p4->flags.state == KERNEL || p4->flags.state == USER_ANONYMOUS ||
                           p4->flags.state == USER_FILE || p4->flags.state == KERNEL_PERSIST||
                        p4->flags.is_belonged_to_buddy) {
                    accummulated_count = 0;
                    expected_next_phys = p4_base + P4K;
                } else { //剩下的类型不应该出现在DRAM段里面
                    kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Illegal page state ("<<p4->flags.state<<") when scanning 4KB entry at index: "
                                 <<"1GB:"<<cur4._1gb_idx<<", 2MB:"<<cur4._2mb_idx<<", 4KB:"<<cur4._4kb_idx
                                 <<", base address: "<<(void*)idx_to_phyaddr(cur4)<<kio::kendl;
                    result_base = 0;
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                    return fatal;
                }
            } // end for i4

            }else{
                
                kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Illegal page state ("<<p2->flags.state<<") when scanning 2MB entry in dram seg, at index: "
                             <<"1GB:"<<cur2._1gb_idx<<", 2MB:"<<cur2._2mb_idx
                             <<", base address: "<<(void*)idx_to_phyaddr(cur2)<<kio::kendl;
                self_trace();
                result_base = 0;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                return fatal;
            }
        } // end for i2
        }else{
            kio::bsp_kout<<"[phymemspace_mgr::align4kb_pages_search]Illegal page state ("<<p1->flags.state<<") when scanning 1GB entry in dram seg, at index: "
                         <<cur1._1gb_idx<<", base address: "<<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
            result_base = 0;
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
            return fatal;
        }
    } // end for i1

    // 未找到连续区
    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NO_ENOUGH_MEMORY;
    return fail;
}

KURD_t phymemspace_mgr::align2mb_pages_search(
    const PHYSEG& current_seg,
    phyaddr_t&result_base,
    uint64_t num_of_2mbpgs)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;

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

        if (!(p1->flags.is_sub_valid) && !(p1->flags.is_belonged_to_buddy)) { // 原子 1GB
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
                    result_base = idx_to_phyaddr(candidate_result);
                    return success;
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
                kio::bsp_kout<<"[phymemspace_mgr::align2mb_pages_search]Illegal page state when scanning 1GB atomic entry, at index: "
                             <<cur1._1gb_idx<<", base address: "
                             <<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
                result_base = 0;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                return fatal;
            }
            continue;
        } else if (p1->flags.state == FULL||(p1->flags.is_belonged_to_buddy)) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        }else  if (p1->flags.state == NOT_ATOM) {
            page_size2mb_t *p2base = p1->sub2mbpages;
            if (p2base == nullptr) {
                kio::bsp_kout<<"[phymemspace_mgr::align2mb_pages_search]Inconsistent NOT_ATOM 1GB without sub2mbpages, at index: "
                             <<cur1._1gb_idx<<", base address: "
                             <<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_SUBTABLE_NULLPTR;
                return fatal;
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
                if (!p2->flags.is_sub_valid && !(p2->flags.is_belonged_to_buddy)) { // 原子 2MB
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
                            result_base = idx_to_phyaddr(candidate_result);
                            return success;
                        }
                    } else if (p2->flags.state == KERNEL || p2->flags.state == USER_ANONYMOUS ||
                               p2->flags.state == USER_FILE || p2->flags.state == KERNEL_PERSIST||
                               p2->flags.state == NOT_ATOM || p2->flags.is_belonged_to_buddy) {
                        phyaddr_in_idx_t next_idx;
                        next_idx._1gb_idx = i1;
                        next_idx._2mb_idx = i2 + 1;
                        next_idx._4kb_idx = 0;
                        accummulated_count = 0;
                        expected_next_phys = idx_to_phyaddr(next_idx);
                    } else {
                        kio::bsp_kout<<"[phymemspace_mgr::align2mb_pages_search]Illegal page state ("<<p2->flags.state<<") when scanning 2MB entry at index: "
                                     <<"1GB:"<<cur2._1gb_idx<<", 2MB:"<<cur2._2mb_idx
                                     <<", base address: "<<(void*)idx_to_phyaddr(cur2)<<kio::kendl;
                        result_base = 0;
                        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                        return fatal;
                    }
                    continue;
                } else if (p2->flags.state == FULL||p2->flags.state == NOT_ATOM||p2->flags.is_belonged_to_buddy) {
                    phyaddr_in_idx_t next_idx;
                    next_idx._1gb_idx = i1;
                    next_idx._2mb_idx = i2 + 1;
                    next_idx._4kb_idx = 0;
                    accummulated_count = 0;
                    expected_next_phys = idx_to_phyaddr(next_idx);
                } else {
                    kio::bsp_kout<<"[phymemspace_mgr::align2mb_pages_search]Illegal page state when scanning 2MB entry, at index: "
                                 <<"1GB:"<<cur1._1gb_idx<<", 2MB:"<<cur2._2mb_idx
                                 <<", base address: "<<(void*)idx_to_phyaddr(cur2)<<kio::kendl; 
                    result_base = 0;
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                    return fatal;
                }
            } // end for i2
        } else {
            kio::bsp_kout<<"[phymemspace_mgr::align2mb_pages_search]Illegal page state when scanning 1GB entry in dram seg, at index: "
                         <<cur1._1gb_idx<<", base address: "
                         <<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
            result_base = 0;
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
            return fatal;
        }
    } // end for i1

    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NO_ENOUGH_MEMORY;
    return fail;
}

KURD_t phymemspace_mgr::align1gb_pages_search(
    const PHYSEG& current_seg,
    phyaddr_t&result_base,
    uint64_t num_of_1gbpgs)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_ALIGN_SEARCHES;

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

        if (!p1->flags.is_sub_valid && !(p1->flags.is_belonged_to_buddy)) { // 原子 1GB
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
                    result_base = idx_to_phyaddr(candidate_result);
                    return success;
                }
            } else if (p1->flags.state == KERNEL || p1->flags.state == USER_ANONYMOUS ||
                       p1->flags.state == USER_FILE || p1->flags.state == KERNEL_PERSIST||p1->flags.is_belonged_to_buddy) {
                phyaddr_in_idx_t next_idx;
                next_idx._1gb_idx = i1 + 1;
                next_idx._2mb_idx = 0;
                next_idx._4kb_idx = 0;
                accummulated_count = 0;
                expected_next_phys = idx_to_phyaddr(next_idx);
            } else { // 非法
                kio::bsp_kout<<"[phymemspace_mgr::align1gb_pages_search]Illegal page state when scanning 1GB atomic entry, at index: "
                             <<cur1._1gb_idx<<", base address: "
                             <<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
                result_base = 0;
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
                return fatal;
            }
            continue;
        } else if (p1->flags.state == FULL|| p1->flags.state == NOT_ATOM||p1->flags.is_belonged_to_buddy) {
            phyaddr_in_idx_t next_idx;
            next_idx._1gb_idx = i1 + 1;
            next_idx._2mb_idx = 0;
            next_idx._4kb_idx = 0;
            accummulated_count = 0;
            expected_next_phys = idx_to_phyaddr(next_idx);
            continue;
        } else {
            kio::bsp_kout<<"[phymemspace_mgr::align1gb_pages_search]Illegal page state when scanning 1GB entry in dram seg, at index: "
                         <<cur1._1gb_idx<<", base address: "
                         <<(void*)idx_to_phyaddr(cur1)<<kio::kendl;
            result_base = 0;
            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FATAL_REASONS::REASON_CODE_ILLAGLE_DRAM_PAGE_TYPE;
            return fatal;
        }
    } // end for i1

    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::ALIGN_SEARCHES_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NO_ENOUGH_MEMORY;
    return fail;
}
