#include "processor_Ks_stacks_mgr.h"
#include "OS_utils.h"
#include "../memory/includes/phygpsmemmgr.h"
processor_Ks_stacks_mgr gProcessor_Ks_stacks_mgr;
processor_Ks_stacks_mgr::processor_Ks_stacks_mgr(/* args */)
{
}

void processor_Ks_stacks_mgr::Init()
{
    heap_base = (vaddr_t)gKspacePgsMemMgr.pgs_allocate_remapped(
        heap_size+1,
        KernelSpacePgsMemMgr::kspace_data_flags,21);//故意多一个字节，防止越界访问
    setmem((void*)heap_base, heap_size, 0);
}

vaddr_t processor_Ks_stacks_mgr::AllocateStack()
{
        for(int i=0;i<bitset512count;i++)
    {
        for(int j=0;j<512;j++)
        {
            bool var= getbit_entry1bit_width(&heap_maps[i],j);
            if(var==false){
                setbit_entry1bit_width(heap_maps+i,true,j);
                uint64_t offset=( ( (i*512)+j+1) * ks_stack_size);
                return (vaddr_t)(heap_base + offset);
            }
        }
    }
    return (vaddr_t)nullptr;
}

void processor_Ks_stacks_mgr::FreeStack(vaddr_t stack_base)
{
    // 检查地址是否在有效范围内
    if(stack_base < heap_base || stack_base >= (heap_base + heap_size))
    {
        return;
    }

    // 计算相对于堆基址的偏移量
    uint64_t offset = static_cast<uint64_t>(stack_base - heap_base);
    
    // 计算栈索引（分配时+1的逆操作）
    uint64_t stack_index = (offset / ks_stack_size) - 1;
    
    // 计算位图位置
    uint64_t i = stack_index / 512;
    uint64_t j = stack_index % 512;

    // 安全验证位图索引
    if(i >= bitset512count) return;

    // 清除位图标记（false表示空闲）
    setbit_entry1bit_width(heap_maps + i, false, j);
}

processor_Ks_stacks_mgr::~processor_Ks_stacks_mgr()
{
}