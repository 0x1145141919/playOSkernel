#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "linker_symbols.h"
#include "util/OS_utils.h"
#include "util/kptrace.h"
#include "util/kout.h"
int VM_vaddr_cmp(VM_DESC *a, VM_DESC *b)
{
    if(a->start < b->start&&a->end <= b->start)return -1;
    if(a->start > b->start&&a->start >= b->end)return 1;
    return 0;
}
// 定义KspaceMapMgr的静态成员变量
KspaceMapMgr::kspace_vm_table_t* KspaceMapMgr::kspace_vm_table = nullptr;
spinlock_cpp_t KspaceMapMgr::GMlock = spinlock_cpp_t();
phyaddr_t KspaceMapMgr::kspace_uppdpt_phyaddr = 0;


bool pglv_4_or_5=PAGE_TBALE_LV::LV_4;//true代表4级页表，false代表5级页表
cache_table_idx_struct_t cache_strategy_to_idx(cache_strategy_t cache_strategy)
{   
    uint8_t i=0;
    for(;i<8;i++){
        if(cache_strategy==DEFAULT_PAT_CONFIG.mapped_entry[i])break;
    }
    cache_table_idx_struct_t result={
        .PWT=(uint8_t)(i&1),
        .PCD=(uint8_t)((i>>1)&1),
        .PAT=(uint8_t)((i>>2)&1)
    };
    return result;
}

KspaceMapMgr::kspace_vm_table_t::kspace_vm_table_t() = default;

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::left_rotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->left = x;

    y->parent = x->parent;
    x->parent = y;

    return y;
}

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::right_rotate(Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->right = x;

    y->parent = x->parent;
    x->parent = y;

    return y;
}

void KspaceMapMgr::kspace_vm_table_t::fix_insert(Node* n) {
    while (n != root && n->parent->color == 1) {
        Node* p = n->parent;
        Node* g = p->parent;

        if (p == g->left) {
            Node* u = g->right;
            if (u && u->color == 1) {
                p->color = 0;
                u->color = 0;
                g->color = 1;
                n = g;
            } else {
                if (n == p->right) {
                    n = p;
                    root = (p->parent ?
                        (p->parent->left == p ?
                            (p->parent->left = left_rotate(p)) :
                            (p->parent->right = left_rotate(p)))
                        : left_rotate(p));
                }
                p = n->parent;
                g = p->parent;
                p->color = 0;
                g->color = 1;

                root = (g->parent ?
                    (g->parent->left == g ?
                        (g->parent->left = right_rotate(g)) :
                        (g->parent->right = right_rotate(g)))
                    : right_rotate(g));
            }
        } else {
            Node* u = g->left;
            if (u && u->color == 1) {
                p->color = 0;
                u->color = 0;
                g->color = 1;
                n = g;
            } else {
                if (n == p->left) {
                    n = p;
                    root = (p->parent ?
                        (p->parent->left == p ?
                            (p->parent->left = right_rotate(p)) :
                            (p->parent->right = right_rotate(p)))
                        : right_rotate(p));
                }
                p = n->parent;
                g = p->parent;
                p->color = 0;
                g->color = 1;

                root = (g->parent ?
                    (g->parent->left == g ?
                        (g->parent->left = left_rotate(g)) :
                        (g->parent->right = left_rotate(g)))
                    : left_rotate(g));
            }
        }
    }
    root->color = 0;
}

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::subtree_min(Node* x) {
    while (x->left) x = x->left;
    return x;
}

void KspaceMapMgr::kspace_vm_table_t::fix_remove(Node* x, Node* parent) {
    while (x != root && (!x || x->color == 0)) {
        if (x == parent->left) {
            Node* w = parent->right;
            if (w->color == 1) {
                w->color = 0;
                parent->color = 1;
                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = left_rotate(parent)) :
                        (parent->parent->right = left_rotate(parent)))
                    : left_rotate(parent));
                w = parent->right;
            }
            if ((!w->left || w->left->color == 0) &&
                (!w->right || w->right->color == 0)) {
                w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->right || w->right->color == 0) {
                    if (w->left) w->left->color = 0;
                    w->color = 1;

                    root = (w->parent ?
                        (w->parent->left == w ?
                            (w->parent->left = right_rotate(w)) :
                            (w->parent->right = right_rotate(w)))
                        : right_rotate(w));

                    w = parent->right;
                }
                w->color = parent->color;
                parent->color = 0;
                if (w->right) w->right->color = 0;

                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = left_rotate(parent)) :
                        (parent->parent->right = left_rotate(parent)))
                    : left_rotate(parent));

                x = root;
                break;
            }
        } else {
            Node* w = parent->left;
            if (w->color == 1) {
                w->color = 0;
                parent->color = 1;
                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = right_rotate(parent)) :
                        (parent->parent->right = right_rotate(parent)))
                    : right_rotate(parent));
                w = parent->left;
            }
            if ((!w->right || w->right->color == 0) &&
                (!w->left || w->left->color == 0)) {
                w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->left || w->left->color == 0) {
                    if (w->right) w->right->color = 0;
                    w->color = 1;

                    root = (w->parent ?
                        (w->parent->left == w ?
                            (w->parent->left = left_rotate(w)) :
                            (w->parent->right = left_rotate(w)))
                        : left_rotate(w));

                    w = parent->left;
                }
                w->color = parent->color;
                parent->color = 0;
                if (w->left) w->left->color = 0;

                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = right_rotate(parent)) :
                        (parent->parent->right = right_rotate(parent)))
                    : right_rotate(parent));

                x = root;
                break;
            }
        }
    }

    if (x) x->color = 0;
}

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::subtree_max(Node* x)
{
    while (x && x->right)
    x = x->right;
    return x;
}

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::successor(Node* x) {
    if (!x) return nullptr;

    if (x->right) {
        Node* cur = x->right;
        while (cur->left)
            cur = cur->left;
        return cur;
    }

    Node* p = x->parent;
    while (p && x == p->right) {
        x = p;
        p = p->parent;
    }
    return p;
}

KspaceMapMgr::kspace_vm_table_t::Node* KspaceMapMgr::kspace_vm_table_t::search(vaddr_t vaddr) {
    Node* cur = root;
    while (cur) {
        if (vaddr < cur->data.start) {
            cur = cur->left;
        } else if (vaddr >= cur->data.end) {
            cur = cur->right;
        } else {
            return cur;
        }
    }
    return nullptr;
}

int KspaceMapMgr::kspace_vm_table_t::insert(VM_DESC data) {
    if (!root) {
        root = new (KspaceMapMgr::kspace_vm_table_t::specify_alloc_flag) Node{data, nullptr, nullptr, nullptr, 0};
        return 0;
    }

    Node* p = root;
    Node* parent = nullptr;

    while (p) {
        parent = p;
        int r = VM_vaddr_cmp(&data, &p->data);
        if (r == 0) return -1;
        p = (r < 0 ? p->left : p->right);
    }

    Node* n = new (KspaceMapMgr::kspace_vm_table_t::specify_alloc_flag) Node{data, nullptr, nullptr, parent, 1};

    if (VM_vaddr_cmp(&data, &parent->data) < 0)
        parent->left = n;
    else
        parent->right = n;

    fix_insert(n);
    return 0;
}

int KspaceMapMgr::kspace_vm_table_t::remove(vaddr_t vaddr) {
    Node* z = search(vaddr);
    if (!z) return -1;

    Node* y = z;
    Node* x = nullptr;
    Node* x_parent = nullptr;

    bool y_orig_color = y->color;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;

        if (!z->parent) root = z->right;
        else if (z == z->parent->left) z->parent->left = z->right;
        else z->parent->right = z->right;
        if (z->right) z->right->parent = z->parent;

    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;

        if (!z->parent) root = z->left;
        else if (z == z->parent->left) z->parent->left = z->left;
        else z->parent->right = z->left;
        if (z->left) z->left->parent = z->parent;

    } else {
        y = subtree_min(z->right);
        y_orig_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x_parent = y;
        } else {
            if (y->right) y->right->parent = y->parent;
            y->parent->right = y->right;

            y->right = z->right;
            y->right->parent = y;
            x_parent = y->parent;
        }

        if (!z->parent) root = y;
        else if (z == z->parent->left) z->parent->left = y;
        else z->parent->right = y;

        y->parent = z->parent;
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_orig_color == 0)
        fix_remove(x, x_parent);

    delete z;
    return 0;
}
KURD_t KspaceMapMgr::VM_search_by_vaddr(vaddr_t vaddr, VM_DESC &result){
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_VM_SEARCH_BY_ADDR;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_VM_SEARCH_BY_ADDR;

    // 假定调用者持有 GMlock，并且前 valid_vmentry_count 项是紧凑排列的已用条目
    VM_DESC tmp_desc={
        0
    };
    tmp_desc.start = vaddr;
    tmp_desc.end = vaddr + 1; // exclusive end
    KspaceMapMgr::kspace_vm_table_t::Node* result_node = kspace_vm_table->search(vaddr);
    if(result_node) {
        result = result_node->data;
        return success;
    }
    
    fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::VM_SEARCH_BY_ADDR_RESULTS::FAIL_REASONS::REASON_CODE_NOT_FOUND;
    return fail; // not found
}


int KspaceMapMgr::VM_add(VM_DESC vmentry){
    // 假定调用者持有 GMlock，且参数已校验（不做重复/越界检查）
    return kspace_vm_table->insert(vmentry);
}


int KspaceMapMgr::VM_del(VM_DESC*entry){
    return kspace_vm_table->remove(entry->start);
}

KURD_t KspaceMapMgr::enable_VMentry(VM_DESC &vmentry)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_ENABLE_VMENTRY;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_ENABLE_VMENTRY;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_ENABLE_VMENTRY;

    // basic alignment checks (4KB)
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.phys_start % _4KB_SIZE) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
        return fail;
    }

    if (vmentry.start >= vmentry.end) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
        return fail;
    }

    auto vmentry_congruence_vlidation = [vmentry]()->bool {
        enum PHY_SEG_MAX_PAGE:uint8_t{
        PHY_SEG_MAX_PAGE_4KB=0,
        PHY_SEG_MAX_PAGE_2MB,
        PHY_SEG_MAX_PAGE_1GB
    };
        auto max_type_identifier=[vmentry]()->PHY_SEG_MAX_PAGE{
        phyaddr_t phybase=vmentry.phys_start;
        phyaddr_t phyend=phybase+vmentry.end-vmentry.start;

        uint64_t base_1GB=align_up(phybase,_1GB_SIZE);
        uint64_t end_1GB=align_down(phyend,_1GB_SIZE);
        if(base_1GB<end_1GB){
            return PHY_SEG_MAX_PAGE_1GB;
        }
        uint64_t base_2MB=align_up(phybase,_2MB_SIZE);
        uint64_t end_2MB=align_down(phyend,_2MB_SIZE);
        if(base_2MB<end_2MB){
            return PHY_SEG_MAX_PAGE_2MB;
        }
        return PHY_SEG_MAX_PAGE_4KB;
        };
        switch(max_type_identifier())
        {
            case PHY_SEG_MAX_PAGE_4KB:return true;
            case PHY_SEG_MAX_PAGE_2MB:return vmentry.start%_2MB_SIZE==vmentry.phys_start%_2MB_SIZE;
            case PHY_SEG_MAX_PAGE_1GB:return vmentry.start%_1GB_SIZE==vmentry.phys_start%_1GB_SIZE;
        }
    };
    if (!vmentry_congruence_vlidation()) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_VMENTRY_congruence_vlidation;
        return fail;
    }

    // Only implement 4-level paging path here
    if (!pglv_4_or_5) {
        // 5-level not implemented in this function
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_NOT_SUPPORT_LV5_PAGING;
        return fail;
    }

    // 1) Split segment into page-sized runs
    seg_to_pages_info_pakage_t pkg;
     seg_to_pages_info_get(pkg, vmentry);
    // Helper lambda: given page size (p), call the appropriate setter in chunks
    KURD_t rc=KURD_t();
    // 2) Iterate over pkg.entryies and dispatch
    for (int i = 0; i < 5; ++i) {
        auto &e = pkg.entryies[i];
        if (e.num_of_pages == 0) continue;

        uint64_t psize = e.page_size_in_byte;
        // sanity check: vbase and base should be aligned to page size
        if ((e.vbase % psize) != 0 || (e.phybase % psize) != 0) {
            fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
            return fail;
        }

        switch(e.page_size_in_byte) {
            case _1GB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pdpte_1GB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            case _2MB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pde_2MB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            case _4KB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pte_4KB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            default:
                fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FATAL_REASONS::REASON_CODE_INVALIDE_PAGES_SIZE;
                return fatal; // unknown page size
        }
        if (rc.result != result_code::SUCCESS) {
            return rc;
        }
    }

    // Optionally mark vmentry as enabled (if VM_DESC has such a field)
    // vmentry.enabled = true;  // uncomment if VM_DESC supports it

    return success;
}
void *KspaceMapMgr::pgs_remapp(KURD_t&kurd,
    phyaddr_t addr, 
    uint64_t size, 
    pgaccess access, 
    vaddr_t vbase,
    bool is_protective
)
{
    if(addr%_4KB_SIZE||size%_4KB_SIZE||vbase%_4KB_SIZE)return nullptr;
    VM_DESC vmentry={
        .start=0,
        .end=0,
        .map_type=VM_DESC::MAP_PHYSICAL,//内核的内存你敢随便分配吗，必须第一时间分配映射
        .phys_start=addr,
        .access=access,        
        .committed_full=0,
    };
    GMlock.lock();
    int status;
    if(vbase==0){
        vmentry.is_vaddr_alloced=1;
        if(is_protective){
        vaddr_t new_base=kspace_vm_table->alloc_available_space(size+_4KB_SIZE*2, (addr-_4KB_SIZE)%_1GB_SIZE);
        GMlock.unlock();  
        vmentry.start=new_base;
        vmentry.end=new_base+size+_4KB_SIZE*2;  
        vmentry.is_out_bound_protective=true;
    }else
        {
        vaddr_t new_base=kspace_vm_table->alloc_available_space(size, addr%_1GB_SIZE);
        GMlock.unlock();
        vmentry.start=new_base;
        vmentry.end=new_base+size;
    }
    }else{
        if(pglv_4_or_5){
            if(size+vbase-PAGELV4_KSPACE_BASE>PAGELV4_KSPACE_SIZE){
            GMlock.unlock();
            return nullptr;
            }
            vmentry.start=vbase;
            vmentry.end=vbase+size;
        }
    }
    status=VM_add(vmentry);
    if (status!=OS_SUCCESS)
    {
        GMlock.unlock();
        
        if(status==OS_OUT_OF_RESOURCE){
            kio::bsp_kout << "KspaceMapMgr::pgs_remapp:VM_add entryies out of resource" << kio::kendl;
        }
        return nullptr;
    }
    VM_DESC vmentry_copy=vmentry;
    if(vmentry.is_vaddr_alloced&&vmentry.is_out_bound_protective){
        
        vmentry_copy.start+=_4KB_SIZE;
        vmentry_copy.end-=_4KB_SIZE;
    }
    kurd=enable_VMentry(vmentry_copy);    
    GMlock.unlock();
    if(status==OS_SUCCESS)return(void*)vmentry_copy.start; 
    return nullptr;
}

KURD_t KspaceMapMgr::seg_to_pages_info_get(seg_to_pages_info_pakage_t &result, VM_DESC vmentry)
{
    constexpr uint64_t _4KB_PG_SIZE= _4KB_SIZE;
    constexpr uint64_t _2MB_PG_SIZE = _2MB_SIZE;
    constexpr uint64_t _1GB_PG_SIZE = _1GB_SIZE;
    
    // initialize
    for (int i = 0; i < 5; i++) {
        result.entryies[i].vbase = 0;
        result.entryies[i].phybase = 0;
        result.entryies[i].page_size_in_byte = 0;
        result.entryies[i].num_of_pages = 0;
    }
    vaddr_t vbase=vmentry.start;
    vaddr_t vend=vmentry.end;
    uint64_t offset=vmentry.start-vmentry.phys_start;
    vaddr_t start_up_2mb = align_up(vbase, _2MB_PG_SIZE);
    vaddr_t end_down_2mb = align_down(vend, _2MB_PG_SIZE);
    
    vaddr_t start_up_1gb = align_up(vbase, _1GB_PG_SIZE);
    vaddr_t end_down_1gb = align_down(vend, _1GB_PG_SIZE);

    bool is_cross_2mb_boud = false;
    bool is_cross_1gb_boud = false;
    
    if (start_up_2mb <= end_down_2mb) {
        is_cross_2mb_boud = true;
        if (start_up_1gb <= end_down_1gb) {
            is_cross_1gb_boud = true;
        } else {
            is_cross_1gb_boud = false;
        }
    } else {
        is_cross_1gb_boud = false;
        is_cross_2mb_boud = false;
    }
    

    
    if(is_cross_2mb_boud){
        if(is_cross_1gb_boud){
            // 处理跨越1GB边界的段
            uint64_t countmid1gb=(end_down_1gb - start_up_1gb)/_1GB_PG_SIZE;
            result.entryies[0].vbase = start_up_1gb;
            result.entryies[0].page_size_in_byte = _1GB_PG_SIZE;
            result.entryies[0].num_of_pages = countmid1gb;
            result.entryies[0].phybase = start_up_1gb - offset;
            
            // 处理1GB区域之前的2MB区域
            uint64_t count_down_2mb=(start_up_1gb - start_up_2mb)/_2MB_PG_SIZE;
            result.entryies[1].vbase = start_up_2mb;
            result.entryies[1].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[1].num_of_pages = count_down_2mb;
            result.entryies[1].phybase = start_up_2mb - offset;
            
            // 处理1GB区域之后的2MB区域
            uint64_t count_up_2mb=(end_down_2mb - end_down_1gb)/_2MB_PG_SIZE;
            result.entryies[2].vbase = end_down_1gb;
            result.entryies[2].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[2].num_of_pages = count_up_2mb;
            result.entryies[2].phybase = end_down_2mb - offset;
        }else{
            // 处理仅跨越2MB边界的段
            uint64_t count_2mb=(end_down_2mb - start_up_2mb)/_2MB_PG_SIZE;
            result.entryies[2].vbase = start_up_2mb;
            result.entryies[2].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[2].num_of_pages = count_2mb;
            result.entryies[2].phybase = start_up_2mb - offset;
        }
        
        // 处理起始部分的小页面
        uint64_t countdown4kb=(start_up_2mb-vbase)/_4KB_PG_SIZE;
        result.entryies[3].vbase = vbase;
        result.entryies[3].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[3].num_of_pages = countdown4kb;
        result.entryies[3].phybase = vbase - offset;
        
        // 处理结束部分的小页面
        uint64_t countup4kb=(vend-end_down_2mb)/_4KB_PG_SIZE;
        result.entryies[4].vbase = end_down_2mb;
        result.entryies[4].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[4].num_of_pages = countup4kb;
        result.entryies[4].phybase = end_down_2mb - offset;
    }else{
        // 仅使用4KB页面
        uint64_t count4kbpgs=(vend-vbase)/_4KB_PG_SIZE;
        result.entryies[4].vbase = vbase;
        result.entryies[4].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[4].num_of_pages = count4kbpgs;
        result.entryies[4].phybase = vbase - offset;
    }
    KURD_t success=default_success();
    success.event_code=MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_SEG_TO_INFO_PACKAGE;
    return success;
}
KURD_t KspaceMapMgr::disable_VMentry(VM_DESC &vmentry)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_DISABLE_VMENTRY;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_DISABLE_VMENTRY;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_DISABLE_VMENTRY;

    // basic alignment checks (4KB)
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.phys_start % _4KB_SIZE) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::DISABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
        return fail;
    }

    if (vmentry.start >= vmentry.end) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::DISABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
        return fail;
    }

    // require that vstart % 1GB == phys_start % 1GB (与 seg_to_pages_info_get 的前置条件一致)
    if ((vmentry.start % _1GB_SIZE) != (vmentry.phys_start % _1GB_SIZE)) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::DISABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VMENTRY;
        return fail;
    }
    
    KURD_t status = seg_to_pages_info_get(shared_inval_kspace_VMentry_info.info_package, vmentry);
    for(uint8_t i = 0; i < 5; i++)
    {
        seg_to_pages_info_pakage_t::pages_info_t& entry = 
            shared_inval_kspace_VMentry_info.info_package.entryies[i];
            
        if (entry.page_size_in_byte == 0 || entry.num_of_pages == 0) 
            continue;
            
        switch (entry.page_size_in_byte) {
            case _4KB_SIZE:
                {
                    status = _4lv_pte_4KB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                    if(status.result != result_code::SUCCESS) return status;
                    break;
                }
            case _2MB_SIZE:
            {
                status = _4lv_pde_2MB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if(status.result != result_code::SUCCESS) return status;
                break;
            }
            case _1GB_SIZE:
            {
                status = _4lv_pdpte_1GB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if(status.result != result_code::SUCCESS) return status;
                break;
            }
            default:
                fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::DISABLE_VMENTRY_RESULTS::FATAL_REASONS::REASON_CODE_INVALIDE_PAGES_SIZE;
                return fatal;
        
        }
    }
    return success;
}
KURD_t KspaceMapMgr::v_to_phyaddrtraslation(vaddr_t vaddr, phyaddr_t &result)
{
    PageTableEntryUnion badentry={0};
    PageTableEntryUnion& result_entry=badentry;
    uint32_t page_size;
    KURD_t status = v_to_phyaddrtraslation_entry(vaddr, result_entry, page_size);
    if(status.result != result_code::SUCCESS)return status;
    switch (page_size)
    {
        case _4KB_SIZE:
            {
                result=result_entry.pte.page_addr<<12;
                return default_success();
            }
        case _2MB_SIZE:
            {
                result=result_entry.pde2MB._2mb_Addr<<21;
                return default_success();
            }
        case _1GB_SIZE:
            {
                result=result_entry.pdpte1GB._1GB_Addr<<30;
                return default_success();
            }
    }
}
/**
 * @brief 虚拟地址比较函数
 * 如果地址段有重复，则返回0,
 * a.start < b.start, 且 a.end <= b.start, 返回负值
 * a.start > b.start, 且 a.start >= b.end, 返回正值
 */

/**
 * 这个接口有大问题，至少需要传入一个参数表示原有物理地址余1GB的偏移，虚拟地址必须与这个偏移对应
 *选取起始虚拟地址也有讲究，不能一刀切必须与物理地址1gb同余，这样要造成虚拟地址浪费
 *根据穿来的物理地址便宜和大小判断应该采取分配虚拟地址策略，如果对应物理段合并了2mb大页，那么虚拟地址，物理地址要2mb同余
*如果对应物理段合并了1gb大页，那么虚拟地址，物理地址要1gb同余
    *没有合并则最简单
 */
/**
 * 所以需要把起始虚拟地址向上调整操作，鉴定物理段使用的大页操作这些拿出来做成函数
 */
vaddr_t KspaceMapMgr::kspace_vm_table_t::alloc_available_space(uint64_t size,uint32_t target_vaddroffset)
{
    if(size==0||size%_4KB_SIZE||target_vaddroffset>=_1GB_SIZE||target_vaddroffset%_4KB_SIZE)return 0;
    enum PHY_SEG_MAX_PAGE:uint8_t{
        PHY_SEG_MAX_PAGE_4KB=0,
        PHY_SEG_MAX_PAGE_2MB,
        PHY_SEG_MAX_PAGE_1GB
    };
    auto max_type_identifier=[size,target_vaddroffset]()->PHY_SEG_MAX_PAGE{
        phyaddr_t phybase=target_vaddroffset;
        phyaddr_t phyend=phybase+size;

        uint64_t base_1GB=align_up(phybase,_1GB_SIZE);
        uint64_t end_1GB=align_down(phyend,_1GB_SIZE);
        if(base_1GB<end_1GB){
            return PHY_SEG_MAX_PAGE_1GB;
        }
        uint64_t base_2MB=align_up(phybase,_2MB_SIZE);
        uint64_t end_2MB=align_down(phyend,_2MB_SIZE);
        if(base_2MB<end_2MB){
            return PHY_SEG_MAX_PAGE_2MB;
        }
        return PHY_SEG_MAX_PAGE_4KB;
    };
    PHY_SEG_MAX_PAGE max_page_type=max_type_identifier();
    auto base_addr_modifier=[max_page_type,target_vaddroffset](vaddr_t base)->vaddr_t{ 
        switch (max_page_type)
        {
            case PHY_SEG_MAX_PAGE_4KB:
                return base;
            case PHY_SEG_MAX_PAGE_2MB:
                {
                    uint32_t lower_offset=base%_2MB_SIZE;
                    uint64_t basebase=base-lower_offset;
                    uint32_t upper_offset=target_vaddroffset%_2MB_SIZE;
                    if(upper_offset<lower_offset)return basebase+upper_offset+_2MB_SIZE;
                    else return basebase+upper_offset;
                }
            case PHY_SEG_MAX_PAGE_1GB:
                {
                    uint32_t lower_offset=base%_1GB_SIZE;
                    uint64_t basebase=base-lower_offset;
                    uint32_t upper_offset=target_vaddroffset%_1GB_SIZE;
                    if(upper_offset<lower_offset)return basebase+upper_offset+_1GB_SIZE;
                    else return basebase+upper_offset;
                }
            default:
                    return 0;
        }
    };
    KspaceMapMgr::kspace_vm_table_t::Node* maxnode=this->subtree_max(this->root);
VM_DESC*maxentry=&maxnode->data;
    vaddr_t upper_alloc_base=base_addr_modifier(maxentry->end);
    uint64_t upper_top_avaliable=~upper_alloc_base+1;
    if(upper_top_avaliable>size)return upper_alloc_base;
    KspaceMapMgr::kspace_vm_table_t::Node* minnode=this->subtree_min(this->root);
    KspaceMapMgr::kspace_vm_table_t::Node* backnode=minnode;
    KspaceMapMgr::kspace_vm_table_t::Node* front_node=successor(minnode);
    do{
        vaddr_t gap_start=base_addr_modifier(backnode->data.end);
        vaddr_t gap_end=front_node->data.start;
        if(gap_end>gap_start&&(gap_end-gap_start)>=size){
            return gap_start;
        }
        backnode=front_node;
        front_node=successor(front_node);
    }while(front_node!=maxnode);
    return 0;
}
