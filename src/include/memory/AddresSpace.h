#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include "memory/pgtable45.h"
#include <lock.h>
enum cache_strategy_t:uint8_t
{
    UC=0,
    WC=1,
    WT=4,
    WP=5,
    WB=6,
    UC_minus=7
};
union ia32_pat_t
{
   uint64_t value;
   cache_strategy_t  mapped_entry[8];
};
struct pgaccess
{
    uint8_t is_kernel:1;
    uint8_t is_writeable:1;
    uint8_t is_readable:1;
    uint8_t is_executable:1;
    uint8_t is_global:1;
    cache_strategy_t cache_strategy;
};
struct VM_DESC{

    };
/**
 * 此类的职责就是创建虚拟地址空间，管理虚拟地址空间，
 * 此类的职责有且仅一个功能，就是管理相应的低128t虚拟地址空间，
 */
class AddressSpace//到时候进程管理器可以用这个类创建，但是内核空间还是受内核空间管理器管理
{ private:
    PML4Entry *pml4;//这个是虚拟地址
    phyaddr_t kspace_pml4_phyaddr;
    
    void sharing_kernel_space();//直接使用KernelSpacePgsMemMgr的全局单例
};
    /**
 * 这个类的职责有且仅有一个功能，就是管理内核空间，
 * 通过kspacePML4暴露给AddressSpace::sharing_kernel_space()强制复制高一半pml4e
 * 以及类内的kspaceUPpdpt同步所有进程空间的内核结构
 * 此类全局唯一，只管理高128tb虚拟地址空间，
 */
class KernelSpacePgsMemMgr//使用上面的位域结构体，在初始化函数中直接用，但在后续正式外部暴露接口中对页表项必须用原子操作函数
{
private://后面五级页表的时候考虑选择编译
alignas(4096) PageTableEntryUnion  PML4[512];
    PageTableEntryUnion kspaceUPpdpt[256][512];
public:
const pgaccess PG_RW={1,1,1,0,1,WB};
const pgaccess PG_RWX ={1,1,1,1,1,WB};
const pgaccess PG_R ={1,0,1,0,1,WB};

int pgs_remapped_free(vaddr_t addr);
void*pgs_remapp(phyaddr_t addr,pgaccess access,vaddr_t vbase=0);
    void Init();
    void* v_to_phyaddrtraslation(vaddr_t vaddr);
};
extern KernelSpacePgsMemMgr gKspacePgsMemMgr;