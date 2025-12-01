#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "linker_symbols.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "panic.h"
constexpr uint64_t MIN_VIR_ADDR=0xff00000000000000;
int kpoolmemmgr_t::HCB_v2::HCB_bitmap::Init()
{
    if(this!=&gKpoolmemmgr.first_linekd_heap.bitmap_controller)
    {
        return OS_BAD_FUNCTION;
    }
    this->bitmap=(uint64_t*)&__heap_bitmap_start;
    uint32_t bitmap_size_in_byte=((uint64_t)&__heap_bitmap_end-(uint64_t)&__heap_bitmap_start);
    bitmap_size_in_64bit_units=bitmap_size_in_byte/8;
    byte_bitmap_base=(uint8_t*)this->bitmap;
    setmem(this->bitmap,bitmap_size_in_byte,0);
    bitmap_used_bit=0;
    return OS_SUCCESS;
}

int kpoolmemmgr_t::HCB_v2::HCB_bitmap::second_stage_Init(uint32_t entries_count)
{
    if(this==&gKpoolmemmgr.first_linekd_heap.bitmap_controller)return OS_BAD_FUNCTION;
    bitmap_size_in_64bit_units=entries_count/64;
    phyaddr_t bitmap_phybase=gPhyPgsMemMgr.pages_alloc(
        (bitmap_size_in_64bit_units*8)/4096,
        phygpsmemmgr_t::KERNEL
    );
    if(bitmap_phybase==0)
    {
        return OS_OUT_OF_MEMORY;
    }
    this->bitmap=(uint64_t*)gKspacePgsMemMgr.pgs_remapp(bitmap_phybase,bitmap_size_in_64bit_units*8,KSPACE_RW_ACCESS);
    if(this->bitmap==nullptr)return OS_MEMRY_ALLOCATE_FALT;
    byte_bitmap_base=(uint8_t*)this->bitmap;
    return OS_SUCCESS;
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap()
{
    byte_bitmap_base=nullptr;
    phyaddr_t bitmap_phyaddr;
    int status=gKspacePgsMemMgr.v_to_phyaddrtraslation((vaddr_t)this->bitmap,bitmap_phyaddr);
    status=gKspacePgsMemMgr.pgs_remapped_free((vaddr_t)this->bitmap);
    
    if(status!=OS_SUCCESS){
        gkernelPanicManager.panic("kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap cancel memmap failed");
    }
    status=gPhyPgsMemMgr.pages_recycle(bitmap_phyaddr,bitmap_size_in_64bit_units*8/4096);
    if(status!=OS_SUCCESS){
        gkernelPanicManager.panic("kpoolmemmgr_t::HCB_v2::HCB_bitmap::~HCB_bitmap recycle phy pages failed");
    }
    this->bitmap=nullptr;
}
kpoolmemmgr_t::HCB_v2::HCB_bitmap::HCB_bitmap()
{
    bitmap_used_bit=0;
}
bool kpoolmemmgr_t::HCB_v2::HCB_bitmap::target_bit_seg_is_avaliable(uint64_t bit_idx, uint64_t bit_count)
{
    if (bit_count == 0) return true;

    uint64_t total_bits = bitmap_size_in_64bit_units * 64;
    if (bit_idx >= total_bits || bit_idx + bit_count > total_bits)
        return false;

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

int kpoolmemmgr_t::HCB_v2::HCB_bitmap::bit_seg_set(uint64_t bit_idx, uint64_t bit_count, bool value)
{
    if (bit_count == 0) return OS_SUCCESS;

    uint64_t total_bits = bitmap_size_in_64bit_units * 64;
    if (bit_idx >= total_bits || bit_idx + bit_count > total_bits)
        return OS_INVALID_PARAMETER;

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
    return OS_SUCCESS;
}

int kpoolmemmgr_t::HCB_v2::first_linekd_heap_Init()
{
    if(this!=&gKpoolmemmgr.first_linekd_heap)return OS_BAD_FUNCTION;
    int status=this->bitmap_controller.Init();
    phybase=(phyaddr_t)&_heap_lma;
    vbase=(vaddr_t)&__heap_start;
    total_size_in_bytes=(uint64_t)&__heap_end-(uint64_t)&__heap_start;
    return status;   
}

kpoolmemmgr_t::HCB_v2::HCB_v2(uint32_t apic_id)
{
  total_size_in_bytes=0x200000;
  belonged_to_cpu_apicid=apic_id;
}
int kpoolmemmgr_t::HCB_v2::second_stage_Init()
{
    if(this==&gKpoolmemmgr.first_linekd_heap)return OS_BAD_FUNCTION;
    phybase=gPhyPgsMemMgr.pages_alloc(total_size_in_bytes/4096,phygpsmemmgr_t::KERNEL);
    if(phybase==0)return OS_OUT_OF_MEMORY;
    vbase=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(phybase,total_size_in_bytes,KSPACE_RW_ACCESS);
    if(vbase==0){
        gPhyPgsMemMgr.pages_recycle(phybase,total_size_in_bytes/4096);
        return OS_MEMRY_ALLOCATE_FALT;
    }
    int status=bitmap_controller.second_stage_Init(
        (total_size_in_bytes/bytes_per_bit+63)/64
    );
    return status;
}
kpoolmemmgr_t::HCB_v2::~HCB_v2()
{
    bitmap_controller.~HCB_bitmap();
    int status=gKspacePgsMemMgr.pgs_remapped_free(vbase);
    if(status!=OS_SUCCESS){
        gkernelPanicManager.panic("kpoolmemmgr_t::HCB_v2::~HCB_v2 cancel memmap failed");
    }
    status=gPhyPgsMemMgr.pages_recycle(phybase,total_size_in_bytes/4096);
    if(status!=OS_SUCCESS){
        gkernelPanicManager.panic("kpoolmemmgr_t::HCB_v2::~HCB_v2 recycle phy pages failed");
    }
}
int kpoolmemmgr_t::HCB_v2::clear(void *ptr)
{
    if (!ptr) return OS_INVALID_PARAMETER;

    // ptr 是用户可见地址，meta 在其前面
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    if (meta->magic != MAGIC_ALLOCATED)
    {
        return OS_HEAP_OBJ_DESTROYED;
    }

    // 清零用户数据区域（不清 meta）
    // setmem 的原型假设为 setmem(void* addr, size_t size, int value)
    setmem(ptr, meta->data_size, 0);

    return OS_SUCCESS;
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
int kpoolmemmgr_t::HCB_v2::in_heap_alloc(
    void *&addr,
    uint32_t size,
    bool is_longtime,
    bool vaddraquire,
    uint8_t alignment)
{
    // -----------------------------
    // 基本参数与边界检查
    // -----------------------------
    if (alignment > 10)
        return OS_INVALID_PARAMETER;
    if (size == 0)
        return OS_INVALID_PARAMETER;
    if(size>=1ULL<<16)return OS_INVALID_PARAMETER;//64k,一次最大分配
    // 常量定义（在内核空间必须为常量或宏）
    static constexpr uint32_t SMALL_UNIT_BYTES=16;
    static constexpr uint8_t MID_UNIT_BYTES = 1U << 7;
    static constexpr uint32_t LARGE_UNIT_BYTES = 1U << 10;
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
    if (alignment <= 4)
        aquire_alignment = 4;
    else if (alignment <= 7)
        aquire_alignment = 7;
    else
        aquire_alignment = 10;

    // 最终取较大者
    final_alignment_aquire = (size_alignment > aquire_alignment) ?
                             size_alignment : aquire_alignment;

    // -----------------------------
    // 分配逻辑（不同对齐档）
    // -----------------------------
    int status = OS_SUCCESS;
    uintptr_t base_addr = (uintptr_t)(vaddraquire ? vbase : phybase);

    switch (final_alignment_aquire)
    {
    case 4: {  // 小块分配 (16字节对齐)
        uint32_t serial_bits_count = (size + SMALL_UNIT_BYTES - 1 + sizeof(data_meta)) >> 4;
        uint64_t base_bit_idx = 0;
        bitmap_controller.bitmap_rwlock.read_lock();
        status = bitmap_controller.continual_avaliable_bits_search(serial_bits_count, base_bit_idx);
        bitmap_controller.bitmap_rwlock.read_unlock();
        if (status != OS_SUCCESS)
            return OS_INHEAP_NOT_ENOUGH_MEMORY;
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
            return OS_INHEAP_NOT_ENOUGH_MEMORY;

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
            return OS_INHEAP_NOT_ENOUGH_MEMORY;

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
        return OS_INVALID_PARAMETER;
    }

    // -----------------------------
    // 设置元数据（data_meta）
    // -----------------------------
    data_meta *meta = (data_meta *)((uint8_t *)addr - sizeof(data_meta));
    meta->magic = MAGIC_ALLOCATED;
    meta->data_size = size;
    meta->data_type = DT_UNKNOWN;
    meta->flags.alignment = final_alignment_aquire;
    meta->flags.is_longtime_alloc = is_longtime;

    return OS_SUCCESS;
}

int kpoolmemmgr_t::HCB_v2::free(void *ptr)
{
    if(uint64_t(ptr)&15)return OS_INVALID_ADDRESS;  //本堆的内存至少16字节对齐
    uint32_t in_heap_offset;
    if((uint64_t)ptr<MIN_VIR_ADDR)
    {//物理地址
        if((uint64_t)ptr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)ptr
    )return OS_INVALID_ADDRESS;
    in_heap_offset=(uint64_t)ptr-(uint64_t)phybase;
    }else{
        if((uint64_t)ptr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)ptr)return OS_INVALID_ADDRESS;
    in_heap_offset=(uint64_t)ptr-(uint64_t)vbase;
    }
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    if(meta->magic != MAGIC_ALLOCATED)
    {
        return OS_HEAP_OBJ_DESTROYED;
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
    return bitmap_controller.bit_seg_set(
        base_bit_idx,
        total_bits_count,
        false
    );
}

int kpoolmemmgr_t::HCB_v2::in_heap_realloc(void *&ptr, uint32_t new_size, bool vaddraquire, uint8_t alignment)
{
    if(uint64_t(ptr)&15)return OS_INVALID_ADDRESS;  //本堆的内存至少16字节对齐
    uint32_t in_heap_offset;
    if((uint64_t)ptr<MIN_VIR_ADDR)
    {//物理地址
        if((uint64_t)ptr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)ptr
    )return OS_INVALID_ADDRESS;
    in_heap_offset=(uint64_t)ptr-(uint64_t)phybase;
    }else{
        if((uint64_t)ptr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)ptr)return OS_INVALID_ADDRESS;
    in_heap_offset=(uint64_t)ptr-(uint64_t)vbase;
    }
    data_meta *meta = (data_meta *)((uint8_t *)ptr - sizeof(data_meta));
    uint16_t old_size=meta->data_size;
    if(meta->magic != MAGIC_ALLOCATED)
    {
        return OS_HEAP_OBJ_DESTROYED;
    }uint32_t old_bits_count=(meta->data_size+15)/16;
    uint32_t base_bit_idx=in_heap_offset/16;
    uint32_t new_bits_count=(new_size+15)/16;
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
        return OS_SUCCESS;
    }else{
        bool try_append=bitmap_controller.target_bit_seg_is_avaliable(
            base_bit_idx+old_bits_count,
            new_bits_count-old_bits_count
        );
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
            return OS_SUCCESS;
        }else{
            void*new_ptr;
            int status=in_heap_alloc(
                new_ptr,new_size,meta->flags.is_longtime_alloc,vaddraquire,alignment
            );
            if(status==OS_SUCCESS){
                ksystemramcpy(ptr,new_ptr,old_size);
                status=free(ptr);
                ptr=new_ptr;
                bitmap_controller.used_bit_count_lock.lock();
                bitmap_controller.bitmap_used_bit+=(new_bits_count-old_bits_count);
                bitmap_controller.used_bit_count_lock.unlock();
                return OS_SUCCESS;
            }else{
                return OS_INHEAP_NOT_ENOUGH_MEMORY;
            }
        }
    }
    return  OS_UNREACHABLE_CODE;
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
        if((uint64_t)addr<MIN_VIR_ADDR)
    {//物理地址
        if((uint64_t)addr<phybase+sizeof(data_meta)||
    phybase+total_size_in_bytes<=(uint64_t)addr
    )return false;

    }else{
        if((uint64_t)addr<vbase+sizeof(data_meta)||
    vbase+total_size_in_bytes<=(uint64_t)addr)return false;

    }
    return true;
}
uint32_t kpoolmemmgr_t::HCB_v2::get_belonged_cpu_apicid()
{
    return this->belonged_to_cpu_apicid;
}
