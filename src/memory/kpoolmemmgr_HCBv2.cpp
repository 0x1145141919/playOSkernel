#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "linker_symbols.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "panic.h"
#ifdef USER_MODE
#include "stdlib.h"
#include "kpoolmemmgr.h"
#endif  
#ifdef USER_MODE
    constexpr uint64_t FIRST_STATIC_HEAP_SIZE=1ULL<<24;
    
    kpoolmemmgr_t::HCB_v2::HCB_v2()
    {
        bitmap_controller.Init();
    }
#endif
 kpoolmemmgr_t::HCB_v2::HCB_v2()
{
    
}
kpoolmemmgr_t::HCB_v2::HCB_v2(uint32_t size, vaddr_t vbase)
{
    this->total_size_in_bytes=size;
    this->vbase=vbase;
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::param_checkment(uint64_t bit_idx, uint64_t bit_count)
{
    if(bit_idx+bit_count>bitmap_size_in_64bit_units*64){
        return kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t::HCB_BITMAP_BAD_PARAM;
    }
    if(bit_count>bitmap_size_in_64bit_units*64){
        return kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t::TOO_BIG_MEM_DEMAND;
    }
    return kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t::SUCCESS;
}
int kpoolmemmgr_t::HCB_v2::HCB_bitmap::Init()
{
    if(this!=&kpoolmemmgr_t::first_linekd_heap.bitmap_controller)
    {
        return OS_BAD_FUNCTION;
    }
    uint32_t bitmap_size_in_byte=0;
#ifdef KERNEL_MODE
    this->bitmap=(uint64_t*)&__heap_bitmap_start;
    bitmap_size_in_byte=((uint64_t)&__heap_bitmap_end-(uint64_t)&__heap_bitmap_start);
#endif
#ifdef USER_MODE
    this->bitmap=(uint64_t*)malloc(FIRST_STATIC_HEAP_SIZE/128);
    bitmap_size_in_byte=FIRST_STATIC_HEAP_SIZE/128;
#endif
    bitmap_size_in_64bit_units=bitmap_size_in_byte/8;
    byte_bitmap_base=(uint8_t*)this->bitmap;
    setmem(this->bitmap,bitmap_size_in_byte,0);
    bitmap_used_bit=0;
    return OS_SUCCESS;
}
KURD_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB_BITMAP,0,0,err_domain::CORE_MODULE);
}
KURD_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::default_fail()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
KURD_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}
KURD_t kpoolmemmgr_t::HCB_v2::HCB_bitmap::second_stage_Init(uint32_t entries_count)
{
    KURD_t success=default_success();
    KURD_t fail=default_fail();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_BITMAP_EVENTS::EVENT_CODE_INIT;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_BITMAP_EVENTS::EVENT_CODE_INIT;
    if(this==&kpoolmemmgr_t::first_linekd_heap.bitmap_controller){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_BITMAP_EVENTS::INIT_RESULTS::FAIL_RESONS::REASON_CODE_HCB_BITMAP_INIT_FAIL;
    }
    bitmap_size_in_64bit_units=entries_count/64;
    KURD_t kurd;
#ifdef KERNEL_MODE
    this->bitmap=(uint64_t*)__wrapped_pgs_valloc(
     &kurd,align_up(bitmap_size_in_64bit_units,512)/512,KERNEL,12
    );
#endif
#ifdef USER_MODE
    this->bitmap=(uint64_t*)malloc((bitmap_size_in_64bit_units*8));
#endif
    if(this->bitmap==nullptr||kurd.result!=result_code::SUCCESS)return kurd;

    byte_bitmap_base=(uint8_t*)this->bitmap;
    return success;
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap::HCB_bitmap()
{
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap()
{
    byte_bitmap_base=nullptr;
    #ifdef KERNEL_MODE
   phyaddr_t bitmap_phyaddr;
    KURD_t status=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)this->bitmap,bitmap_phyaddr);
    status=KspaceMapMgr::pgs_remapped_free((vaddr_t)this->bitmap);
    panic_info_inshort inshort={
        .is_bug=true,
        .is_policy=false,
        .is_hw_fault=false,
        .is_mem_corruption=false,
        .is_escalated=false,  
    };
    if(status.result!=result_code::SUCCESS){
        Panic::panic(default_panic_behaviors_flags,"kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap cancel memmap failed",nullptr,&inshort,status);
    }
    status=phymemspace_mgr::pages_recycle(bitmap_phyaddr,bitmap_size_in_64bit_units*8/4096);
    if(status.result!=result_code::SUCCESS){
        Panic::panic(default_panic_behaviors_flags,"kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap recycle phy pages failed",nullptr,&inshort,status);
    }
    #endif
    #ifdef USER_MODE
    std::free(this->bitmap);
#endif
    this->bitmap=nullptr;
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t
 kpoolmemmgr_t::HCB_v2::HCB_bitmap::
continual_avaliable_u64s_search_higher_alignment(
    uint64_t u64idx_align_log2,
    uint64_t u64_count,
    uint64_t &result_base_idx
)
{
    if (u64_count == 0)
        return SUCCESS;

    const uint64_t total_u64s = bitmap_size_in_64bit_units;

    // 用完即弃的对齐计算
    auto align_u64_idx = [&](uint64_t idx) -> uint64_t {
        if (u64idx_align_log2 == 0)
            return idx;
        const uint64_t align = 1ULL << u64idx_align_log2;
        return (idx + align - 1) & ~(align - 1);
    };

    uint64_t seg_base_u64_idx = 0;
    bool seg_value = !!bitmap[0];
    uint64_t seg_length = 1;

    while (seg_base_u64_idx + seg_length < total_u64s) {
        uint64_t cur_idx = seg_base_u64_idx + seg_length;
        bool new_value = !!bitmap[cur_idx];

        if (new_value == seg_value) {
            seg_length++;
        } else {
            // 完整 free 段处理
            if (!seg_value) {
                uint64_t seg_end = seg_base_u64_idx + seg_length;
                uint64_t aligned_base = align_u64_idx(seg_base_u64_idx);

                if (aligned_base >= seg_base_u64_idx &&
                    aligned_base + u64_count <= seg_end) {
                    result_base_idx = aligned_base;
                    return SUCCESS;
                }
            }

            // 切换段
            seg_base_u64_idx = cur_idx;
            seg_value = new_value;
            seg_length = 1;
        }
    }

    // 最后一个段
    if (!seg_value) {
        uint64_t seg_end = seg_base_u64_idx + seg_length;
        uint64_t aligned_base = align_u64_idx(seg_base_u64_idx);

        if (aligned_base >= seg_base_u64_idx &&
            aligned_base + u64_count <= seg_end) {
            result_base_idx = aligned_base;
            return SUCCESS;
        }
    }

    return AVALIBLE_MEMSEG_SEARCH_FAIL;
}

bool kpoolmemmgr_t::HCB_v2::HCB_bitmap::target_bit_seg_is_avaliable
(uint64_t bit_idx, 
    uint64_t bit_count,
kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t&err)
{
    if (bit_count == 0) return true;
    err=param_checkment(bit_idx,bit_count);
    if(err!=SUCCESS){
        return false;
    }

    bitmap_rwlock.read_lock();

    uint64_t start_bit = bit_idx;
    uint64_t end_bit = bit_idx + bit_count;

    uint64_t start_u64 = start_bit >> 6;
    uint64_t end_u64   = (end_bit - 1) >> 6;  // inclusive
    uint64_t start_off = start_bit & 63;
    uint64_t end_off   = end_bit & 63;

    // 1️⃣ 如果目标区间小于64bit，直接逐bit检查，可读性高
    if (bit_count <= 64)
    {
        for (uint64_t i = 0; i < bit_count; ++i)
        {
            if (bit_get(bit_idx + i))
            {
                bitmap_rwlock.read_unlock();
                return false;
            }
        }
        bitmap_rwlock.read_unlock();
        return true;
    }

    // 2️⃣ 跨越多个 u64 块的情况
    // ---- 前导未对齐部分 ----
    if (start_off != 0)
    {
        uint64_t mask = (~0ULL) << start_off;
        if (bitmap[start_u64] & mask)
        {
            bitmap_rwlock.read_unlock();
            return false;
        }
        start_u64++;
    }

    // ---- 中间完整的 u64 区域 ----
    for (uint64_t i = start_u64; i < end_u64; ++i)
    {
        if (bitmap[i] != 0ULL)
        {
            bitmap_rwlock.read_unlock();
            return false;
        }
    }

    // ---- 尾部未对齐部分 ----
    if (end_off != 0)
    {
        uint64_t mask = (1ULL << end_off) - 1ULL;
        if (bitmap[end_u64] & mask)
        {
            bitmap_rwlock.read_unlock();
            return false;
        }
    }

    bitmap_rwlock.read_unlock();
    return true;
}

kpoolmemmgr_t::HCB_v2::HCB_bitmap_error_code_t
 kpoolmemmgr_t::HCB_v2::HCB_bitmap::bit_seg_set(uint64_t bit_idx, uint64_t bit_count, bool value)
{
    if (bit_count == 0) return SUCCESS;

    HCB_bitmap_error_code_t status=param_checkment(bit_idx,bit_count);
    if(status!=SUCCESS){
        return status;
    }

    bitmap_rwlock.write_lock();

    uint64_t end_bit = bit_idx + bit_count;
    uint64_t start_align64 = (bit_idx + 63) & ~63ULL;   // 下一个64位边界
    uint64_t end_align64 = end_bit & ~63ULL;            // 末尾对齐边界

    // ----------- 1. 处理前导未对齐部分 -----------
    if (bit_idx < start_align64 && bit_idx < end_bit)
    {
        uint64_t pre_bits = (start_align64 > end_bit ? end_bit : start_align64) - bit_idx;
        bits_set(bit_idx, pre_bits, value);
        bit_idx += pre_bits;
    }

    // ----------- 2. 处理中间完整的u64块 -----------
    if (bit_idx < end_align64)
    {
        uint64_t u64_start_idx = bit_idx >> 6;               // /64
        uint64_t u64_count = (end_align64 - bit_idx) >> 6;   // /64
        u64s_set(u64_start_idx, u64_count, value);
        bit_idx += u64_count << 6;
    }

    // ----------- 3. 处理尾部未对齐部分 -----------
    if (bit_idx < end_bit)
    {
        bits_set(bit_idx, end_bit - bit_idx, value);
    }

    bitmap_rwlock.write_unlock();
    return SUCCESS;
}

int kpoolmemmgr_t::HCB_v2::first_linekd_heap_Init()
{
    if(this!=&kpoolmemmgr_t::first_linekd_heap)return OS_BAD_FUNCTION;
    #ifdef KERNEL_MODE
    int status=this->bitmap_controller.Init();
    phybase=(phyaddr_t)&_heap_lma;
    vbase=(vaddr_t)&__heap_start;
    total_size_in_bytes=(uint64_t)&__heap_end-(uint64_t)&__heap_start;
    return status;  
    #endif
     
    #ifdef USER_MODE
    vbase=(vaddr_t)malloc(FIRST_STATIC_HEAP_SIZE);
    phybase=vbase;
    total_size_in_bytes=FIRST_STATIC_HEAP_SIZE;
    if(vbase==NULL)return OS_OUT_OF_MEMORY;
    return OS_SUCCESS;
    #endif
}


KURD_t kpoolmemmgr_t::HCB_v2::second_stage_Init()
{
    KURD_t success=default_success();
    KURD_t  fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INIT;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INIT;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INIT;
    if(this==&kpoolmemmgr_t::first_linekd_heap){
        fail.module_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INIT_RESULTS::FAIL_RESONS::REASON_CODE_first_linekd_heap_NOT_ALLOWED;
        return fail;
    }
    
    KURD_t contain;
    this->phybase=__wrapped_pgs_alloc(&contain,total_size_in_bytes/0x1000,KERNEL,21);
    if(!success_all_kurd(contain))return contain;
    #ifdef KERNEL_MODE
    VM_DESC desc={
        .start=vbase,
        .end=vbase+total_size_in_bytes,
        .map_type=VM_DESC::MAP_PHYSICAL,
        .phys_start=phybase,
        .access=KspaceMapMgr::PG_RW,
        .committed_full=true,
        .is_vaddr_alloced=true,
        .is_out_bound_protective=false,
    };
    contain=KspaceMapMgr::enable_VMentry(desc);
    if(!success_all_kurd(contain))return contain;
    #endif
    
    #ifdef USER_MODE
    
    vbase=(vaddr_t)malloc(total_size_in_bytes);
    if(vbase==NULL)return OS_OUT_OF_MEMORY;
    phybase=vbase;
    #endif
    KURD_t status=bitmap_controller.second_stage_Init(
        total_size_in_bytes/bytes_per_bit
    );
    return status;
}
kpoolmemmgr_t::HCB_v2::~HCB_v2()
{
    bitmap_controller.~HCB_bitmap();
    KURD_t status=KURD_t();
    #ifdef KERNEL_MODE
    VM_DESC desc={
        .start=vbase,
        .end=vbase+total_size_in_bytes,
        .map_type=VM_DESC::MAP_PHYSICAL,
        .phys_start=phybase,
        .access=KspaceMapMgr::PG_RW,
        .committed_full=true,
        .is_vaddr_alloced=true,
        .is_out_bound_protective=false,
    };
    status=KspaceMapMgr::disable_VMentry(desc);
    if(!success_all_kurd(status))goto free_wrong;
    status=__wrapped_pgs_free(
        phybase,
        total_size_in_bytes/0x1000
    );
    if(!success_all_kurd(status))goto free_wrong;
    #endif
    #ifdef USER_MODE
    std::free((void*)vbase);
    return;
    #endif
free_wrong:    
    panic_info_inshort inshort={
        .is_bug=true,
        .is_policy=false,
        .is_hw_fault=false,
        .is_mem_corruption=false,
        .is_escalated=false,  
    };
    if(status.result!=result_code::SUCCESS){
        Panic::panic(default_panic_behaviors_flags,"kpoolmemmgr_t::HCB_v2::~HCB_v2 cancel memmap failed",nullptr,&inshort,status);
    }
    status=phymemspace_mgr::pages_recycle(phybase,total_size_in_bytes/4096);
    if(status.result!=result_code::SUCCESS){
        Panic::panic(default_panic_behaviors_flags,"kpoolmemmgr_t::HCB_v2::~HCB_v2 recycle phy pages failed",nullptr,&inshort,status);
    }
}
KURD_t kpoolmemmgr_t::HCB_v2::clear(void *ptr)
{
    KURD_t success=default_success();
    KURD_t  fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_CLEAR;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_CLEAR;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_CLEAR;
    if (!is_addr_belong_to_this_hcb(ptr)){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::CLEAR_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;
    }

    // ptr 是用户可见地址，meta 在其前面
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    if (meta->magic != MAGIC_ALLOCATED)
    {
        fatal.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::CLEAR_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED;
        return fatal;
    }

    // 清零用户数据区域（不清 meta）
    // setmem 的原型假设为 setmem(void* addr, size_t size, int value)
    setmem(ptr, meta->data_size, 0);

    return success;
}
KURD_t kpoolmemmgr_t::HCB_v2::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB,0,0,err_domain::CORE_MODULE);
}

KURD_t kpoolmemmgr_t::HCB_v2::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}

KURD_t kpoolmemmgr_t::HCB_v2::default_fail()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}

KURD_t kpoolmemmgr_t::HCB_v2::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}

/**
 * @brief 在堆中分配内存
 * 
 * 根据请求的大小和对齐要求，在堆中分配一块内存区域。
 * 分配策略根据大小分为三种：小块(4位对齐)、中块(7位对齐)和大块(10位对齐)
 * 
 * @param addr 返回分配的内存地址
 * @param size 请求分配的内存大小(字节)
 * @param is_crucial 是否为关键变量，当内存损坏时决定是否触发内核恐慌
 * @param vaddraquire true返回虚拟地址，false返回物理地址
 * @param alignment 对齐要求，实际对齐值为2^alignment字节对齐，最大支持10(1024字节对齐)
 * @return int OS_SUCCESS表示成功分配，其他值表示分配失败
 */
KURD_t kpoolmemmgr_t::HCB_v2::in_heap_alloc(
    void *&addr,
    uint32_t size,
    alloc_flags_t flags)
{
    KURD_t success=default_success();
    KURD_t  fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_ALLOC;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_ALLOC;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_ALLOC;
    // -----------------------------
    // 基本参数与边界检查
    // -----------------------------
    if (flags.align_log2 >=32)
        {
            fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_TOO_HIGH_ALIGN_DEMAND;
            return fail;
        }
    if (size == 0)
        {
            fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SIZE_DEMAND_IS_ZERO;
            return fail;
        }
    if(size>=1ULL<<16){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SIZE_DEMAND_TOO_LARGE;
        return fail;
    }
    // 常量定义（在内核空间必须为常量或宏）
    static constexpr uint32_t SMALL_UNIT_BYTES=16;
    static constexpr uint32_t MID_UNIT_BYTES = SMALL_UNIT_BYTES<<3;
    static constexpr uint32_t LARGE_UNIT_BYTES = MID_UNIT_BYTES<<3;
    // -----------------------------
    // 计算对齐方式
    // -----------------------------
    uint8_t size_alignment = 0;
    uint8_t aquire_alignment = 0;
    uint8_t final_alignment_aquire;

    // 根据请求大小确定对齐档次
    if (size < 2 * 8 * bytes_per_bit)
        size_alignment = 4;    // 16B 对齐
    else if (size < 2 * 64 * bytes_per_bit)
        size_alignment = 7;    // 128B 对齐
    else
        size_alignment = 10;   // 1024B 对齐

    // 根据 alignment 参数映射档次
    if (flags.align_log2 <= 4)
        aquire_alignment = 4;
    else if (flags.align_log2 <= 7)
        aquire_alignment = 7;
    else if (flags.align_log2 <= 10)
        aquire_alignment = 10;
    else aquire_alignment = flags.align_log2;
    // 最终取较大者
    final_alignment_aquire = (size_alignment > aquire_alignment) ?
                             size_alignment : aquire_alignment;

    // -----------------------------
    // 分配逻辑（不同对齐档）
    // -----------------------------
    int status = OS_SUCCESS;
    uintptr_t base_addr = (uintptr_t)(flags.vaddraquire ? vbase : phybase);

    switch (final_alignment_aquire)
    {
    case 4: {  // 小块分配 (16字节对齐)
        uint32_t serial_bits_count = (size + SMALL_UNIT_BYTES - 1 + sizeof(data_meta)) >> 4;
        uint64_t base_bit_idx = 0;
        bitmap_controller.bitmap_rwlock.read_lock();
        status = bitmap_controller.continual_avaliable_bits_search(serial_bits_count, base_bit_idx);
        bitmap_controller.bitmap_rwlock.read_unlock();
        if (status != OS_SUCCESS)
            {
                fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SEARCH_MEMSEG_FAIL;
                return fail;
            }
        bitmap_controller.bit_seg_set(base_bit_idx, serial_bits_count,true);
        bitmap_controller.used_bit_count_lock.lock();
        bitmap_controller.bitmap_used_bit+=serial_bits_count;
        uintptr_t offset = bytes_per_bit * (base_bit_idx + 1);
        bitmap_controller.used_bit_count_lock.unlock();
        addr = (void *)(base_addr + offset);
        break;
    }
    case 7: {  // 中块分配 (128字节对齐)
        uint32_t serial_bytes_count = ((size + MID_UNIT_BYTES - 1) >> 7) + 1;
        uint64_t base_byte_idx = 0;
        bitmap_controller.bitmap_rwlock.read_lock();
        status = bitmap_controller.continual_avaliable_bytes_search(serial_bytes_count, base_byte_idx);
        bitmap_controller.bitmap_rwlock.read_unlock();
        if (status != OS_SUCCESS)
            {
                fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SEARCH_MEMSEG_FAIL;
                return fail;
            }

        bitmap_controller.bit_set(7 + (base_byte_idx << 3) , true);
        bitmap_controller.used_bit_count_lock.lock();
        bitmap_controller.bitmap_used_bit++;
        uint32_t total_bitcount = (size + SMALL_UNIT_BYTES - 1) >> 4;
        bitmap_controller.bit_seg_set((base_byte_idx+1)*8, total_bitcount,true);
        bitmap_controller.bitmap_used_bit+=total_bitcount;
        bitmap_controller.used_bit_count_lock.unlock();
        uintptr_t offset = bytes_per_bit * ((base_byte_idx + 1) << 3);
        addr = (void *)(base_addr + offset);
        break;
    }
    case 10: {  // 大块分配 (1024字节对齐)
        uint32_t serial_u64_count = ((size + LARGE_UNIT_BYTES - 1) >> 10) + 1;
        uint64_t base_u64_idx = 0;
        bitmap_controller.bitmap_rwlock.read_lock();
        status = bitmap_controller.continual_avaliable_u64s_search(serial_u64_count, base_u64_idx);
        bitmap_controller.bitmap_rwlock.read_unlock();
        if (status != OS_SUCCESS)
            {
                fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SEARCH_MEMSEG_FAIL;
                return fail;
            }

        bitmap_controller.bit_set( 63 + (base_u64_idx << 6), true);
        bitmap_controller.used_bit_count_lock.lock();
        bitmap_controller.bitmap_used_bit++;
        uint32_t total_bitcount = (size + SMALL_UNIT_BYTES - 1) >> 4;
        bitmap_controller.bit_seg_set((base_u64_idx+1)*64, total_bitcount,true);
        bitmap_controller.bitmap_used_bit += total_bitcount;
        bitmap_controller.used_bit_count_lock.unlock();
        uintptr_t offset = bytes_per_bit * ((base_u64_idx + 1) << 6);
        addr = (void *)(base_addr + offset);
        break;
    }
    default:
    {
    if (final_alignment_aquire <= 10)
        {
            fatal.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FATAL_REASONS::REASON_CODE_ALIGN_DEMAND_INVALID;
            return fatal;
        }

    // ------------------------------------
    // 对齐参数（u64 粒度）
    // ------------------------------------
    const uint8_t u64_align_log2 = final_alignment_aquire - 10;
    const uint64_t u64_align = 1ULL << u64_align_log2;

    // ------------------------------------
    // 真实 payload 需要的 u64 数量
    // ------------------------------------
    const uint32_t payload_u64_count =
        (align_up(size, LARGE_UNIT_BYTES)) >> 10;

    // 最坏情况下需要的搜索长度（payload + padding）
    const uint32_t search_u64_count =
        align_up(payload_u64_count, u64_align)+ u64_align;

    uint64_t base_u64_idx = 0;

    // ------------------------------------
    // 搜索（只保证：有一个对齐起点能放下 payload）
    // ------------------------------------
    bitmap_controller.bitmap_rwlock.read_lock();
    status = bitmap_controller.continual_avaliable_u64s_search_higher_alignment(
        u64_align_log2,
        search_u64_count,
        base_u64_idx
    );
    bitmap_controller.bitmap_rwlock.read_unlock();

    if (status != OS_SUCCESS)
        {
                fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_SEARCH_MEMSEG_FAIL;
                return fail;
            }

    // ------------------------------------
    // 计算真正的对齐起点
    // ------------------------------------
    uint64_t real_base_u64 =
        base_u64_idx+u64_align ;

    uint64_t real_base_bit = real_base_u64 << 6;

    // ------------------------------------
    // 标记 bitmap
    // sentinel: real_base_bit - 1
    // ------------------------------------
    bitmap_controller.bit_set(real_base_bit - 1, true);

    bitmap_controller.used_bit_count_lock.lock();
    bitmap_controller.bitmap_used_bit++;

    const uint32_t total_bitcount =
        (size + SMALL_UNIT_BYTES - 1) >> 4;

    bitmap_controller.bit_seg_set(
        real_base_bit,
        total_bitcount,
        true
    );

    bitmap_controller.bitmap_used_bit += total_bitcount;
    bitmap_controller.used_bit_count_lock.unlock();

    // ------------------------------------
    // 返回地址（与 real_base_u64 严格一致）
    // ------------------------------------
    uintptr_t offset = bytes_per_bit * real_base_bit;
    addr = (void *)(base_addr + offset);

    break;
    }

    }

    // -----------------------------
    // 设置元数据（data_meta）
    // -----------------------------
    data_meta *meta = (data_meta *)((uint8_t *)addr - sizeof(data_meta));
    meta->magic = MAGIC_ALLOCATED;
    meta->data_size = size;
    meta->alloc_flags= flags;
    return success;
}

KURD_t kpoolmemmgr_t::HCB_v2::free(void *ptr)
{
    KURD_t success=default_success();
    KURD_t  fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_FREE;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_FREE;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_FREE;
    if(uint64_t(ptr)&15||!is_addr_belong_to_this_hcb(ptr)){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    }
    uint32_t in_heap_offset;
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
    if((uint64_t)ptr<MIN_KVADDR)
    {//物理地址
        if((uint64_t)ptr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)ptr
    ){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    }
    in_heap_offset=(uint64_t)ptr-(uint64_t)phybase;
    }else{
        if((uint64_t)ptr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)ptr){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    }
    in_heap_offset=(uint64_t)ptr-(uint64_t)vbase;
    }
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    if(meta->magic != MAGIC_ALLOCATED)
    {
        fatal.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED;
        return fatal;
    }
    uint32_t total_bits_count=(meta->data_size+15)/16;
    uint32_t base_bit_idx=in_heap_offset/16;
    bitmap_controller.bitmap_rwlock.write_lock();
    bitmap_controller.bit_set(base_bit_idx-1,false);
    bitmap_controller.bitmap_rwlock.write_unlock();
    bitmap_controller.used_bit_count_lock.lock();
    bitmap_controller.bitmap_used_bit--;
    bitmap_controller.bitmap_used_bit-=total_bits_count;
    bitmap_controller.used_bit_count_lock.unlock();
    bitmap_controller.bit_seg_set(
        base_bit_idx,
        total_bits_count,
        false
    );
    return success;
}

KURD_t kpoolmemmgr_t::HCB_v2::in_heap_realloc(
    void *&ptr, 
    uint32_t new_size,
    alloc_flags_t flags)
{
    KURD_t success=default_success();
    KURD_t  fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC;
    if(uint64_t(ptr)&15||!is_addr_belong_to_this_hcb(ptr)){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    }  //本堆的内存至少16字节对齐
    uint32_t in_heap_offset;
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
    if((uint64_t)ptr<MIN_KVADDR)
    {//物理地址
        if((uint64_t)ptr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)ptr
    ){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    } 
    in_heap_offset=(uint64_t)ptr-(uint64_t)phybase;
    }else{
        if((uint64_t)ptr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)ptr){
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FAIL_RESONS::REASON_CODE_BAD_ADDR;
        return fail;   
    } 
    in_heap_offset=(uint64_t)ptr-(uint64_t)vbase;
    }
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    uint16_t old_size=meta->data_size;
    if(meta->magic != MAGIC_ALLOCATED)
    {
        fatal.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::FREE_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED;
        return fatal;
    }uint32_t old_bits_count=(meta->data_size+15)/16;
    uint32_t base_bit_idx=in_heap_offset/16;
    uint32_t new_bits_count=(new_size+15)/16;
    if(!flags.is_when_realloc_force_new_addr)
    {
    if(new_bits_count<=old_bits_count)
    {
        meta->data_size=new_size;
        bitmap_controller.bit_seg_set(
            base_bit_idx+new_bits_count,
            old_bits_count-new_bits_count,
            false
        );
        bitmap_controller.used_bit_count_lock.lock();
        bitmap_controller.bitmap_used_bit-=(old_bits_count-new_bits_count);
        bitmap_controller.used_bit_count_lock.unlock();

        return success;
    }else{
        HCB_bitmap_error_code_t err;
        bool try_append=bitmap_controller.target_bit_seg_is_avaliable(
            base_bit_idx+old_bits_count,
            new_bits_count-old_bits_count,
            err
        );
        if(err!=SUCCESS)try_append=false;
        if(try_append)
        {
             bitmap_controller.bit_seg_set(
                base_bit_idx+old_bits_count,
                new_bits_count-old_bits_count,
                true
            );
            bitmap_controller.used_bit_count_lock.lock();
            bitmap_controller.bitmap_used_bit+=(new_bits_count-old_bits_count);
            bitmap_controller.used_bit_count_lock.unlock();
            return success;
        }else{
            void*new_ptr;
            KURD_t status=in_heap_alloc(
                new_ptr,new_size,flags
            );
            if(status.result==result_code::SUCCESS){
                ksystemramcpy(ptr,new_ptr,old_size);
                status=free(ptr);
                ptr=new_ptr;
                bitmap_controller.used_bit_count_lock.lock();
                bitmap_controller.bitmap_used_bit+=(new_bits_count-old_bits_count);
                bitmap_controller.used_bit_count_lock.unlock();
                return success;
            }else{
                return status;
            }
        }
    }
    }else{
        void*new_ptr;
            KURD_t status=in_heap_alloc(
                new_ptr,new_size,flags
            );
            if(status.result==result_code::SUCCESS){
                ksystemramcpy(ptr,new_ptr,old_size);
                status=free(ptr);
                ptr=new_ptr;
                bitmap_controller.used_bit_count_lock.lock();
                bitmap_controller.bitmap_used_bit+=(new_bits_count-old_bits_count);
                bitmap_controller.used_bit_count_lock.unlock();
                return success;
            }else{
                return status;
            }
    }fatal.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_UNREACHABLE_CODE;
    return fatal;
}

uint64_t kpoolmemmgr_t::HCB_v2::get_used_bytes_count()
{
    return this->bitmap_controller.bitmap_used_bit*this->bytes_per_bit;
}

bool kpoolmemmgr_t::HCB_v2::is_full()
{
    bitmap_controller.used_bit_count_lock.lock();
    uint64_t used_bit_count=bitmap_controller.bitmap_used_bit;
    bitmap_controller.used_bit_count_lock.unlock();
    return used_bit_count==bitmap_controller.bitmap_size_in_64bit_units*64 ;
}
void kpoolmemmgr_t::HCB_v2::count_used_bytes()
{
    bitmap_controller.count_bitmap_used_bit();
}

bool kpoolmemmgr_t::HCB_v2::is_addr_belong_to_this_hcb(void *addr)
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
    if((uint64_t)addr<MAX_PHYADDR)
    {//物理地址
        if((uint64_t)addr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)addr
    )return false;

    }else{
        if(MAX_PHYADDR<=(uint64_t)addr&&(uint64_t)addr<MIN_KVADDR)return false;
        if((uint64_t)addr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)addr)return false;

    }
    return true;
}
phyaddr_t kpoolmemmgr_t::HCB_v2::tran_to_phy(void *addr)
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
    if((uint64_t)addr<MAX_PHYADDR)return (phyaddr_t)addr;
    if(is_addr_belong_to_this_hcb(addr)){
        return (phyaddr_t)((uint64_t)addr-vbase+phybase);
    }
    return 0;
}
vaddr_t kpoolmemmgr_t::HCB_v2::tran_to_virt(phyaddr_t addr)
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
    if(MIN_KVADDR<=addr)return (vaddr_t)addr;
    if(is_addr_belong_to_this_hcb((void*)addr)){
        return (vaddr_t)((uint64_t)addr-phybase+vbase);
    }
    return 0;
}
