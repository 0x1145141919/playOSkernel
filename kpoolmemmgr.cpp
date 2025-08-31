#include "kpoolmemmgr.h"
#include "Memory.h"
#include "utils.h"
#include "os_error_definitions.h"
#include "VideoDriver.h"
#ifdef TEST_MODE
#include "stdlib.h"
#endif  

// 定义全局变量gKpoolmemmgr
kpoolmemmgr_t gKpoolmemmgr;

constexpr uint16_t FirstStaticHeapMaxObjCount = 4096;
HeapObjectMeta objMetaTable_for_FirstStaticHeap[FirstStaticHeapMaxObjCount] = {0};
#ifdef KERNEL_MODE
extern "C" {
    extern char __heap_start;
    extern char __heap_end;
}
#endif
// 简单的对数函数实现（如果标准库不可用）
static inline uint8_t log2(size_t n) {
    uint8_t result = 0;
    while (n >>= 1) {
        result++;
    }
    return result;
}
/**
 * 使用二分查找在元信息表中查找地址对应的对象索引
 * 如果地址在某个对象内，返回该对象的索引
 * 如果地址在堆空洞中，返回-1
 * 如果地址不在堆范围内，返回INDEX_NOT_EXIST (-100)
 */
int kpoolmemmgr_t::addr_to_HCB_MetaInfotb_Index(HCB_chainlist_node HNode, uint8_t *addr)
{
    HeapMetaInfoArray* metaInfo = &HNode.heap.metaInfo;
    uint32_t objCount = metaInfo->header.objMetaCount;
    HeapObjectMeta* metaTable = metaInfo->objMetaTable;
    
    // 检查地址是否在堆范围内
    uint8_t* heapStart = (uint8_t*)HNode.heap.heapStart;
    uint8_t* heapEnd = heapStart + HNode.heap.heapSize;
    
    if (addr < heapStart || addr >= heapEnd) {
        return OS_OUT_OF_RANGE; // 地址不在堆范围内
    }
    
    // 如果元数据表为空，地址在堆空洞中
    if (objCount == 0) {
        return OS_NOT_EXIST;
    }
    
    // 使用二分查找找到地址所在的对象
    int32_t left = 0;
    int32_t right = objCount - 1;
    
    while (left <= right) {
        int32_t mid = left + (right - left) / 2;
        uint8_t* objStart = (uint8_t*)metaTable[mid].base;
        uint8_t* objEnd = objStart + metaTable[mid].size;
        
        if (addr >= objStart && addr < objEnd) {
            // 地址在对象范围内
            return mid;
        } else if (addr < objStart) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    // 如果没有找到对象，地址在堆空洞中
    return OS_NOT_EXIST;
}

/*内核池管理多个堆内存块*/
/**
 * 此函数搜索堆内存块中某段内存是否可申请,若是则返回空指针，
 * 若否，则返回下一个可能的空闲内存块的起始地址
 * 
 */
uint8_t* kpoolmemmgr_t::is_space_available(HCB_chainlist_node Heap, uint8_t* addr, uint64_t size)
{
    HeapMetaInfoArray* metaInfo = &Heap.heap.metaInfo;
    uint32_t objCount = metaInfo->header.objMetaCount;
    HeapObjectMeta* metaTable = metaInfo->objMetaTable;
    uint8_t* heapStart = (uint8_t*)Heap.heap.heapStart;
    uint8_t* heapEnd = heapStart + Heap.heap.heapSize;
    
    // 检查地址是否在堆范围内
    if (addr < heapStart || addr >= heapEnd) {
        return heapEnd; // 超出堆范围，返回堆结束地址
    }
    
    // 检查请求的大小是否超过堆剩余空间
    if (size > Heap.heap.freeSize) {
        return heapEnd; // 空间不足，返回堆结束地址
    }
    
    // 检查地址+大小是否超出堆范围
    if (addr + size > heapEnd) {
        return heapEnd; // 超出堆范围，返回堆结束地址
    }
    
    // 使用二分查找定位地址所在的元数据项
    int index = addr_to_HCB_MetaInfotb_Index(Heap, addr);
    
    // 处理地址不在堆范围内的情况
    if (index == OS_OUT_OF_RANGE) {
        return heapEnd;
    }
    
    // 处理地址在堆空洞中的情况
    if (index == OS_NOT_EXIST) {
        // 这种情况理论上不应该发生，因为元信息表应该覆盖整个堆
        // 但如果发生了，我们返回堆结束地址
        return heapEnd;
    }
    
    // 处理地址在对象内的情况
    if (index >= 0 && index < objCount) {
        HeapObjectMeta* meta = &metaTable[index];
        
        // 如果对象是空闲的，检查是否有足够空间
        if (meta->type == OBJ_TYPE_FREE) {
            uint8_t* objEnd = (uint8_t*)meta->base + meta->size;
            
            // 检查从给定地址开始是否有足够空间
            if (addr + size <= objEnd) {
                return nullptr; // 空间可用
            }
        }
        
        // 对象被占用或空间不足，返回对象结束地址
        return (uint8_t*)meta->base + meta->size;
    }
    
    // 默认情况下返回堆结束地址
    return heapEnd;
}
/**
 * 正式的kalloc函数，分配堆内存的时候要考虑
 * 1.ableto_Expand位
 *  如果开启了，就要考虑遍历多个堆块寻找空闲内存，
 * 不过还是先到先得，
 * 内存不够就用其它接口创建新的堆以及对应的HCB节点
 * 或者在可以额外增高堆内存的HCB节点上使用相关接口向上增加内存
 * 2.有没有开启内核映射到高位以及映射到高位需求
 * 如果是映射到高位，就要考虑返回地址要加上堆虚拟基址，
 * 毕竟堆块的对齐，分配都是2mb为单位的
 * 3.分配时的地址对齐需求
 * kalloc给其它对象分配内存时候，对齐粒度至少8字节，
 * 如果特定指明了才会提升到2^n字节对齐
 */
/**
 * 目前的缺陷：
 * 这个函数如果要处理对齐问题，可能会产生内存碎屑，
 * 但是这个函数对于内存碎屑的处理是不处理，
 * 会产生元信息表没有映射到的内存空洞
 */
void *kpoolmemmgr_t::kalloc(uint64_t size_in_bytes, bool vaddraquire, uint8_t alignment)
{   
    if(size_in_bytes == 0)return nullptr;
    if (kpoolmemmgr_flags.ableto_Expand == 0) {
        // 直接根据hcb.status里面的各个位进行判断
        bool is_free = (first_static_heap.heap.heapSize == first_static_heap.heap.freeSize);
        
        bool is_partial_used = (first_static_heap.heap.status.block_exist && 
                                !first_static_heap.heap.status.block_tb_full && 
                                !first_static_heap.heap.status.block_full && 
                                !first_static_heap.heap.status.block_reserved && 
                                !first_static_heap.heap.status.block_merged);
        if(!is_free && !is_partial_used)
        {
            return nullptr;
        }
        HeapObjectMeta* infotb = first_static_heap.heap.metaInfo.objMetaTable;
        uint64_t MaxObjCount = first_static_heap.heap.metaInfo.header.objMetaMaxCount;
        uint64_t ObjCount = first_static_heap.heap.metaInfo.header.objMetaCount;
        uint8_t* heap_phys_base = (uint8_t*)first_static_heap.heap.heapStart;
        uint8_t* heap_virt_base = (uint8_t*)first_static_heap.heap.heapVStart;
        uint8_t* heap_phys_end = heap_phys_base + first_static_heap.heap.heapSize;
        
        // 计算对齐掩码
        uint64_t align_value = (1ULL << alignment);
        uint64_t align_mask = align_value - 1;
        
        // 遍历元信息表寻找合适的空闲块
        for (uint32_t i = 0; i < ObjCount; i++) {
            if (infotb[i].type == OBJ_TYPE_FREE) {
                uint8_t* block_phys_start = (uint8_t*)infotb[i].base;
                uint8_t* block_virt_start = (uint8_t*)infotb[i].vbase;
                uint64_t block_size = infotb[i].size;
                // 计算对齐后的物理起始地址
                uint8_t* aligned_phys_start = (uint8_t*)(((uint64_t)block_phys_start + align_mask) & ~align_mask);
                uint64_t alignment_padding = aligned_phys_start - block_phys_start;
                // 计算对应的虚拟地址
                uint8_t* aligned_virt_start = block_virt_start + (aligned_phys_start - block_phys_start);    
                // 检查是否有足够空间（包括对齐填充）
                 if (alignment_padding + size_in_bytes <= block_size) {
                    // 计算剩余空间
                    uint64_t remaining_size = block_size - alignment_padding - size_in_bytes;
                    
                    // 检查是否有空间存储新的元信息
                    uint32_t needed_entries = 0;
                    if (alignment_padding > 0) needed_entries++;
                    if (remaining_size > 0) needed_entries++;
                    
                    if (ObjCount + needed_entries <= MaxObjCount) {
                        // 处理对齐填充部分
                        if (alignment_padding > 0) {
                            // 创建对齐填充空闲块
                            HeapObjectMeta padding_block;
                            padding_block.base = (phyaddr_t)block_phys_start;
                            padding_block.vbase = (vaddr_t)block_virt_start;
                            padding_block.size = alignment_padding;
                            padding_block.type = OBJ_TYPE_FREE;
                            
                            // 更新当前块为对齐填充块
                            infotb[i] = padding_block;
                            
                            // 如果有剩余空间，需要插入分配块和剩余空间块
                            if (remaining_size > 0) {
                                // 创建分配块
                                HeapObjectMeta alloc_block;
                                alloc_block.base = (phyaddr_t)aligned_phys_start;
                                alloc_block.vbase = (vaddr_t)aligned_virt_start;
                                alloc_block.size = size_in_bytes;
                                alloc_block.type = OBJ_TYPE_NORMAL;
                                
                                // 创建剩余空间空闲块
                                HeapObjectMeta remaining_block;
                                remaining_block.base = (phyaddr_t)(aligned_phys_start + size_in_bytes);
                                remaining_block.vbase = (vaddr_t)(aligned_virt_start + size_in_bytes);
                                remaining_block.size = remaining_size;
                                remaining_block.type = OBJ_TYPE_FREE;
                                
                                // 插入分配块和剩余空间块
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 1,
                                    &alloc_block,
                                    infotb,
                                    sizeof(HeapObjectMeta),
                                    1
                                );
                                
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 2,
                                    &remaining_block,
                                    infotb,
                                    sizeof(HeapObjectMeta),
                                    1
                                );
                                
                                // 更新对象计数
                                ObjCount += 2;
                            } else {
                                //没法增加新的项的情况下zhi neng
                                // 只有对齐填充，没有剩余空间
                                // 创建分配块
                                HeapObjectMeta alloc_block;
                                alloc_block.base = (phyaddr_t)aligned_phys_start;
                                alloc_block.vbase = (vaddr_t)aligned_virt_start;
                                alloc_block.size = size_in_bytes;
                                alloc_block.type = OBJ_TYPE_NORMAL;
                                
                                // 插入分配块
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 1,
                                    &alloc_block,
                                    infotb,
                                    sizeof(HeapObjectMeta),
                                    1
                                );
                                
                                // 更新对象计数
                                ObjCount += 1;
                            }
                        } else {
                            // 没有对齐填充，但有剩余空间
                            // 更新当前块为分配块
                            infotb[i].base = (phyaddr_t)aligned_phys_start;
                            infotb[i].vbase = (vaddr_t)aligned_virt_start;
                            infotb[i].size = size_in_bytes;
                            infotb[i].type = OBJ_TYPE_NORMAL;
                            
                            // 创建剩余空间空闲块
                            HeapObjectMeta remaining_block;
                            remaining_block.base = (phyaddr_t)(aligned_phys_start + size_in_bytes);
                            remaining_block.vbase = (vaddr_t)(aligned_virt_start + size_in_bytes);
                            remaining_block.size = remaining_size;
                            remaining_block.type = OBJ_TYPE_FREE;
                            
                            // 插入剩余空间块
                            linearTBSerialInsert(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i + 1,
                                &remaining_block,
                                infotb,
                                sizeof(HeapObjectMeta),
                                1
                            );
                            
                            // 更新对象计数
                            ObjCount += 1;
                        }
                        
                        // 更新堆的剩余空间
                        first_static_heap.heap.freeSize -= size_in_bytes;
                    } else {
                       return nullptr;  
                    }  
                    if(first_static_heap.heap.freeSize) {
                        // 设置为部分使用状态
                        first_static_heap.heap.status.block_exist = 1;
                        first_static_heap.heap.status.block_full = 0;
                    } else {
                        // 设置为完全使用状态
                        first_static_heap.heap.status.block_exist = 1;
                        first_static_heap.heap.status.block_full = 1;
                    }
                        
                        // 返回分配的内存地址
                        if (vaddraquire) {
                            return aligned_virt_start;
                        } else {
                            return aligned_phys_start;
                        }
                }
            }
        }
        
        // 没有找到合适的空闲块
        return nullptr;
    } else {
        // ableto_Expand位开启的情况，暂不实现
        return nullptr;
    }
}
HCB_chainlist_node *kpoolmemmgr_t::getFirst_static_heap()
{
    return &first_static_heap;
}
kpoolmemmgr_flags_t kpoolmemmgr_t::getkpoolmemmgr_flags()
{
    return kpoolmemmgr_flags;
}

// 实现mgr_vaddr_enabled方法
int kpoolmemmgr_t::mgr_vaddr_enabled()
{
    return kpoolmemmgr_flags.heap_vaddr_enabled;
}

void kpoolmemmgr_t::kfree(void*ptr)
{
    if (kpoolmemmgr_flags.ableto_Expand == 0) {
        HeapObjectMeta* infotb = first_static_heap.heap.metaInfo.objMetaTable;
        uint64_t ObjCount = first_static_heap.heap.metaInfo.header.objMetaCount;
        uint8_t* heap_phys_base = (uint8_t*)first_static_heap.heap.heapStart;
        uint8_t* heap_virt_base = (uint8_t*)first_static_heap.heap.heapVStart;
        
        // 判断ptr是物理地址还是虚拟地址
        bool is_virtual_addr = false;
        uint8_t* target_addr = (uint8_t*)ptr;
        
        // 检查高16位是否全1（虚拟地址特征）
        if ((reinterpret_cast<uint64_t>(ptr) >> 48) == 0xFFFF) {
            is_virtual_addr = true;
        }
        
        // 遍历元信息表查找对应的对象
        for (uint32_t i = 0; i < ObjCount; i++) {
            uint8_t* obj_phys_addr = (uint8_t*)infotb[i].base;
            uint8_t* obj_virt_addr = (uint8_t*)infotb[i].vbase;
            
            // 检查地址是否匹配
            bool addr_match = false;
            if (is_virtual_addr) {
                addr_match = (target_addr >= obj_virt_addr && 
                             target_addr < obj_virt_addr + infotb[i].size);
            } else {
                addr_match = (target_addr >= obj_phys_addr && 
                             target_addr < obj_phys_addr + infotb[i].size);
            }
            
            if (addr_match && infotb[i].type != OBJ_TYPE_FREE) {
                // 找到要释放的对象
                uint64_t freed_size = infotb[i].size;
                
                // 标记为空闲块
                infotb[i].type = OBJ_TYPE_FREE;
                
                // 更新堆的空闲空间
                first_static_heap.heap.freeSize += freed_size;
                
                // 尝试与相邻的空闲块合并
                bool merged;
                do {
                    merged = false;
                    
                    // 尝试与前一个块合并
                    if (i > 0 && infotb[i-1].type == OBJ_TYPE_FREE) {
                        uint8_t* prev_end = (uint8_t*)infotb[i-1].base + infotb[i-1].size;
                        
                        // 检查物理地址是否连续
                        if (prev_end == (uint8_t*)infotb[i].base) {
                            // 合并块
                            infotb[i-1].size += infotb[i].size;
                            
                            // 删除当前块
                            linearTBSerialDelete(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i,
                                i,
                                infotb,
                                sizeof(HeapObjectMeta)
                            );
                            
                            // 更新对象计数和索引
                            ObjCount--;
                            i--;
                            merged = true;
                            continue;
                        }
                    }
                    
                    // 尝试与后一个块合并
                    if (i < ObjCount - 1 && infotb[i+1].type == OBJ_TYPE_FREE) {
                        uint8_t* curr_end = (uint8_t*)infotb[i].base + infotb[i].size;
                        
                        // 检查物理地址是否连续
                        if (curr_end == (uint8_t*)infotb[i+1].base) {
                            // 合并块
                            infotb[i].size += infotb[i+1].size;
                            
                            // 删除后一个块
                            linearTBSerialDelete(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i+1,
                                i+1,
                                infotb,
                                sizeof(HeapObjectMeta)
                            );
                            
                            // 更新对象计数
                            ObjCount--;
                            merged = true;
                        }
                    }
                } while (merged);
                
                return; // 释放完成
            }
        }
        
        // 如果没有找到匹配的对象，可能是错误的释放请求
        // 在内核环境中，可能需要记录错误或触发断言
    } else {
        // ableto_Expand位开启的情况，暂不实现
    }
}

kpoolmemmgr_t::kpoolmemmgr_t()
{
#ifdef TEST_MODE
    first_static_heap.heap.heapStart = (phyaddr_t)malloc(1ULL<<22);
    first_static_heap.heap.heapVStart = first_static_heap.heap.heapStart ;
    first_static_heap.heap.heapSize=first_static_heap.heap.freeSize=1ULL<<22;
#endif  
#ifdef KERNEL_MODE
    // 获取内核堆的起始地址和大小
    first_static_heap.heap.heapStart = (phyaddr_t)&__heap_start;
    first_static_heap.heap.heapVStart = (vaddr_t)&__heap_start;
    first_static_heap.heap.heapSize = (uint64_t)(&__heap_end - &__heap_start);
    first_static_heap.heap.freeSize = (uint64_t)(&__heap_end - &__heap_start);
#endif    
    // 初始化status为HEAP_BLOCK_FREE状态
    first_static_heap.heap.status.block_exist = 0;
    first_static_heap.heap.status.block_tb_full = 0;
    first_static_heap.heap.status.block_full = 0;
    first_static_heap.heap.status.block_reserved = 0;
    first_static_heap.heap.status.block_merged = 0;
    
    // 初始化元信息数组
    first_static_heap.heap.metaInfo.header.magic = 0x48504D41; // "HPM A"
    first_static_heap.heap.metaInfo.header.version = 1;
    first_static_heap.heap.metaInfo.header.objMetaCount = 1;  // 初始时没有对象
    first_static_heap.heap.metaInfo.header.objMetaMaxCount = FirstStaticHeapMaxObjCount;
    first_static_heap.heap.metaInfo.objMetaTable = objMetaTable_for_FirstStaticHeap;
    objMetaTable_for_FirstStaticHeap[0].base = first_static_heap.heap.heapStart;
    objMetaTable_for_FirstStaticHeap[0].size = first_static_heap.heap.heapSize;
    objMetaTable_for_FirstStaticHeap[0].type = OBJ_TYPE_FREE;
    // 初始化链表指针
    first_static_heap.prev = nullptr;
    first_static_heap.next = nullptr;
    last_heap_node=&first_static_heap;
    // 初始化flags结构
    // 根据文档说明，设置flags里面的位域值
    kpoolmemmgr_flags.ableto_Expand = 0;  // 使用0而不是false，因为这是位域

    
    HCB_count = 1;
}

// 实现新的成员函数
void kpoolmemmgr_t::print_hcb_status(HCB_chainlist_node* node) {
    const char* status_str = "UNKNOWN";
    
    // 直接根据hcb.status里面的各个位进行判断
    if (node->heap.heapSize == node->heap.freeSize) {
        status_str = "FREE";  // HEAP_BLOCK_FREE = 0
    } 
    else if (node->heap.status.block_exist && 
             
             !node->heap.status.block_full &&
             node->heap.freeSize!=0
            ) {
        status_str = "USED";  // HEAP_BLOCK_PARTIAL_USED = 1
    }
    else if (node->heap.status.block_exist && 

             node->heap.status.block_reserved) {
        status_str = "RESERVED";  // HEAP_BLOCK_RESERVED = 2
    }
    else if (node->heap.status.block_exist && 
             node->heap.status.block_merged) {
        status_str = "MERGED";  // HEAP_BLOCK_MERGED = 3
    }
    else if (node->heap.status.block_exist && 
             node->heap.status.block_full
            ) {
        status_str = "FULL";  // HEAP_BLOCK_FULL = 4
    }else if (node->heap.status.block_exist && 
             node->heap.status.block_tb_full)
    {
        status_str = "TB_FULL";
    }
    
    
    kputsSecure("Heap Control Block Status:");
    kputsSecure("\n  Physical Start: 0x");
    kpnumSecure(&node->heap.heapStart, UNHEX, sizeof(phyaddr_t));
    
    kputsSecure("\n  Virtual Start: 0x");
    kpnumSecure(&node->heap.heapVStart, UNHEX, sizeof(vaddr_t));
    
    kputsSecure("\n  Total Size: ");
    kpnumSecure(&node->heap.heapSize, UNDEC, 0);
    kputsSecure(" bytes");
    
    kputsSecure("\n  Free Size: ");
    kpnumSecure(&node->heap.freeSize, UNDEC, 0);
    kputsSecure(" bytes");
    
    kputsSecure("\n  Status: ");
    kputsSecure(const_cast<char*>(status_str));
    
    kputsSecure("\n  Meta Entries: ");
    uint64_t count = node->heap.metaInfo.header.objMetaCount;
    kpnumSecure(&count, UNDEC, 0);
    kputsSecure("/");
    kpnumSecure(&node->heap.metaInfo.header.objMetaMaxCount, UNDEC, 0);
    kputsSecure("\n");
}

const char* obj_type_to_str(KernelObjType type) {
    switch(type) {
        case OBJ_TYPE_TASK: return "TASK";
        case OBJ_TYPE_THREAD: return "THREAD";
        case OBJ_TYPE_MUTEX: return "MUTEX";
        case OBJ_TYPE_SEMAPHORE: return "SEM";
        case OBJ_TYPE_EVENT: return "EVENT";
        case OBJ_TYPE_QUEUE: return "QUEUE";
        case OBJ_TYPE_TIMER: return "TIMER";
        case OBJ_TYPE_DEVICE: return "DEVICE";
        case OBJ_TYPE_FILE: return "FILE";
        case OBJ_TYPE_SHM: return "SHM";
        case OBJ_TYPE_NORMAL: return "NORMAL";
        case OBJ_TYPE_FREE: return "FREE";
        case OBJ_TYPE_FIXED: return "FIXED";
        default: return "UNKNOWN";
    }
}

void kpoolmemmgr_t::print_meta_table(HCB_chainlist_node* node) {
    HeapMetaInfoArray* meta = &node->heap.metaInfo;
    uint64_t count = meta->header.objMetaCount;
    
    kputsSecure("\nHeap Meta Table (");
    kpnumSecure(&count, UNDEC, 0);
    kputsSecure(" entries):\n");
    
    kputsSecure("IDX | PHYS ADDR    | SIZE       | TYPE    | VADDR\n");
    kputsSecure("----+-------------+------------+---------+------------\n");
    
    for(uint32_t i = 0; i < count; i++) {
        HeapObjectMeta* entry = &meta->objMetaTable[i];
        
        // 打印索引
        kpnumSecure(&i, UNDEC, 3);
        kputsSecure(" | ");
        
        // 物理地址
        kpnumSecure(&entry->base, UNHEX, sizeof(phyaddr_t));
        kputsSecure(" | ");
        
        // 大小
        kpnumSecure(&entry->size, UNDEC, 8);
        kputsSecure(" | ");
        
        // 类型
        const char* type = obj_type_to_str(entry->type);
        kputsSecure(const_cast<char*> (type));
        for(int s = 0; s < (7 - strlen(type)); s++) kputsSecure(" ");
        kputsSecure(" | ");
        
        // 虚拟地址
        kpnumSecure(&entry->vbase, UNHEX, sizeof(vaddr_t));
        kputsSecure("\n");
    }
}

void kpoolmemmgr_t::print_all_hcb_status() {
    HCB_chainlist_node* current = &first_static_heap;
    uint64_t index = 0;
    
    while(current) {
        kputsSecure("\n=== HCB Node #");
        kpnumSecure(&index, UNDEC, 0);
        kputsSecure(" ===\n");
        
        print_hcb_status(current);
        print_meta_table(current);
        
        current = current->next;
        index++;
    }
    
    kputsSecure("\nTotal HCBs: ");
    kpnumSecure(&index, UNDEC, 0);
    kputsSecure("\n");
}

kpoolmemmgr_t::~kpoolmemmgr_t()
{
}
// 重载全局 new/delete 操作符
void* operator new(size_t size) {
    return gKpoolmemmgr.kalloc(size, gKpoolmemmgr.getkpoolmemmgr_flags().heap_vaddr_enabled, 3);
}

void* operator new(size_t size, bool vaddraquire, uint8_t alignment) {
    return gKpoolmemmgr.kalloc(size, vaddraquire, alignment);
}

void* operator new[](size_t size) {
    return gKpoolmemmgr.kalloc(size, gKpoolmemmgr.getkpoolmemmgr_flags().heap_vaddr_enabled, 3);
}

void* operator new[](size_t size, bool vaddraquire, uint8_t alignment) {
    return gKpoolmemmgr.kalloc(size, vaddraquire, alignment);
}

void operator delete(void* ptr) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete[](void* ptr) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

// 放置 new 操作符
void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

void* operator new[](size_t, void* ptr) noexcept {
    return ptr;
}