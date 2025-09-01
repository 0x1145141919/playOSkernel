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
HeapObjectMetav2 objMetaTable_for_FirstStaticHeap[FirstStaticHeapMaxObjCount] = {0};
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

// 辅助函数：根据偏移量计算物理地址
static inline uint8_t* offset_to_phys_addr(HCB_chainlist_node* node, uint32_t offset) {
    return (uint8_t*)node->heap.heapStart + offset;
}

// 辅助函数：根据偏移量计算虚拟地址
static inline uint8_t* offset_to_virt_addr(HCB_chainlist_node* node, uint32_t offset) {
    return (uint8_t*)node->heap.heapVStart + offset;
}

// 辅助函数：根据物理地址计算偏移量
static inline uint32_t phys_addr_to_offset(HCB_chainlist_node* node, uint8_t* addr) {
    return (uint32_t)(addr - (uint8_t*)node->heap.heapStart);
}

// 辅助函数：根据虚拟地址计算偏移量
static inline uint32_t virt_addr_to_offset(HCB_chainlist_node* node, uint8_t* addr) {
    return (uint32_t)(addr - (uint8_t*)node->heap.heapVStart);
}
/**
 * 使用二分查找在元信息表中查找地址对应的对象索引
 * 如果地址在某个对象内，返回该对象的索引
 * 如果地址在堆空洞中，返回-1
 * 如果地址不在堆范围内，返回INDEX_NOT_EXIST (-100)
 */
int kpoolmemmgr_t::addr_to_HCB_MetaInfotb_Index(HCB_chainlist_node HNode, uint8_t* addr) {
    HeapMetaInfoArray* metaInfo = &HNode.heap.metaInfo;
    uint32_t objCount = metaInfo->header.objMetaCount;
    HeapObjectMetav2* metaTable = metaInfo->objMetaTable;
    
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
    
    // 计算地址在堆中的偏移量
    uint32_t offset = phys_addr_to_offset(&HNode, addr);
    
    // 使用二分查找找到地址所在的对象
    int32_t left = 0;
    int32_t right = objCount - 1;
    
    while (left <= right) {
        int32_t mid = left + (right - left) / 2;
        uint32_t objStart = metaTable[mid].offset_in_heap;
        uint32_t objEnd = objStart + metaTable[mid].size;
        
        if (offset >= objStart && offset < objEnd) {
            // 地址在对象范围内
            return mid;
        } else if (offset < objStart) {
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
    HeapObjectMetav2* metaTable = metaInfo->objMetaTable;
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
        HeapObjectMetav2* meta = &metaTable[index];
        
        // 计算对象的起始地址
        uint8_t* objStart = heapStart + meta->offset_in_heap;
        uint8_t* objEnd = objStart + meta->size;
        
        // 如果对象是空闲的，检查是否有足够空间
        if (meta->type == OBJ_TYPE_FREE) {
            // 检查从给定地址开始是否有足够空间
            if (addr + size <= objEnd) {
                return nullptr; // 空间可用
            }
        }
        
        // 对象被占用或空间不足，返回对象结束地址
        return objEnd;
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
 * 只是返回内存地址，
 * 没有提前清理地址以及相关内存，
 * 可能造成未定义后果
 * 不过可以用另外的成员函数来实现
 */
void *kpoolmemmgr_t::kalloc(uint64_t size_in_bytes, bool vaddraquire, uint8_t alignment)
{   
    if(size_in_bytes == 0) return nullptr;
    
    if (kpoolmemmgr_flags.ableto_Expand == 0) {
        // 直接根据hcb.status里面的各个位进行判断
        bool is_free = (first_static_heap.heap.heapSize == first_static_heap.heap.freeSize);
        
        bool is_partial_used = (first_static_heap.heap.status.block_exist && 
                                !first_static_heap.heap.status.block_tb_full && 
                                !first_static_heap.heap.status.block_full && 
                                !first_static_heap.heap.status.block_reserved && 
                                !first_static_heap.heap.status.block_merged);
        
        if(!is_free && !is_partial_used) {
            return nullptr;
        }
        
        HeapObjectMetav2* infotb = first_static_heap.heap.metaInfo.objMetaTable;
        uint64_t MaxObjCount = first_static_heap.heap.metaInfo.header.objMetaMaxCount;
        uint64_t ObjCount = first_static_heap.heap.metaInfo.header.objMetaCount;
        uint8_t* heap_phys_base = (uint8_t*)first_static_heap.heap.heapStart;
        uint8_t* heap_virt_base = (uint8_t*)first_static_heap.heap.heapVStart;
        
        // 计算对齐掩码
        uint64_t align_value = (1ULL << alignment);
        uint64_t align_mask = align_value - 1;
        
        // 遍历元信息表寻找合适的空闲块
        for (uint32_t i = 0; i < ObjCount; i++) {
            if (infotb[i].type == OBJ_TYPE_FREE) {
                // 计算块的物理和虚拟起始地址
                uint8_t* block_phys_start = heap_phys_base + infotb[i].offset_in_heap;
                uint8_t* block_virt_start = heap_virt_base + infotb[i].offset_in_heap;
                uint64_t block_size = infotb[i].size;
                
                // 计算对齐后的物理起始地址
                uint8_t* aligned_phys_start = (uint8_t*)(((uint64_t)block_phys_start + align_mask) & ~align_mask);
                uint64_t alignment_padding = aligned_phys_start - block_phys_start;
                
                // 计算对应的虚拟地址
                uint8_t* aligned_virt_start = block_virt_start + alignment_padding;
                
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
                            HeapObjectMetav2 padding_block;
                            padding_block.offset_in_heap = infotb[i].offset_in_heap;
                            padding_block.size = alignment_padding;
                            padding_block.type = OBJ_TYPE_FREE;
                            
                            // 更新当前块为对齐填充块
                            infotb[i] = padding_block;
                            
                            // 如果有剩余空间，需要插入分配块和剩余空间块
                            if (remaining_size > 0) {
                                // 创建分配块
                                HeapObjectMetav2 alloc_block;
                                alloc_block.offset_in_heap = infotb[i].offset_in_heap + alignment_padding;
                                alloc_block.size = size_in_bytes;
                                alloc_block.type = OBJ_TYPE_NORMAL;
                                
                                // 创建剩余空间空闲块
                                HeapObjectMetav2 remaining_block;
                                remaining_block.offset_in_heap = alloc_block.offset_in_heap + size_in_bytes;
                                remaining_block.size = remaining_size;
                                remaining_block.type = OBJ_TYPE_FREE;
                                
                                // 插入分配块和剩余空间块
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 1,
                                    &alloc_block,
                                    infotb,
                                    sizeof(HeapObjectMetav2),
                                    1
                                );
                                
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 2,
                                    &remaining_block,
                                    infotb,
                                    sizeof(HeapObjectMetav2),
                                    1
                                );
                                
                                // 更新对象计数
                                ObjCount += 2;
                            } else {
                                // 只有对齐填充，没有剩余空间
                                // 创建分配块
                                HeapObjectMetav2 alloc_block;
                                alloc_block.offset_in_heap = infotb[i].offset_in_heap + alignment_padding;
                                alloc_block.size = size_in_bytes;
                                alloc_block.type = OBJ_TYPE_NORMAL;
                                
                                // 插入分配块
                                linearTBSerialInsert(
                                    &first_static_heap.heap.metaInfo.header.objMetaCount,
                                    i + 1,
                                    &alloc_block,
                                    infotb,
                                    sizeof(HeapObjectMetav2),
                                    1
                                );
                                
                                // 更新对象计数
                                ObjCount += 1;
                            }
                        } else {
                            // 没有对齐填充，但有剩余空间
                            // 更新当前块为分配块
                            infotb[i].offset_in_heap += 0; // 偏移量不变
                            infotb[i].size = size_in_bytes;
                            infotb[i].type = OBJ_TYPE_NORMAL;
                            
                            // 创建剩余空间空闲块
                            HeapObjectMetav2 remaining_block;
                            remaining_block.offset_in_heap = infotb[i].offset_in_heap + size_in_bytes;
                            remaining_block.size = remaining_size;
                            remaining_block.type = OBJ_TYPE_FREE;
                            
                            // 插入剩余空间块
                            linearTBSerialInsert(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i + 1,
                                &remaining_block,
                                infotb,
                                sizeof(HeapObjectMetav2),
                                1
                            );
                            
                            // 更新对象计数
                            ObjCount += 1;
                        }
                        
                        // 更新堆的剩余空间
                        first_static_heap.heap.freeSize -= size_in_bytes;
                        
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
                    } else {
                       return nullptr;  
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
void kpoolmemmgr_t::Init()
{
#ifdef KERNEL_MODE
    // 获取内核堆的起始地址和大小
    first_static_heap.heap.heapStart = (phyaddr_t)&__heap_start;
    first_static_heap.heap.heapVStart = (vaddr_t)&__heap_start;
    first_static_heap.heap.heapSize = (uint64_t)(&__heap_end - &__heap_start);
    first_static_heap.heap.freeSize = (uint64_t)(&__heap_end - &__heap_start);
#endif    
    first_static_heap.heap.status.block_exist = 0;
    first_static_heap.heap.status.block_tb_full = 0;
    first_static_heap.heap.status.block_full = 0;
    first_static_heap.heap.status.block_reserved = 0;
    first_static_heap.heap.status.block_merged = 0;
    
    // 初始化元信息数组
    first_static_heap.heap.metaInfo.header.magic = 0x48504D41; // "HPM A"
    first_static_heap.heap.metaInfo.header.version = 2; // 版本改为2，表示使用v2元信息
    first_static_heap.heap.metaInfo.header.objMetaCount = 1;  // 初始时有一个空闲对象
    first_static_heap.heap.metaInfo.header.objMetaMaxCount = FirstStaticHeapMaxObjCount;
    first_static_heap.heap.metaInfo.objMetaTable = objMetaTable_for_FirstStaticHeap;
    
    // 初始化第一个元信息项（整个堆作为空闲块）
    objMetaTable_for_FirstStaticHeap[0].offset_in_heap = 0;
    objMetaTable_for_FirstStaticHeap[0].size = first_static_heap.heap.heapSize;
    objMetaTable_for_FirstStaticHeap[0].type = OBJ_TYPE_FREE;
    
    // 初始化链表指针
    first_static_heap.prev = nullptr;
    first_static_heap.next = nullptr;
    last_heap_node = &first_static_heap;
    
    // 初始化flags结构
    kpoolmemmgr_flags.ableto_Expand = 0;
    kpoolmemmgr_flags.heap_vaddr_enabled = 1; // 默认启用虚拟地址
    kpoolmemmgr_flags.alignment = 3; // 默认8字节对齐 (2^3=8)
    
    HCB_count = 1;
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

void kpoolmemmgr_t::kfree(void* ptr)
{
    if (kpoolmemmgr_flags.ableto_Expand == 0) {
        HeapObjectMetav2* infotb = first_static_heap.heap.metaInfo.objMetaTable;
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
        
        // 计算目标地址在堆中的偏移量
        uint32_t target_offset;
        if (is_virtual_addr) {
            target_offset = virt_addr_to_offset(&first_static_heap, target_addr);
        } else {
            target_offset = phys_addr_to_offset(&first_static_heap, target_addr);
        }
        
        // 遍历元信息表查找对应的对象
        for (uint32_t i = 0; i < ObjCount; i++) {
            uint32_t obj_offset = infotb[i].offset_in_heap;
            uint32_t obj_end = obj_offset + infotb[i].size;
            
            // 检查地址是否匹配
            bool addr_match = (target_offset >= obj_offset && target_offset < obj_end);
            
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
                        uint32_t prev_end = infotb[i-1].offset_in_heap + infotb[i-1].size;
                        
                        // 检查偏移量是否连续
                        if (prev_end == infotb[i].offset_in_heap) {
                            // 合并块
                            infotb[i-1].size += infotb[i].size;
                            
                            // 删除当前块
                            linearTBSerialDelete(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i,
                                i,
                                infotb,
                                sizeof(HeapObjectMetav2)
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
                        uint32_t curr_end = infotb[i].offset_in_heap + infotb[i].size;
                        
                        // 检查偏移量是否连续
                        if (curr_end == infotb[i+1].offset_in_heap) {
                            // 合并块
                            infotb[i].size += infotb[i+1].size;
                            
                            // 删除后一个块
                            linearTBSerialDelete(
                                &first_static_heap.heap.metaInfo.header.objMetaCount,
                                i+1,
                                i+1,
                                infotb,
                                sizeof(HeapObjectMetav2)
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

kpoolmemmgr_t::kpoolmemmgr_t() {
#ifdef TEST_MODE
    first_static_heap.heap.heapStart = (phyaddr_t)malloc(1ULL<<22);
    first_static_heap.heap.heapVStart = first_static_heap.heap.heapStart;
    first_static_heap.heap.heapSize = first_static_heap.heap.freeSize = 1ULL<<22;
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
    first_static_heap.heap.metaInfo.header.version = 2; // 版本改为2，表示使用v2元信息
    first_static_heap.heap.metaInfo.header.objMetaCount = 1;  // 初始时有一个空闲对象
    first_static_heap.heap.metaInfo.header.objMetaMaxCount = FirstStaticHeapMaxObjCount;
    first_static_heap.heap.metaInfo.objMetaTable = objMetaTable_for_FirstStaticHeap;
    
    // 初始化第一个元信息项（整个堆作为空闲块）
    objMetaTable_for_FirstStaticHeap[0].offset_in_heap = 0;
    objMetaTable_for_FirstStaticHeap[0].size = first_static_heap.heap.heapSize;
    objMetaTable_for_FirstStaticHeap[0].type = OBJ_TYPE_FREE;
    
    // 初始化链表指针
    first_static_heap.prev = nullptr;
    first_static_heap.next = nullptr;
    last_heap_node = &first_static_heap;
    
    // 初始化flags结构
    kpoolmemmgr_flags.ableto_Expand = 0;
    kpoolmemmgr_flags.heap_vaddr_enabled = 1; // 默认启用虚拟地址
    kpoolmemmgr_flags.alignment = 3; // 默认8字节对齐 (2^3=8)
    
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
    uint64_t phyaddr_temp = 0;
    uint64_t vaddr_temp = 0;
    for(uint32_t i = 0; i < count; i++) {
        HeapObjectMetav2* entry = &meta->objMetaTable[i];
        
        // 打印索引
        kpnumSecure(&i, UNDEC, 3);
        kputsSecure(" | ");
        
        // 物理地址
        phyaddr_temp = entry->offset_in_heap+node->heap.heapStart;
        kpnumSecure(&phyaddr_temp, UNHEX, sizeof(phyaddr_t));
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
        vaddr_temp = entry->offset_in_heap+node->heap.heapVStart;
        kpnumSecure(&vaddr_temp, UNHEX, sizeof(vaddr_t));
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
HCB::HCB(/* args */)
{
}

HCB::~HCB()
{
}