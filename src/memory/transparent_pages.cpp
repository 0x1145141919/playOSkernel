#include "memory/phygpsmemmgr.h"
#include "abi/os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "memory/phyaddr_accessor.h"
#include "util/kout.h"
#include "panic.h"
#include "util/kptrace.h"
uint64_t phymemspace_mgr::page_head(uint64_t idx)
{
    return mem_map[idx].page_flags.bitfield.is_skipped ? mem_map[idx].tranp.head_page : (~0ull);
}
uint64_t phymemspace_mgr::page_size(uint64_t idx)
{
    return !mem_map[idx].page_flags.bitfield.is_skipped ? 1ull<<(12+mem_map[idx].head.order) : (~0ull);
}
KURD_t phymemspace_mgr::page_spilt(uint64_t idx, uint8_t target_order)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_TRANSPARNENT_PAGE,
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::EVENT_CODE_SPILT,
            level_code::INFO, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.reason = reason;
        return kurd;
    };
    auto make_fatal = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_fatal_result_level(default_kurd());
        kurd.reason = reason;
        return kurd;
    };

    // 参数验证
    if (target_order > 63) {  // order 最大支持值
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_TARGET_ORDER
        );
    }
    if (idx >= mem_map_entry_count) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 如果当前是透明页（跳过页），先获取头页
    uint64_t head_idx = idx;
    if (mem_map[idx].page_flags.bitfield.is_skipped) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::NOT_HEAD_PAGE
        );
    }
    
    uint8_t current_order = mem_map[head_idx].head.order;
    
    // 如果目标 order 大于等于当前 order，无需拆分或参数错误
    if (target_order >= current_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TARGET_ORDER_NOT_SMALLER
        );
    }
    
    // 验证索引对齐要求
    uint8_t idx_allow_max_order = __builtin_ctzll(head_idx);
    if (idx_allow_max_order < target_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INDEX_ALIGN_TOO_SMALL
        );
    }
    
    // 一致性检查
    if (idx_allow_max_order < current_order) {
        return make_fatal(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FATAL_REASONS_CODE::CONSISTENCY_VIOLATION
        );
    }

    uint64_t current_span = 1ULL << current_order;
    if (current_span > mem_map_entry_count || head_idx > mem_map_entry_count - current_span) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::SPILT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 计算需要拆分的页数：从 current_order 拆分到 target_order
    // 原来 1 个 current_order 的大页，拆分成 2^(current_order - target_order) 个 target_order 的小页
    uint64_t num_pages = 1ULL << (current_order - target_order);    
    page new_head = mem_map[head_idx];
    new_head.head.order = target_order;
    
    // 批量设置所有拆分后的页面
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t new_head_idx = head_idx + (i << target_order);
        
        // 设置新的头页
        mem_map[new_head_idx] = new_head;
        
        // 设置该头页包含的所有透明页
        for (uint64_t j = 0; j < (1ULL << target_order); j++) {
            mem_map[new_head_idx + j].tranp.head_page = new_head_idx;
            mem_map[new_head_idx + j].page_flags.bitfield.is_skipped = 1;
        }
    }
    
    return make_success();
}
KURD_t phymemspace_mgr::page_merge_identical(uint64_t head_idx, uint8_t target_order)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_TRANSPARNENT_PAGE,
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::EVENT_CODE_MERGE,
            level_code::INFO, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.reason = reason;
        return kurd;
    };

    // 参数验证
    if (target_order > 63) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_TARGET_ORDER
        );
    }
    if (head_idx >= mem_map_entry_count) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 如果当前是透明页，先获取头页
    if (mem_map[head_idx].page_flags.bitfield.is_skipped) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::NOT_HEAD_PAGE
        );
    }
    if (head_idx >= mem_map_entry_count) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    uint8_t current_order = mem_map[head_idx].head.order;
    
    // 如果目标 order 小于等于当前 order，无法合并或无需合并
    if (target_order <= current_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TARGET_ORDER_NOT_GREATER
        );
    }
    
    // 验证索引对齐要求
    uint8_t idx_allow_max_order = __builtin_ctzll(head_idx);
    if (idx_allow_max_order < target_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INDEX_ALIGN_TOO_SMALL
        );
    }

    uint64_t target_span = 1ULL << target_order;
    if (target_span > mem_map_entry_count || head_idx > mem_map_entry_count - target_span) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 计算需要合并的头页数量：2^(target_order - current_order)
    uint64_t num_heads_to_merge = 1ULL << (target_order - current_order);
    
    // ========== 第一阶段：扫描验证 ==========
    
    // 保存第一个头页的信息作为比对基准
    page first_head = mem_map[head_idx];
    uint64_t first_type = first_head.head.type;
    // ptr 是每个页的独立属性，不需要连续，只需验证其他属性
    
    // 验证所有待合并的头页
    for (uint64_t i = 0; i < num_heads_to_merge; i++) {
        uint64_t current_head_idx = head_idx + (i << current_order);
        
        if (current_head_idx >= mem_map_entry_count) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
            );
        }
        
        page current_page = mem_map[current_head_idx];
        
        // 检查是否是头页（is_skipped 应该为 0）
        if (current_page.page_flags.bitfield.is_skipped) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_HEAD_PAGE
            );
        }
        
        // 检查 order 是否匹配
        if (current_page.head.order != current_order) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_ORDER_MISMATCH
            );
        }
        
        // 检查 type 是否相同
        if (current_page.head.type != first_type) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TYPE_MISMATCH
            );
        }
        
        // ptr 是每个页的独立属性，不需要验证连续性
        
        // 检查该头页下的所有透明页是否合法
        for (uint64_t j = 1; j < (1ULL << current_order); j++) {
            uint64_t page_idx = current_head_idx + j;
            
            if (page_idx >= mem_map_entry_count) {
                return make_fail(
                    MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
                );
            }
            
            page trans_page = mem_map[page_idx];
            
            // 检查是否是透明页
            if (!trans_page.page_flags.bitfield.is_skipped) {
                return make_fail(
                    MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TRANSPARENT_PAGE_INVALID
                );
            }
            
            // 检查 head_page 指针是否正确指向当前头页
            if (trans_page.tranp.head_page != current_head_idx) {
                return make_fail(
                    MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_HEAD_PTR_MISMATCH
                );
            }
        }
    }
    
    // ========== 第二阶段：执行合并 ==========
    
    // 设置新的头页信息
    mem_map[head_idx].head.order = target_order;
    // type 和 ptr 保持不变（ptr 是该页的独立属性）
    
    // 将该头页包含的所有页面设置为透明页
    for (uint64_t j = 1; j < (1ULL << target_order); j++) {
        uint64_t page_idx = head_idx + j;
        
        if (page_idx >= mem_map_entry_count) {
            break;
        }
        
        mem_map[page_idx].page_flags.bitfield.is_skipped = 1;
        mem_map[page_idx].tranp.head_page = head_idx;
    }
    
    return make_success();
}

KURD_t phymemspace_mgr::page_merge_freedram(uint64_t head_idx, uint8_t target_order)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_TRANSPARNENT_PAGE,
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::EVENT_CODE_MERGE_FREE,
            level_code::INFO, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.reason = reason;
        return kurd;
    };

    // 参数验证
    if (target_order > 63) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_TARGET_ORDER
        );
    }
    if (head_idx >= mem_map_entry_count) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 如果当前是透明页，先获取头页
    if (mem_map[head_idx].page_flags.bitfield.is_skipped) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_HEAD_PAGE
        );
    }
    if (head_idx >= mem_map_entry_count) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    uint8_t current_order = mem_map[head_idx].head.order;
    
    // 如果目标 order 小于等于当前 order，无法合并或无需合并
    if (target_order <= current_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TARGET_ORDER_NOT_GREATER
        );
    }
    
    // 验证索引对齐要求
    uint8_t idx_allow_max_order = __builtin_ctzll(head_idx);
    if (idx_allow_max_order < target_order) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INDEX_ALIGN_TOO_SMALL
        );
    }

    uint64_t target_span = 1ULL << target_order;
    if (target_span > mem_map_entry_count || head_idx > mem_map_entry_count - target_span) {
        return make_fail(
            MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
        );
    }
    
    // 计算需要合并的头页数量：2^(target_order - current_order)
    uint64_t num_heads_to_merge = 1ULL << (target_order - current_order);
    
    // ========== 第一阶段：扫描验证所有待合并页是否为空闲 DRAM ==========
    
    for (uint64_t i = 0; i < num_heads_to_merge; i++) {
        uint64_t current_head_idx = head_idx + (i << current_order);
        
        if (current_head_idx >= mem_map_entry_count) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
            );
        }
        
        page current_page = mem_map[current_head_idx];
        
        // 检查是否是头页（is_skipped 应该为 0）
        if (current_page.page_flags.bitfield.is_skipped) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_HEAD_PAGE
            );
        }
        
        // 检查 order 是否匹配
        if (current_page.head.order != current_order) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_ORDER_MISMATCH
            );
        }
        
        // 检查 type 是否为 free
        if (current_page.head.type != static_cast<uint64_t>(page_state_t::free)) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_FREE
            );
        }
        
        // 检查 is_allocatable 是否为 1
        if (!current_page.page_flags.bitfield.is_allocateble) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_ALLOCATABLE
            );
        }
        
        // 检查 refcount 是否为 0
        if (current_page.refcount != 0) {
            return make_fail(
                MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_REFCOUNT_NONZERO
            );
        }
        
        // 如果当前页是透明页头页，还需要验证其下的所有透明页
        if ((1ULL << current_order) > 1) {
            for (uint64_t j = 1; j < (1ULL << current_order); j++) {
                uint64_t page_idx = current_head_idx + j;
                
                if (page_idx >= mem_map_entry_count) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_OUT_OF_RANGE
                    );
                }
                
                page trans_page = mem_map[page_idx];
                
                // 检查是否是透明页
                if (!trans_page.page_flags.bitfield.is_skipped) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_TRANSPARENT_PAGE_INVALID
                    );
                }
                
                // 检查 head_page 指针是否正确指向当前头页
                if (trans_page.tranp.head_page != current_head_idx) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_HEAD_PTR_MISMATCH
                    );
                }
                
                // 检查 type 是否为 free
                if (trans_page.tranp.type != static_cast<uint64_t>(page_state_t::free)) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NOT_FREE
                    );
                }
                
                // 检查 huge_order 是否与当前 order 一致
                if (trans_page.tranp.huge_order != current_order) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_HUGE_ORDER_MISMATCH
                    );
                }
                
                // 检查 refcount 是否为 0
                if (trans_page.refcount != 0) {
                    return make_fail(
                        MEMMODULE_LOCAIONS::TRANSPARNENT_PAGE_EVENTS::MERGE_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_REFCOUNT_NONZERO
                    );
                }
            }
        }
    }
    
    // ========== 第二阶段：执行合并 ==========
    
    // 设置新的头页信息
    mem_map[head_idx].head.order = target_order;
    mem_map[head_idx].head.type = static_cast<uint64_t>(page_state_t::free);  // 保持为 free
    mem_map[head_idx].page_flags.bitfield.is_allocateble = 1;
    mem_map[head_idx].refcount = 0;
    // ptr 保持不变（是该页的独立属性）
    
    // 将该头页包含的所有页面设置为透明页
    for (uint64_t j = 1; j < (1ULL << target_order); j++) {
        uint64_t page_idx = head_idx + j;
        
        if (page_idx >= mem_map_entry_count) {
            break;
        }
        
        mem_map[page_idx].page_flags.bitfield.is_skipped = 1;
        mem_map[page_idx].page_flags.bitfield.is_allocateble = 1;
        mem_map[page_idx].tranp.head_page = head_idx;
        mem_map[page_idx].tranp.type = static_cast<uint64_t>(page_state_t::free);
        mem_map[page_idx].tranp.huge_order = target_order;
    }
    
    return make_success();
}
