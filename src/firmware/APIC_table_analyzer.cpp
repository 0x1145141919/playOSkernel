#include "firmware/ACPI_APIC.h"
#include "memory/Memory.h"
#include "firmware/gSTResloveAPIs.h"
APIC_table_analyzer*gAnalyzer;
//根据传入的MADT表以状态机的思想遍历填充那五个双向链表
APIC_table_analyzer::APIC_table_analyzer(MADT_Table *madt)
{
    // 初始化各个链表
    processor_x64_list = new Ktemplats::list_doubly<APICtb_analyzed_structures::processor_x64_lapic_struct>();
    io_apic_list = new Ktemplats::list_doubly<APICtb_analyzed_structures::io_apic_structure>();
    isa_interrupt_ovveride_list = new Ktemplats::list_doubly<APICtb_analyzed_structures::ISA_interrupt_ovveride_struct>();
    nmi_sourece_list = new Ktemplats::list_doubly<APICtb_analyzed_structures::nmi_sourece_struct>();
    all_processor_nmi_list = new Ktemplats::list_doubly<APICtb_analyzed_structures::all_processor_nmi_struct>();

    // 获取MADT表的起始地址和结束地址
    uint8_t* current_entry = (uint8_t*)(madt + 1); // 跳过表头
    uint8_t* table_end = (uint8_t*)madt + madt->Header.Length;

    // 遍历MADT表中的每个条目
    while (current_entry < table_end) {
        ACPI_MADT::MADT_entrytype entry_type = (ACPI_MADT::MADT_entrytype)(*current_entry);
        uint8_t entry_length = *(current_entry + 1);

        // 验证条目长度是否有效
        if (current_entry + entry_length > table_end) {
            break; // 防止越界
        }

        switch (entry_type) {
            case ACPI_MADT::LocalAPIC: {
                ACPI_MADT::Local_APIC_entry* local_apic = (ACPI_MADT::Local_APIC_entry*)current_entry;
                // 检查APIC是否启用
                if (local_apic->flags & 1) { // Bit 0 indicates enabled
                    APICtb_analyzed_structures::processor_x64_lapic_struct proc = 
                        APICtb_analyzed_structures::xapic(local_apic);
                    processor_x64_list->push_back(proc);
                }
                break;
            }
            
            case ACPI_MADT::x2LocalAPIC: {
                ACPI_MADT::processor_local_x2apic_structure* x2apic = 
                    (ACPI_MADT::processor_local_x2apic_structure*)current_entry;
                // 检查APIC是否启用
                if (x2apic->flags & 1) { // Bit 0 indicates enabled
                    APICtb_analyzed_structures::processor_x64_lapic_struct proc = 
                        APICtb_analyzed_structures::xapic(x2apic);
                    processor_x64_list->push_back(proc);
                }
                break;
            }
            
            case ACPI_MADT::IOAPIC: {
                ACPI_MADT::io_apic_structure* io_apic = (ACPI_MADT::io_apic_structure*)current_entry;
                APICtb_analyzed_structures::io_apic_structure io_apic_processed = 
                    APICtb_analyzed_structures::ioapic(io_apic);
                io_apic_list->push_back(io_apic_processed);
                break;
            }
            
            case ACPI_MADT::ISOverride: {
                ACPI_MADT::interrupt_source_override* iso = 
                    (ACPI_MADT::interrupt_source_override*)current_entry;
                APICtb_analyzed_structures::ISA_interrupt_ovveride_struct iso_processed = 
                    APICtb_analyzed_structures::isa_interrupt_ovveride(iso);
                isa_interrupt_ovveride_list->push_back(iso_processed);
                break;
            }
            
            case ACPI_MADT::NMI_Source: {
                ACPI_MADT::nmi_source_structure* nmi_src = 
                    (ACPI_MADT::nmi_source_structure*)current_entry;
                APICtb_analyzed_structures::nmi_sourece_struct nmi_processed = 
                    APICtb_analyzed_structures::nmi_sourece(nmi_src);
                nmi_sourece_list->push_back(nmi_processed);
                break;
            }
            
            case ACPI_MADT::LAPIC_NMI: {
                ACPI_MADT::local_apic_nmi_structure* lapic_nmi = 
                    (ACPI_MADT::local_apic_nmi_structure*)current_entry;
                APICtb_analyzed_structures::all_processor_nmi_struct nmi_processed = 
                    APICtb_analyzed_structures::all_processor_nmi(lapic_nmi);
                all_processor_nmi_list->push_back(nmi_processed);
                break;
            }
            
            case ACPI_MADT::Lx2APIC_NMI: {
                ACPI_MADT::local_x2apic_nmi_structure* l2xapic_nmi = 
                    (ACPI_MADT::local_x2apic_nmi_structure*)current_entry;
                APICtb_analyzed_structures::all_processor_nmi_struct nmi_processed = 
                    APICtb_analyzed_structures::all_processor_nmi(l2xapic_nmi);
                all_processor_nmi_list->push_back(nmi_processed);
                break;
            }
            
            default:
                // 对于未知类型，跳过
                break;
        }

        // 移动到下一个条目
        // 确保至少前进1个字节以避免无限循环
        if (entry_length < 2) {
            current_entry += 2; // 至少跳过类型和长度字段
        } else {
            current_entry += entry_length;
        }
    }
}

APIC_table_analyzer::~APIC_table_analyzer()
{
    delete processor_x64_list;
    delete io_apic_list;
    delete isa_interrupt_ovveride_list;
    delete nmi_sourece_list;
    delete all_processor_nmi_list;
}