#include "memory/Memory.h"
#include "util/OS_utils.h"
#include "os_error_definitions.h"
#include "VideoDriver.h"
#include "panic.h"
#include "memory/kpoolmemmgr.h"
#include "util/cpuid_intel.h"
#ifdef USER_MODE
#include "stdlib.h"
#include "kpoolmemmgr.h"
#endif  

// 定义类中的静态成员变量
bool kpoolmemmgr_t::is_able_to_alloc_new_hcb = false;
kpoolmemmgr_t::HCB_v2* kpoolmemmgr_t::HCB_ARRAY[kpoolmemmgr_t::HCB_ARRAY_MAX_COUNT] = {nullptr};
trylock_cpp_t kpoolmemmgr_t::HCB_LOCK_ARR[kpoolmemmgr_t::HCB_ARRAY_MAX_COUNT];
kpoolmemmgr_t::HCB_v2 kpoolmemmgr_t::first_linekd_heap;

void kpoolmemmgr_t::enable_new_hcb_alloc()
{
    is_able_to_alloc_new_hcb=true;
}

void *kpoolmemmgr_t::kalloc(uint64_t size, bool is_longtime, bool vaddraquire, uint8_t alignment)
{

    void*ptr;
    if(is_able_to_alloc_new_hcb)//先尝试在现核心的堆中分配，再尝试建立新堆，最后再次借用别人的堆
    { 
        for(uint32_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
        {
            if(HCB_ARRAY[i]==nullptr)
            {
                if(HCB_LOCK_ARR[i].try_lock())
                {
                    // 尝试分配HCB_v2对象所需内存
                    void* temp_ptr = nullptr;
                    int status=first_linekd_heap.in_heap_alloc(temp_ptr,sizeof(HCB_v2),is_longtime,vaddraquire,alignment);
                    if(status == OS_SUCCESS) {
                        HCB_ARRAY[i] = new (temp_ptr) HCB_v2(query_x2apicid());
                        status=HCB_ARRAY[i]->second_stage_Init();
                        if(status!=OS_SUCCESS){
                            // 注意：这里temp_ptr不需要显式释放，因为HCB_v2构造函数可能已经管理了它
                            HCB_ARRAY[i]->~HCB_v2(); // 显式调用析构函数
                            HCB_ARRAY[i] = nullptr;
                            HCB_LOCK_ARR[i].unlock();
                            return nullptr;
                        }
                    } else {
                        HCB_LOCK_ARR[i].unlock();
                        continue; // 继续寻找其他空槽
                    }
                    HCB_LOCK_ARR[i].unlock();
                    status=HCB_ARRAY[i]->in_heap_alloc(ptr,size,is_longtime,vaddraquire,alignment);
                    //如果刚建立的新堆出问题那是不可能的
                    if (status == OS_SUCCESS) {
                        return ptr;
                    }
                    // 如果分配失败，我们仍返回nullptr，但不销毁HCB_ARRAY[i]
                }
            } else {
                if(HCB_ARRAY[i]->get_belonged_cpu_apicid()==query_x2apicid())
                {
                    if(HCB_ARRAY[i]->is_full())
                    {
                        continue;
                    }
                    int status=HCB_ARRAY[i]->in_heap_alloc(ptr,size,is_longtime,vaddraquire,alignment);
                    if(status==OS_SUCCESS){
                        return ptr;
                    }
                }
            }
        }
    }
    int status=first_linekd_heap.in_heap_alloc(ptr,size,is_longtime,vaddraquire,alignment);
    if(status==OS_SUCCESS)
    {
        return ptr;
    }
    
    return nullptr;
}

void *kpoolmemmgr_t::realloc(void *ptr, uint64_t size,bool vaddraquire,uint8_t alignment)
{

    void*old_ptr=ptr;
    int status=0;
    if(is_able_to_alloc_new_hcb)
    {
        void*result=kalloc(size,false,vaddraquire,alignment);
        if(result!=nullptr)
        {
            HCB_v2::data_meta* oldmeta=
            reinterpret_cast<HCB_v2::data_meta*>(reinterpret_cast<uint64_t>(old_ptr)-sizeof(HCB_v2::data_meta));
            ksystemramcpy(old_ptr,result,oldmeta->data_size);
            clear(old_ptr);
            return result;
        }else{
            return nullptr;
        }
    }else{
        status= first_linekd_heap.in_heap_realloc(ptr,size,vaddraquire,alignment);
        if(status==OS_HEAP_OBJ_DESTROYED)//这个分支直接内核恐慌
        {
            kputsSecure(const_cast<char*>("kpoolmemmgr_t::realloc:first_linekd_heap.in_heap_realloc() return OS_HEAP_OBJ_DESTROYED\n when reallocating at address 0x"));
            kpnumSecure(&ptr,UNHEX,8);
            KernelPanicManager::panic("\nInit error,first_linekd_heap has been destroyed\n");
        }
    }
    return status==OS_SUCCESS?ptr:nullptr;
}

void kpoolmemmgr_t::clear(void *ptr)
{
    int status=OS_SUCCESS;

    if(is_able_to_alloc_new_hcb)
    {
        for(uint32_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
        {
            if(HCB_ARRAY[i]!=nullptr)
            {
                if(HCB_ARRAY[i]->is_addr_belong_to_this_hcb(ptr)){
                    status=HCB_ARRAY[i]->clear(ptr);
                    if(status==OS_HEAP_OBJ_DESTROYED){
                        //打印日志信息并且panic
                    }
                    return;
                }
            }
        }
    }
        status=first_linekd_heap.clear(ptr);
        if(status==OS_HEAP_OBJ_DESTROYED){
            kputsSecure(const_cast<char*>("kpoolmemmgr_t::clear:first_linekd_heap.clear return OS_HEAP_OBJ_DESTROYED\n when clearing at address 0x"));
            kpnumSecure(&ptr,UNHEX,8);
            KernelPanicManager::panic("\nfirst_linekd_heap has been destroyed\n");
        }
    
}

int kpoolmemmgr_t::Init()
{

    is_able_to_alloc_new_hcb=false;
    first_linekd_heap.first_linekd_heap_Init();
    return OS_SUCCESS;
}

void kpoolmemmgr_t::kfree(void *ptr)
{
    int status=OS_SUCCESS;

    if(is_able_to_alloc_new_hcb)
    {
        for(uint32_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
        {
            if(HCB_ARRAY[i]!=nullptr)
            {
                if(HCB_ARRAY[i]->is_addr_belong_to_this_hcb(ptr)){
                    status=HCB_ARRAY[i]->free(ptr);
                    if(status==OS_HEAP_OBJ_DESTROYED){
                        //打印日志信息并且panic
                    }
                    if(HCB_ARRAY[i]->get_used_bytes_count()==0){
                        status=first_linekd_heap.free(HCB_ARRAY[i]);
                        if(status!=OS_SUCCESS)
                        {//过于诡异的情况直接panic

                        }
                        HCB_LOCK_ARR[i].try_lock();
                        HCB_ARRAY[i]=nullptr;
                        HCB_LOCK_ARR[i].unlock();
                    }
                    return;
                }
            }
        }
    }else{
        status=first_linekd_heap.free(ptr);
        if(status==OS_HEAP_OBJ_DESTROYED)
        {
             kputsSecure(const_cast<char*>("kpoolmemmgr_t::realloc:first_linekd_heap.in_heap_realloc() return OS_HEAP_OBJ_DESTROYED\n when freeing at address 0x"));
            kpnumSecure(&ptr,UNHEX,8);
            KernelPanicManager::panic("\nInit error,first_linekd_heap has been destroyed\n");
        }
    }
}
//想获得物理地址若本身传的就是物理地址则还是会返回物理地址
phyaddr_t kpoolmemmgr_t::get_phy(vaddr_t addr)
{
    uint64_t MIN_KVADDR=0;
    uint64_t MAX_PHYADDR=0;;
    #ifdef PGLV_5
    MIN_KVADDR=0xff00000000000000;
    MAX_PHYADDR=1ULL<<56;
    #endif
    #ifdef PGLV_4
    MIN_KVADDR=0xffff800000000000;
    MAX_PHYADDR=1ULL<<47;
    #endif
    if(addr<MIN_KVADDR&&addr>=MAX_PHYADDR)return 0;
    if(is_able_to_alloc_new_hcb){
        for(uint16_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
        {
            if(HCB_ARRAY[i]!=nullptr){
                phyaddr_t result=HCB_ARRAY[i]->tran_to_phy((void*)addr);
                if(result!=0)return result;
            }
        }
    }
    return first_linekd_heap.tran_to_phy((void*)addr);
}
vaddr_t kpoolmemmgr_t::get_virt(phyaddr_t addr)
{
    uint64_t MIN_KVADDR=0;
    uint64_t MAX_PHYADDR=0;;
    #ifdef PGLV_5
    MIN_KVADDR=0xff00000000000000;
    MAX_PHYADDR=1ULL<<56;
    #endif
    #ifdef PGLV_4
    MIN_KVADDR=0xffff800000000000;
    MAX_PHYADDR=1ULL<<47;
    #endif
    if(addr<MIN_KVADDR&&addr>=MAX_PHYADDR)return 0;
    if(is_able_to_alloc_new_hcb){
        for(uint16_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
        {
            if(HCB_ARRAY[i]!=nullptr){
                vaddr_t result=HCB_ARRAY[i]->tran_to_virt(addr);
                if(result!=0)return result;
            }
        }
    }
    return first_linekd_heap.tran_to_virt(addr);
}
kpoolmemmgr_t::kpoolmemmgr_t()
{
}
kpoolmemmgr_t::~kpoolmemmgr_t()
{
}

