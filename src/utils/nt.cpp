void *kpoolmemmgr_t::kalloc(uint64_t size_in_bytes, bool vaddraquire, uint8_t alignment)
{   
    if(size_in_bytes == 0)return nullptr;
    if (kpoolmemmgr_flags.ableto_Expand == 0) {
        // ... 前面的代码保持不变 ...

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
                        // ... 状态更新代码保持不变 ...
                        
                        // 返回分配的内存地址
                        if (vaddraquire) {
                            return aligned_virt_start;
                        } else {
                            return aligned_phys_start;
                        }
                    } else {
                        // 元信息表已满，无法分割块
                        // 使用整个块，会有内部碎片
                        infotb[i].base = (phyaddr_t)aligned_phys_start;
                        infotb[i].vbase = (vaddr_t)aligned_virt_start;
                        infotb[i].size = block_size - alignment_padding;
                        infotb[i].type = OBJ_TYPE_NORMAL;
                        
                        // 更新堆的剩余空间
                        first_static_heap.heap.freeSize -= (block_size - alignment_padding);
                        // ... 状态更新代码保持不变 ...
                        
                        // 返回分配的内存地址
                        if (vaddraquire) {
                            return aligned_virt_start;
                        } else {
                            return aligned_phys_start;
                        }
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