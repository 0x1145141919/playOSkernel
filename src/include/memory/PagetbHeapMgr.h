#include "stdint.h"
#include "memory/Memory.h"
#include "memory/kpoolmemmgr.h"
#include "memory/pgtable45.h"
#include "util/bitmap.h"
#include <lock.h>
class PagetbHeapMgr_t { 
    private:
    spinrwlock_cpp_t Module_lock;
    class PgtbHCB{
        private:
        vaddr_t heapvbase;//2m对齐
        phyaddr_t heapphybase;//2m对齐
        uint64_t heapsize;//2m对齐
        class PgthHCB_bitmap_t:public bitmap_t{
            public:
            PgthHCB_bitmap_t(uint64_t heap_size);
            ~PgthHCB_bitmap_t();
            void bitmap_write_lock(){bitmap_rwlock.write_lock();};
            void bitmap_write_unlock(){bitmap_rwlock.write_unlock();};
            void bitmap_read_lock(){bitmap_rwlock.read_lock();};
            void bitmap_read_unlock(){bitmap_rwlock.read_unlock();};
        };
        PgthHCB_bitmap_t* bitmap;
        spinlock_cpp_t write_lock;
        public:
        PgtbHCB(bool is_init);//静态初始化使用
        PgtbHCB();//后面默认的构造函数，动态注册使用，未完成
        int second_stage_init();
        ~PgtbHCB();//后面默认的析构函数，动态注销使用，未完成
        void* alloc(phyaddr_t&phybase);    
        int free(void*vaddr);
        int free(phyaddr_t phybase);
        vaddr_t phyaddr_to_vaddr(phyaddr_t phybase);
        bool is_full();
        bool is_empty();
    };
    static constexpr uint64_t MAX_PGTBHEAP_COUNT=4096;
    PgtbHCB*first_static_Pgtb_HCB;
    PgtbHCB* Pgtb_HCB_ARRAY[MAX_PGTBHEAP_COUNT];
    bool is_extensible;
    bool is_inited;
    public:
    int Init();
    void* alloc_pgtb(phyaddr_t&phybase);
    int free_pgtb_by_vaddr(void*vaddr);
    int free_pgtb_by_phyaddr(phyaddr_t phybase);
    int enable_extensible();
    void*phyaddr_to_vaddr(phyaddr_t phybase);
};
extern PagetbHeapMgr_t gPgtbHeapMgr;