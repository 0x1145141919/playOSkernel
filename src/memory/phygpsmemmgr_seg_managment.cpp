#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
KURD_t phymemspace_mgr::PHYSEG_LIST_ITEM::add_seg(PHYSEG &seg)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;

    // 空链表
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        if (!push_front(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 插入头部之前
    if ((seg.base + seg.seg_size) <= m_head->value.base) {
        if (!push_front(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 插入尾部之后
    if (seg.base >= (m_tail->value.base + m_tail->value.seg_size)) {
        if (!push_back(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 如果链表只有一个节点，且不满足头部或尾部插入条件，则重叠
    if (m_head == m_tail) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
        return fail;
    }

    // 遍历链表，寻找插入位置
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        node* next = cur->next;
        
        // 检查是否与当前节点重叠
        if (seg.base < cur->value.base + cur->value.seg_size) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
            return fail;
        }
        
        // 检查是否可以插入到当前节点和下一节点之间
        if (seg.base + seg.seg_size <= next->value.base) {
            node* new_node = alloc_node(seg);
            if (!new_node) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
                return fail;
            }
            
            cur->next = new_node;
            new_node->prev = cur;
            new_node->next = next;
            next->prev = new_node;
            ++m_size;  // 更新大小计数器
            return success;
        }
        
        // 如果与下一节点重叠，返回冲突
        if (seg.base < next->value.base + next->value.seg_size) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
            return fail;
        }
    }
    
    // 理论上不应执行到这里
    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FATAL_REASONS::REASON_CODE_UNREACHABLE_CODE;
    return fatal;
}

KURD_t phymemspace_mgr::PHYSEG_LIST_ITEM::del_seg(phyaddr_t base)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_DEL_SEG;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_DEL_SEG;

    if (this->m_head == nullptr && this->m_tail == nullptr) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_DEL_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DLL_ALREADY_EMPTY;
        return fail;
    }
    
    if (base == this->m_head->value.base) {
        pop_front();
        return success;
    }
    
    if (base == this->m_tail->value.base) {
        pop_back();
        return success;
    }
    
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        if (cur->value.base == base) {
            node* next = cur->next;
            node* prev = cur->prev;
            
            if (prev && next) {
                prev->next = next;
                next->prev = prev;
                free_node(cur);
                --m_size;  // 更新大小计数器
                return success;
            }
        }
    }
    
    // 未找到对应段
    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_DEL_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
    return fail;
}

bool phymemspace_mgr::PHYSEG_LIST_ITEM::is_seg_have_cover(phyaddr_t base, uint64_t size)
{
    phyaddr_t end = base + size;
    
    for (node* cur = this->m_head; cur != nullptr; cur = cur->next) {
        // 如果当前段的基地址已经大于等于检查区间的结束地址，则后续段都不可能重叠
        if (cur->value.base >= end) {
            break;
        }
        
        phyaddr_t seg_end = cur->value.base + cur->value.seg_size;
        
        // 检查是否有重叠：两个左闭右开区间重叠的条件是：
        // !(base >= seg_end || end <= cur->value.base)
        if (base < seg_end && end > cur->value.base) {
            return true;
        }
    }
    
    return false;
}

KURD_t phymemspace_mgr::PHYSEG_LIST_ITEM::get_seg_by_base(phyaddr_t base, PHYSEG &seg)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_SEARCH;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_SEARCH;

    if(m_head==nullptr&&m_tail==nullptr){
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_SEARCH_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
        return fail;
    }
    for(node *it=m_head;it!=nullptr;it=it->next)
    {
        if(it->value.base==base)
        {
            seg=it->value;
            return success;
        }
    }
    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_SEARCH_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
    return fail;
}
KURD_t phymemspace_mgr::PHYSEG_LIST_ITEM::get_seg_by_addr(phyaddr_t addr, PHYSEG &seg)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_SEARCH;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_SEARCH;

    if(m_head==nullptr&&m_tail==nullptr){
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_SEARCH_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
        return fail;
    }
    for(node*cur=m_head;cur!=nullptr;cur=cur->next){
        if(cur->value.base<=addr&&(addr<cur->value.base+cur->value.seg_size)){
            seg=cur->value;
            return success;
        }
    }
    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_SEARCH_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
    return fail;
}
KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::default_kurd()
{
    return  KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_PHYMEMSPACE_MGR_LOW1MB_MGR,0,0,err_domain::CORE_MODULE);
}
KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}

KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::default_failure()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}
// low1mb_mgr_t类成员函数的实现
KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::regist_seg(low1mb_seg_t seg)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_ADD_SEG;

    // 空链表
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        if (!push_front(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 插入头部之前
    if ((seg.base + seg.size) <= m_head->value.base) {
        if (!push_front(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 插入尾部之后
    if (seg.base >= (m_tail->value.base + m_tail->value.size)) {
        if (!push_back(seg)) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
            return fail;
        }
        return success;
    }

    // 如果链表只有一个节点，且不满足头部或尾部插入条件，则重叠
    if (m_head == m_tail) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
        return fail;
    }

    // 遍历链表，寻找插入位置
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        node* next = cur->next;
        
        // 检查是否与当前节点重叠
        if (seg.base < cur->value.base + cur->value.size) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
            return fail;
        }
        
        // 检查是否可以插入到当前节点和下一节点之间
        if (seg.base + seg.size <= next->value.base) {
            node* new_node = alloc_node(seg);
            if (!new_node) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_ADD_SEG_FAIL;
                return fail;
            }
            
            cur->next = new_node;
            new_node->prev = cur;
            new_node->next = next;
            next->prev = new_node;
            ++m_size;  // 更新大小计数器
            return success;
        }
        
        // 如果与下一节点重叠，返回冲突
        if (seg.base < next->value.base + next->value.size) {
            fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_OVERLAP;
            return fail;
        }
    }
    
    // 理论上不应执行到这里
    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_ADD_SEG_RESULTS_CODE::FATAL_REASONS::REASON_CODE_UNREACHABLE_CODE;
    return fatal;
}

KURD_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::del_seg(uint32_t base)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_DEL_SEG;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::EVENT_CODE_MEMSEG_DOUBLE_LINK_LIST_DEL_SEG;

    if (this->m_head == nullptr && this->m_tail == nullptr) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_DEL_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DLL_ALREADY_EMPTY;
        return fail;
    }
    
    if (base == this->m_head->value.base) {
        pop_front();
        return success;
    }
    
    if (base == this->m_tail->value.base) {
        pop_back();
        return success;
    }
    
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        if (cur->value.base == base) {
            node* next = cur->next;
            node* prev = cur->prev;
            
            if (prev && next) {
                prev->next = next;
                next->prev = prev;
                free_node(cur);
                --m_size;  // 更新大小计数器
                return success;
            }
        }
    }
    
    // 未找到对应段
    fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_LOW1MB_MGR_DOUBLE_LINK_LIST_EVENTS_CODE::MEMSEG_DOUBLE_LINK_LIST_DEL_SEG_RESULTS_CODE::FAIL_REASONS::REASON_CODE_SEG_NOT_FOUND;
    return fail;
}

phymemspace_mgr::low1mb_mgr_t::low1mb_seg_t phymemspace_mgr::low1mb_mgr_t::interval_LinkList::get_seg_by_addr(uint32_t addr)
{
    low1mb_seg_t null_seg = {0, 0, LOW1MB_RESERVED_SEG}; // 默认返回一个空的段
    
    if (m_head == nullptr && m_tail == nullptr)
        return null_seg;
        
    for (node* cur = m_head; cur != nullptr; cur = cur->next) {
        if (cur->value.base <= addr && (addr < cur->value.base + cur->value.size)) {
            return cur->value;
        }
    }
    return null_seg;
}

bool phymemspace_mgr::low1mb_mgr_t::interval_LinkList::is_seg_have_cover(phyaddr_t base, uint64_t size)
{
    // 确保检查的地址在1MB以内
    if (base >= ADDR_TOP) {
        return false;
    }
    
    uint32_t end = (base + size > ADDR_TOP) ? ADDR_TOP : (uint32_t)(base + size);
    
    for (node* cur = this->m_head; cur != nullptr; cur = cur->next) {
        // 如果当前段的基地址已经大于等于检查区间的结束地址，则后续段都不可能重叠
        if (cur->value.base >= end) {
            break;
        }
        
        uint32_t seg_end = cur->value.base + cur->value.size;
        
        // 检查是否有重叠：两个左闭右开区间重叠的条件是：
        // !(base >= seg_end || end <= cur->value.base)
        if (base < seg_end && end > cur->value.base) {
            return true;
        }
    }
    
    return false;
}

// 静态成员函数的实现
int phymemspace_mgr::low1mb_mgr_t::regist_seg(low1mb_seg_t seg)
{
    return low1mb_seg_list.regist_seg(seg);
}

int phymemspace_mgr::low1mb_mgr_t::del_seg(uint32_t base)
{
    return low1mb_seg_list.del_seg(base);
}

phymemspace_mgr::low1mb_mgr_t::low1mb_seg_t phymemspace_mgr::low1mb_mgr_t::get_seg_by_addr(uint32_t addr)
{
    return low1mb_seg_list.get_seg_by_addr(addr);
}

bool phymemspace_mgr::low1mb_mgr_t::is_seg_have_cover(phyaddr_t base, uint64_t size)
{
    return low1mb_seg_list.is_seg_have_cover(base, size);
}
phymemspace_mgr::low1mb_mgr_t::low1mb_mgr_t(){
    
}
