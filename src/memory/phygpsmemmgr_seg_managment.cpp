#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
int phymemspace_mgr::PHYSEG_LIST_ITEM::add_seg(PHYSEG &seg)
{
    // 空链表
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        if (!push_front(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 插入头部之前
    if ((seg.base + seg.seg_size) <= m_head->value.base) {
        if (!push_front(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 插入尾部之后
    if (seg.base >= (m_tail->value.base + m_tail->value.seg_size)) {
        if (!push_back(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 如果链表只有一个节点，且不满足头部或尾部插入条件，则重叠
    if (m_head == m_tail) {
        return OS_RESOURCE_CONFILICT;
    }

    // 遍历链表，寻找插入位置
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        node* next = cur->next;
        
        // 检查是否与当前节点重叠
        if (seg.base < cur->value.base + cur->value.seg_size) {
            return OS_RESOURCE_CONFILICT;
        }
        
        // 检查是否可以插入到当前节点和下一节点之间
        if (seg.base + seg.seg_size <= next->value.base) {
            node* new_node = alloc_node(seg);
            if (!new_node) {
                return OS_OUT_OF_MEMORY;
            }
            
            cur->next = new_node;
            new_node->prev = cur;
            new_node->next = next;
            next->prev = new_node;
            ++m_size;  // 更新大小计数器
            return OS_SUCCESS;
        }
        
        // 如果与下一节点重叠，返回冲突
        if (seg.base < next->value.base + next->value.seg_size) {
            return OS_RESOURCE_CONFILICT;
        }
    }
    
    // 理论上不应执行到这里
    return OS_RESOURCE_CONFILICT;
}

int phymemspace_mgr::PHYSEG_LIST_ITEM::del_seg(phyaddr_t base)
{
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        return OS_BAD_FUNCTION;
    }
    
    if (base == this->m_head->value.base) {
        pop_front();
        return OS_SUCCESS;
    }
    
    if (base == this->m_tail->value.base) {
        pop_back();
        return OS_SUCCESS;
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
                return OS_SUCCESS;
            }
        }
    }
    
    // 未找到对应段
    return OS_BAD_FUNCTION;
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

// low1mb_mgr_t类成员函数的实现
int phymemspace_mgr::low1mb_mgr_t::interval_LinkList::regist_seg(low1mb_seg_t seg)
{
    // 空链表
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        if (!push_front(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 插入头部之前
    if ((seg.base + seg.size) <= m_head->value.base) {
        if (!push_front(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 插入尾部之后
    if (seg.base >= (m_tail->value.base + m_tail->value.size)) {
        if (!push_back(seg)) {
            return OS_OUT_OF_MEMORY;
        }
        return OS_SUCCESS;
    }

    // 如果链表只有一个节点，且不满足头部或尾部插入条件，则重叠
    if (m_head == m_tail) {
        return OS_RESOURCE_CONFILICT;
    }

    // 遍历链表，寻找插入位置
    for (node* cur = this->m_head; cur != m_tail; cur = cur->next) {
        node* next = cur->next;
        
        // 检查是否与当前节点重叠
        if (seg.base < cur->value.base + cur->value.size) {
            return OS_RESOURCE_CONFILICT;
        }
        
        // 检查是否可以插入到当前节点和下一节点之间
        if (seg.base + seg.size <= next->value.base) {
            node* new_node = alloc_node(seg);
            if (!new_node) {
                return OS_OUT_OF_MEMORY;
            }
            
            cur->next = new_node;
            new_node->prev = cur;
            new_node->next = next;
            next->prev = new_node;
            ++m_size;  // 更新大小计数器
            return OS_SUCCESS;
        }
        
        // 如果与下一节点重叠，返回冲突
        if (seg.base < next->value.base + next->value.size) {
            return OS_RESOURCE_CONFILICT;
        }
    }
    
    // 理论上不应执行到这里
    return OS_RESOURCE_CONFILICT;
}

int phymemspace_mgr::low1mb_mgr_t::interval_LinkList::del_seg(uint32_t base)
{
    if (this->m_head == nullptr && this->m_tail == nullptr) {
        return OS_BAD_FUNCTION;
    }
    
    if (base == this->m_head->value.base) {
        pop_front();
        return OS_SUCCESS;
    }
    
    if (base == this->m_tail->value.base) {
        pop_back();
        return OS_SUCCESS;
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
                return OS_SUCCESS;
            }
        }
    }
    
    // 未找到对应段
    return OS_BAD_FUNCTION;
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
