#include "memory/PagetbHeapMgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "linker_symbols.h"
PagetbHeapMgr_t gPgtbHeapMgr;
constexpr uint64_t _2MB_SIZE=2*1024*1024;
constexpr uint64_t _4KB_SIZE=4*1024;
PagetbHeapMgr_t::PgtbHCB::PgtbHCB(bool is_init)
//只能指望调用者的自觉了，非静态堆不要用这个模式
{
    heapvbase=(vaddr_t)&__pgtbhp_start;
    heapsize=(uint64_t)&__pgtbhp_end-heapvbase;
    heapphybase=(phyaddr_t)&_pgtb_heap_lma;
    bitmap=new PgthHCB_bitmap_t(heapsize);
}
void *PagetbHeapMgr_t::PgtbHCB::alloc(phyaddr_t &phybase)
{
    uint64_t result_bit_idx=0;
    bitmap->bitmap_read_lock();
    int status=bitmap->continual_avaliable_bits_search(1,result_bit_idx);
    bitmap->bitmap_read_unlock();
    if(status!=OS_SUCCESS)return nullptr;
    bitmap->bitmap_write_lock();
    bitmap->bit_set(result_bit_idx,true);
    bitmap->used_bit_count_add(1);
    bitmap->bitmap_write_unlock();
    vaddr_t result_vaddr=heapvbase+result_bit_idx*_4KB_SIZE;
    phybase=heapphybase+result_bit_idx*_4KB_SIZE;
    return (void*)result_vaddr;
}
int PagetbHeapMgr_t::PgtbHCB::free(void *vaddr)
{
    vaddr_t vaddr_base=(vaddr_t)vaddr;
    if( vaddr_base<heapvbase || vaddr_base>=heapvbase+heapsize)return OS_OUT_OF_RANGE;
    if(vaddr_base%_4KB_SIZE)return OS_INVALID_PARAMETER;
    uint64_t bit_idx=(vaddr_base-heapvbase)/_4KB_SIZE;
    bitmap->bitmap_write_lock();
    bitmap->bit_set(bit_idx,false);
    bitmap->used_bit_count_sub(1);
    bitmap->bitmap_write_unlock();
    return OS_SUCCESS;
}
 int PagetbHeapMgr_t::PgtbHCB::free(phyaddr_t phybase)
{
    phyaddr_t phy_base=phybase;
    if( phy_base<heapphybase || phy_base>=heapphybase+heapsize)return OS_OUT_OF_RANGE;
    if(phy_base%_4KB_SIZE)return OS_INVALID_PARAMETER;
    uint64_t bit_idx=(phy_base-heapphybase)/_4KB_SIZE;
    bitmap->bitmap_write_lock();
    bitmap->bit_set(bit_idx,false);
    bitmap->used_bit_count_sub(1);
    bitmap->bitmap_write_unlock();
    return OS_SUCCESS;
}
vaddr_t PagetbHeapMgr_t::PgtbHCB::phyaddr_to_vaddr(phyaddr_t phybase)
{
    if(phybase<heapphybase || phybase>=heapphybase+heapsize)return 0;
    else return phybase-heapphybase+heapvbase;
}
PagetbHeapMgr_t::PgtbHCB::PgthHCB_bitmap_t::PgthHCB_bitmap_t(uint64_t heap_size)
{
    bitmap_rwlock.read_unlock();
    bitmap_rwlock.write_unlock();
    used_bit_count_lock.unlock();
    if(heap_size%_2MB_SIZE){bitmap=nullptr;return ;}
    bitmap_used_bit=0;
    bitmap_size_in_64bit_units=heap_size/(_4KB_SIZE*64);
    bitmap=new uint64_t[bitmap_size_in_64bit_units];
    byte_bitmap_base=(uint8_t*)bitmap;
    u64s_set(0,bitmap_size_in_64bit_units,false);
}
PagetbHeapMgr_t::PgtbHCB::PgthHCB_bitmap_t::~PgthHCB_bitmap_t()
{
    delete[] bitmap;
}
bool PagetbHeapMgr_t::PgtbHCB::is_full()
{
    return heapsize/ _4KB_SIZE==bitmap->get_bitmap_used_bit();
}
bool PagetbHeapMgr_t::PgtbHCB::is_empty()
{
    return bitmap->get_bitmap_used_bit()==0;
}
int PagetbHeapMgr_t::Init()
{
    if(is_inited)return OS_BAD_FUNCTION;
    for(int i=0;i<MAX_PGTBHEAP_COUNT;i++)Pgtb_HCB_ARRAY[i]=nullptr;
    first_static_Pgtb_HCB=new PgtbHCB(true);
    is_extensible=false;
    is_inited=true;
    return OS_SUCCESS;
}
/**
 * 这个函数有问题：在新分配堆的时候可能搞出递归溢出
 * 因为创建新堆的第二阶初始化的时候要映射页表，而映射页表又要分配页表页，而分配页表页又要调用这个函数，又会扫描到还没有初始化好的新堆
 * 解决方法：另外设计一个专门给分配页表页的函数，不经过这个分配函数，
 * 另外一个函数直接从前到后扫描有空闲的堆，分配一个页就走，充分利用这个函数的预留机制
 * 同时也需要在pgs_remapp函数里面增加对页表页分配的特殊处理,以及新的默认参数
 */
void *PagetbHeapMgr_t::alloc_pgtb(phyaddr_t &phybase)
{
    void* result=nullptr;
    uint8_t unfull_PgtbHCB_count=0;
    constexpr uint8_t min_unfull_PgtbHCB_count=3;
    Module_lock.write_lock();
    result=first_static_Pgtb_HCB->alloc(phybase);
    if(result!=nullptr){
        Module_lock.write_unlock();
        return result;
    }
    PgtbHCB*candidate=nullptr;
    if(is_extensible){
        for(int i=0;i<MAX_PGTBHEAP_COUNT;i++){
            if(Pgtb_HCB_ARRAY[i]!=nullptr){
                if(!Pgtb_HCB_ARRAY[i]->is_full()){
                    if(candidate==nullptr)candidate=Pgtb_HCB_ARRAY[i];
                unfull_PgtbHCB_count++;
                if(unfull_PgtbHCB_count>=min_unfull_PgtbHCB_count)break;
                }
            }else{
                Pgtb_HCB_ARRAY[i]=new PgtbHCB();
                int status=Pgtb_HCB_ARRAY[i]->second_stage_init();
                if(status!=OS_SUCCESS){
                    Module_lock.write_unlock();
                    return nullptr;
                }
                if(candidate==nullptr)candidate=Pgtb_HCB_ARRAY[i];
                unfull_PgtbHCB_count++;
                if(unfull_PgtbHCB_count>=min_unfull_PgtbHCB_count)break;
            }
        }
    }
    Module_lock.write_unlock();
    return candidate->alloc(phybase);
}
// 在 PagetbHeapMgr.cpp 实现
void* PagetbHeapMgr_t::alloc_pgtb_no_reserve(phyaddr_t& phybase) {
    void* result = nullptr;

    Module_lock.read_lock();  // 先用读锁扫描（读多写少，性能好）

    // 第一优先：静态预留堆（总是存在的）
    result = first_static_Pgtb_HCB->alloc(phybase);
    if (result != nullptr) {
        Module_lock.read_unlock();
        return result;
    }

    // 扫描现有动态堆数组，只找非满的，找到就分配
    for (int i = 0; i < MAX_PGTBHEAP_COUNT; ++i) {
        if (Pgtb_HCB_ARRAY[i] != nullptr && !Pgtb_HCB_ARRAY[i]->is_full()) {
            // 切换到写锁，只锁这个分配
            Module_lock.read_unlock();
            Module_lock.write_lock();
            result = Pgtb_HCB_ARRAY[i]->alloc(phybase);
            Module_lock.write_unlock();
            if (result != nullptr) {
                return result;
            }
            // 如果分配失败（并发满了），继续下一个
            Module_lock.read_lock();
        }
    }

    Module_lock.read_unlock();
    return nullptr;  // 所有现有堆都满或不存在
}
int PagetbHeapMgr_t::free_pgtb_by_vaddr(void *vaddr)
{
    vaddr_t vaddr_base=(vaddr_t)vaddr;
    Module_lock.write_lock();
    if(first_static_Pgtb_HCB->free(vaddr)==OS_SUCCESS){
        Module_lock.write_unlock();
        return OS_SUCCESS;
    }
    if(is_extensible){
        for(int i=0;i<MAX_PGTBHEAP_COUNT;i++){
            if(Pgtb_HCB_ARRAY[i]!=nullptr){
                int status=Pgtb_HCB_ARRAY[i]->free(vaddr);
                if(status==OS_SUCCESS){
                    if(Pgtb_HCB_ARRAY[i]->is_empty()){
                        delete Pgtb_HCB_ARRAY[i];
                        Pgtb_HCB_ARRAY[i]=nullptr;
                    }
                    Module_lock.write_unlock();
                    return OS_SUCCESS;
                }else{
                    if(status==OS_OUT_OF_RANGE)continue;
                    else {
                        Module_lock.write_unlock();
                        return status;
                    }
                }
                
            }else{
                continue;
            }
        }
    }
    Module_lock.write_unlock();
    return OS_OUT_OF_RANGE;
}
int PagetbHeapMgr_t::free_pgtb_by_phyaddr(phyaddr_t phybase)
{
    Module_lock.write_lock();
    if(first_static_Pgtb_HCB->free(phybase)==OS_SUCCESS){
        Module_lock.write_unlock();
        return OS_SUCCESS;
    }
    if(is_extensible){
        for(int i=0;i<MAX_PGTBHEAP_COUNT;i++){
            if(Pgtb_HCB_ARRAY[i]!=nullptr){
                int status=Pgtb_HCB_ARRAY[i]->free(phybase);
                if(status==OS_SUCCESS){
                    if(Pgtb_HCB_ARRAY[i]->is_empty()){
                        delete Pgtb_HCB_ARRAY[i];
                        Pgtb_HCB_ARRAY[i]=nullptr;
                    }
                    Module_lock.write_unlock();
                    return OS_SUCCESS;
                }else{
                    if(status==OS_OUT_OF_RANGE)continue;
                    else {
                        Module_lock.write_unlock();
                        return status;
                    }
                }
                
            }else{
                continue;
            }
        }
    }
    Module_lock.write_unlock();
    return OS_OUT_OF_RANGE;
}

int PagetbHeapMgr_t::enable_extensible()
{
    is_extensible=true;
    return OS_SUCCESS;
}

void *PagetbHeapMgr_t::phyaddr_to_vaddr(phyaddr_t phybase)
{
    Module_lock.read_lock();
    vaddr_t result=first_static_Pgtb_HCB->phyaddr_to_vaddr(phybase);
    if(result){
        Module_lock.read_unlock();
        return (void*)result;
    }
    if(is_extensible){
        for(int i=0;i<MAX_PGTBHEAP_COUNT;i++){
            if(Pgtb_HCB_ARRAY[i]!=nullptr){
                result=Pgtb_HCB_ARRAY[i]->phyaddr_to_vaddr(phybase);
                if(result){
                    Module_lock.read_unlock();
                    return (void*)result;
                }
            }else{
                continue;
            }
        }
    }
    Module_lock.read_unlock();
    return nullptr;
}
PagetbHeapMgr_t::PgtbHCB::PgtbHCB()
{
    heapsize=0x400000;
}
int PagetbHeapMgr_t::PgtbHCB::second_stage_init()
{
    heapphybase=gPhyPgsMemMgr.pages_alloc(heapsize/(_4KB_SIZE),phygpsmemmgr_t::KERNEL,21);
    if(heapphybase==0)return OS_OUT_OF_MEMORY;
    heapvbase=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(heapphybase,heapsize,KSPACE_RW_ACCESS,0,false,false);
    if(heapvbase==0)return OS_MEMRY_ALLOCATE_FALT;
    bitmap=new PgthHCB_bitmap_t(heapsize);
    return OS_SUCCESS;
}
PagetbHeapMgr_t::PgtbHCB::~PgtbHCB()
{
    gKspacePgsMemMgr.pgs_remapped_free(heapvbase);
    delete bitmap;
    gPhyPgsMemMgr.pages_recycle(heapphybase,heapsize/(_4KB_SIZE));
}