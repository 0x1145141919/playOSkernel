#include "phygpsmemmgr.h"
#include "VideoDriver.h"

void PgsMemMgr::PrintPgsMemMgrStructure()
{
    kputsSecure("=== Page Table Structure ===\n");
    
    // 打印基本信息
    kputsSecure("CPU Page Level: ");
    kputsSecure(" (");
    kpnumSecure(&cpu_pglv, UNHEX, 1);
    kputsSecure(")\n");
    
    kputsSecure("Kernel Space CR3: ");
    kpnumSecure(&kernel_space_cr3, UNHEX, 8);
    kputsSecure("\n");
    
    // 打印标志位
    kputsSecure(", PgtbSituate=");
    uint64_t temp_flag2 = flags.is_pgsallocate_enable;
    kpnumSecure(&temp_flag2, UNDEC, 1);
    kputsSecure("\n\n");
    
    // 打印各级页表结构
    if (cpu_pglv == 5)
    {
        // 五级页表模式
        for (int i = 0; i < 512; i++)
        {
            if (rootlv4PgCBtb[i].flags.is_exist)
            {
                kputsSecure("PML5[");
                kpnumSecure(&i, UNDEC, 3);
                kputsSecure("]: ");
                PrintPageTableEntry(&rootlv4PgCBtb[i], 4);
                
                // 打印下一级
                if (rootlv4PgCBtb[i].flags.is_lowerlv_bitmap)
                {
                    // 处理位图情况
                    if (rootlv4PgCBtb[i].flags.is_reserved)
                    {
                        PrintBitmap2bits(rootlv4PgCBtb[i].base.map_tpye2, 4, i);
                    }
                    else
                    {
                        PrintBitmap1bit(rootlv4PgCBtb[i].base.map_tpye1, 4, i);
                    }
                }
                else if (!rootlv4PgCBtb[i].flags.is_atom && rootlv4PgCBtb[i].base.lowerlvPgCBtb)
                {
                    PrintLevel4Table(rootlv4PgCBtb[i].base.lowerlvPgCBtb, i);
                }
                kputsSecure("\n");
            }
        }
    }
    else
    {
        // 四级页表模式
        kputsSecure("PML4[0]: ");
        PrintPageTableEntry(rootlv4PgCBtb, 4);
        
        // 打印下一级
        if (rootlv4PgCBtb->flags.is_lowerlv_bitmap)
        {
            // 处理位图情况
            if (rootlv4PgCBtb->flags.is_reserved)
            {
                PrintBitmap2bits(rootlv4PgCBtb->base.map_tpye2, 4, 0);
            }
            else
            {
                PrintBitmap1bit(rootlv4PgCBtb->base.map_tpye1, 4, 0);
            }
        }
        else if (!rootlv4PgCBtb->flags.is_atom && rootlv4PgCBtb->base.lowerlvPgCBtb)
        {
            PrintLevel4Table(rootlv4PgCBtb->base.lowerlvPgCBtb, 0);
        }
        kputsSecure("\n");
    }
}

// 辅助函数：打印四级表
void PgsMemMgr::PrintLevel4Table(lowerlv_PgCBtb* table, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if ((table->entries[i]).flags.is_exist)
        {
            kputsSecure("  PML4[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PDPT[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 3);
            
            // 打印下一级
            if (table->entries[i].flags.is_lowerlv_bitmap)
            {
                // 处理位图情况
                if (table->entries[i].flags.is_reserved)
                {
                    PrintBitmap2bits(table->entries[i].base.map_tpye2, 3, i);
                }
                else
                {
                    PrintBitmap1bit(table->entries[i].base.map_tpye1, 3, i);
                }
            }
            else if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel3Table(table->entries[i].base.lowerlvPgCBtb, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印三级表
void PgsMemMgr::PrintLevel3Table(lowerlv_PgCBtb* table, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("    PDPT[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PD[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 2);
            
            // 打印下一级
            if (table->entries[i].flags.is_lowerlv_bitmap)
            {
                // 处理位图情况
                if (table->entries[i].flags.is_reserved)
                {
                    PrintBitmap2bits(table->entries[i].base.map_tpye2, 2, i);
                }
                else
                {
                    PrintBitmap1bit(table->entries[i].base.map_tpye1, 2, i);
                }
            }
            else if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel2Table(table->entries[i].base.lowerlvPgCBtb, grandParentIndex, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印二级表
void PgsMemMgr::PrintLevel2Table(lowerlv_PgCBtb* table, int greatGrandParentIndex, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("      PD[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->PT[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 1);
            
            // 打印下一级
            if (table->entries[i].flags.is_lowerlv_bitmap)
            {
                // 处理位图情况
                if (table->entries[i].flags.is_reserved)
                {
                    PrintBitmap2bits(table->entries[i].base.map_tpye2, 1, i);
                }
                else
                {
                    PrintBitmap1bit(table->entries[i].base.map_tpye1, 1, i);
                }
            }
            else if (!table->entries[i].flags.is_atom && table->entries[i].base.lowerlvPgCBtb)
            {
                PrintLevel1Table(table->entries[i].base.lowerlvPgCBtb, greatGrandParentIndex, grandParentIndex, parentIndex, i);
            }
            kputsSecure("\n");
        }
    }
}

// 辅助函数：打印一级表
void PgsMemMgr::PrintLevel1Table(lowerlv_PgCBtb* table, int greatGreatGrandParentIndex, int greatGrandParentIndex, int grandParentIndex, int parentIndex)
{
    for (int i = 0; i < 512; i++)
    {
        if (table->entries[i].flags.is_exist)
        {
            kputsSecure("        PT[");
            kpnumSecure(&parentIndex, UNDEC, 3);
            kputsSecure("]->Page[");
            kpnumSecure(&i, UNDEC, 3);
            kputsSecure("]: ");
            PrintPageTableEntry(&table->entries[i], 0);
            
            // 计算并打印物理地址
            uint64_t physicalAddr = CalculatePhysicalAddress(
                greatGreatGrandParentIndex, greatGrandParentIndex, 
                grandParentIndex, parentIndex, i, 0);
            
            kputsSecure(" PhysAddr: ");
            kpnumSecure(&physicalAddr, UNHEX, 8);
            kputsSecure("\n");
        }
    }
}

// 辅助函数：计算物理地址
uint64_t PgsMemMgr::CalculatePhysicalAddress(int index5, int index4, int index3, int index2, int index1, int index0)
{
    uint64_t addr = 0;
    
    if (cpu_pglv == 5)
    {
        addr |= (static_cast<uint64_t>(index5) << 48);
    }
    
    addr |= (static_cast<uint64_t>(index4) << 39);
    addr |= (static_cast<uint64_t>(index3) << 30);
    addr |= (static_cast<uint64_t>(index2) << 21);
    addr |= (static_cast<uint64_t>(index1) << 12);
    addr |= (static_cast<uint64_t>(index0));
    
    return addr;
}

// 辅助函数：打印页表项信息
void PgsMemMgr::PrintPageTableEntry(PgControlBlockHeader* entry, int level)
{
    // 打印级别
    kputsSecure((char*)"L");
    kpnumSecure(&level, UNDEC, 1);
    kputsSecure((char*)": ");
    
    // 打印基本标志
    kputsSecure(entry->flags.physical_or_virtual_pg ? (char*)"Virtual " : (char*)"Physical ");
    kputsSecure(entry->flags.is_exist ? (char*)"Exist " : (char*)"NotExist ");
    kputsSecure(entry->flags.is_atom ? (char*)"Atom " : (char*)"NonAtom ");
    kputsSecure(entry->flags.is_dirty ? (char*)"Dirty " : (char*)"Clean ");
    
    // 打印位图类型
    if (entry->flags.is_lowerlv_bitmap)
    {
        kputsSecure(entry->flags.is_reserved ? (char*)"2bits " : (char*)"1bit ");
    }
    else
    {
        kputsSecure((char*)"Table ");
    }
    
    kputsSecure(entry->flags.is_reserved ? (char*)"Reserved " : (char*)"NonReserved ");
    
    // 打印权限标志
    kputsSecure(entry->flags.is_readable ? (char*)"R" : (char*)"-");
    kputsSecure(entry->flags.is_writable ? (char*)"W" : (char*)"-");
    kputsSecure(entry->flags.is_executable ? (char*)"X" : (char*)"-");
    kputsSecure((char*)" ");
    
    // 打印其他标志
    kputsSecure(entry->flags.is_kernel ? (char*)"Kernel " : (char*)"User ");
    kputsSecure(entry->flags.is_locked ? (char*)"Locked " : (char*)"Unlocked ");
    kputsSecure(entry->flags.is_shared ? (char*)"Shared " : (char*)"Private ");
    kputsSecure(entry->flags.is_occupied ? (char*)"Occupied " : (char*)"Free ");
}

// 辅助函数：打印1bit位图
void PgsMemMgr::PrintBitmap1bit(lowerlv_bitmap_entry_width1bit* bitmap_entry, int level, int index)
{
    kputsSecure("      Bitmap1bit[");
    kpnumSecure(&index, UNDEC, 3);
    kputsSecure("]: ");
    
    // 打印位图内容（每64位一行）
    for (int i = 0; i < 64; i++)
    {
        uint8_t byte = bitmap_entry->bitmap[i];
        
        // 打印每个字节的二进制表示
        for (int j = 0; j < 8; j++)
        {
            if (byte & (1 << (7 - j)))
            {
                kputsSecure("1");
            }
            else
            {
                kputsSecure("0");
            }
        }
        
        // 每8字节换行
        if ((i + 1) % 8 == 0)
        {
            kputsSecure("\n                      ");
        }
        else
        {
            kputsSecure(" ");
        }
    }
    kputsSecure("\n");
}

// 辅助函数：打印2bits位图
void PgsMemMgr::PrintBitmap2bits(lowerlv_bitmap_entry_width2bits* bitmap_entry, int level, int index)
{
    kputsSecure("      Bitmap2bits[");
    kpnumSecure(&index, UNDEC, 3);
    kputsSecure("]: ");
    
    // 打印位图内容（每32个条目一行）
    for (int i = 0; i < 128; i++)
    {
        uint8_t byte = bitmap_entry->bitmap[i];
        
        // 每个字节包含4个2bit条目
        for (int j = 0; j < 4; j++)
        {
            int entry_index = i * 4 + j;
            if (entry_index >= 512) break;
            
            // 提取2bit值
            uint8_t value = (byte >> (6 - j * 2)) & 3;
            
            // 根据值打印相应字符
            switch (value)
            {
                case 0: kputsSecure("."); break; // 空闲
                case 1: kputsSecure("X"); break; // 占用
                case 2: kputsSecure("R"); break; // 保留
                case 3: kputsSecure("?"); break; // 未知
            }
            
            // 每32个条目换行
            if ((entry_index + 1) % 32 == 0)
            {
                kputsSecure("\n                      ");
            }
        }
    }
    kputsSecure("\n");
}


