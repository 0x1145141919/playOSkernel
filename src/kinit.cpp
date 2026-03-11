#include "core_hardwares/PortDriver.h"
#include "os_error_definitions.h"
#include "core_hardwares/primitive_gop.h"
#include "kcirclebufflogMgr.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "core_hardwares/HPET.h"
#include "Interrupt_system/loacl_processor.h"
#include "core_hardwares/lapic.h"
#include "memory/Memory.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/FreePagesAllocator.h"
#include "memory/init_memory_info.h"
#include "util/cpuid_intel.h"
#include "memory/AddresSpace.h"
#include "util/OS_utils.h"
#include "msr_offsets_definitions.h"
#include "time.h"
#include "util/kout.h"
#include "util/textConsole.h"
#include "firmware/UefiRunTimeServices.h"
#include "panic.h"
#include "firmware/gSTResloveAPIs.h"
#include "util/kptrace.h"
#include "firmware/ACPI_APIC.h"
#include "Interrupt_system/AP_Init_error_observing_protocol.h"
#include "Scheduler/per_processor_scheduler.h"

#undef __stack_chk_fail
extern  void __wrap___stack_chk_fail(void);
// 定义C++运行时需要的符号
 extern "C" {
    // DSO句柄，对于静态链接的内核，可以简单定义为空
    void* __dso_handle = 0;
    
    // 用于注册析构函数的函数，这里提供一个空实现
    int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
        (void)func;
        (void)arg;
        (void)dso;
        return 0;
    }
}
extern "C" void delay(unsigned int milliseconds) {
    for (unsigned int i = 0; i < milliseconds * 10000; ++i) {
        // 空循环，占用 CPU
        asm volatile("nop"); // 防止编译器优化（可选）
    }
}
EFI_TIME global_time;
uint32_t efi_map_ver;
void ipi_test(){
    uint32_t self_processor_id=fast_get_processor_id();
    kio::bsp_kout<<"processor id "<< self_processor_id<<kio::kendl;
    //asm volatile("hlt");
}
extern "C" void ap_norm_start( ){

}
void create_first_kthread(){
    ktime::time_interrupt_generator::set_clock_by_offset(20000);
    textconsole_GoP::RuntimeInitServiceThread();
    GlobalKernelStatus=SCHEDUL_READY;
    x2apic::x2apic_driver::broadcast_exself_fixed_ipi(ipi_test);
    per_processor_scheduler*scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    scheduler->schedule_and_switch();

}
extern "C" uint32_t assigned_cr3;
loaded_VM_interval* VM_intervals;
uint64_t VM_intervals_count;
phymem_segment *phymem_segments;
uint64_t phymem_segments_count; 
extern "C" void kernel_start(init_to_kernel_info* transfer) 
{   
    GlobalKernelStatus=kernel_state::ENTER;
    int  Status=0;
    KURD_t bsp_init_kurd=KURD_t();
    pass_through_device_info*tfg=nullptr;
    loaded_VM_interval*graphic_buffer,*logbuffer,*first_heap,*first_heap_bitmap,*first_BCB_bitmap,*kspaceUPpdpt,*ksymbols;
    for(uint64_t i=0;i<transfer->pass_through_device_info_count;i++){
        if(transfer->pass_through_devices[i].device_info==PASS_THROUGH_DEVICE_GRAPHICS_INFO){
            tfg=(pass_through_device_info*)&transfer->pass_through_devices[i];
        }
    }
    for(uint64_t i=0;i<transfer->loaded_VM_interval_count;i++){
        switch (transfer->loaded_VM_intervals[i].VM_interval_specifyid)
        {
        case VM_ID_BSP_INIT_STACK:
            
            break;
        
        case VM_ID_FIRST_HEAP_BITMAP:
            first_heap_bitmap = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_FIRST_HEAP:
            first_heap = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_LOGBUFFER:
            logbuffer = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_FIRST_BCB_BITMAP:
            first_BCB_bitmap = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_KSYMBOLS:
            ksymbols = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_UP_KSPACE_PDPT:
            kspaceUPpdpt = &transfer->loaded_VM_intervals[i];
            break;
        
        case VM_ID_GRAPHIC_BUFFER:
            graphic_buffer = &transfer->loaded_VM_intervals[i];
            break;
        
        default:
            // 未知的 VM interval，可以选择忽略或记录日志
            break;
        }
    }
    bsp_init_kurd=GfxPrim::Init((GlobalBasicGraphicInfoType*)tfg->specify_data,*graphic_buffer);//要开发直接写图形缓冲区的接口
    if(error_kurd(bsp_init_kurd)){
        asm volatile("hlt");
    }
    ktime::hardware_time::try_tsc();
    ksymmanager::Init(ksymbols,transfer->ksymbols_file_size);  
    kpoolmemmgr_t::Init(first_heap,first_heap_bitmap);  
    global_gST=transfer->gST_ptr;
    DmesgRingBuffer::Init(logbuffer);
    Vec2i font_vec={.x=16, .y=32};
    bsp_init_kurd=textconsole_GoP::Init(&ter16x32_data[0][0][0],font_vec,0x00ffffffff,0);
    serial_init_stage1();
    kio::bsp_kout.Init();
    kio::bsp_kout.shift_dec();
    if (Status!=OS_SUCCESS)
    {
        kio::bsp_kout<<"InitialKernelShellControler Failed\n";
        return ;
    }
    kio::bsp_kout<<"Kernel Shell Initialed Success\n";
    VM_intervals=new loaded_VM_interval[transfer->loaded_VM_interval_count];
    ksystemramcpy(transfer->loaded_VM_intervals,VM_intervals,transfer->loaded_VM_interval_count*sizeof(loaded_VM_interval));
    phymem_segments=new phymem_segment[transfer->phymem_segment_count];
    ksystemramcpy(transfer->memory_map,phymem_segments,transfer->phymem_segment_count*sizeof(phymem_segment));
    VM_intervals_count=transfer->loaded_VM_interval_count;
    phymem_segments_count=transfer->phymem_segment_count;
    GlobalKernelStatus=kernel_state::PANIC_WILL_ANALYZE;
    Panic::will_check();
    if(transfer->kmmu_root_table>=0x100000000){
        kio::bsp_kout<<"Kernel Mmu Root Table is not in low memory"<<kio::kendl;
        asm volatile("hlt");
    }
    assigned_cr3=transfer->kmmu_root_table;
    asm volatile("sfence");
    bsp_init_kurd=phymemspace_mgr::Init(transfer);
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"phymemspace_mgr Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    bsp_init_kurd=FreePagesAllocator::Init(first_BCB_bitmap);//传入一个loaded_VM_entry
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"FreePagesAllocator Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    bsp_init_kurd=KspaceMapMgr::Init(kspaceUPpdpt);
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"KspaceMapMgr Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    gKernelSpace=new AddressSpace();
    bsp_init_kurd=gKernelSpace->second_stage_init();//传入trasfer,特殊重载，要接手相关内存
    bsp_init_kurd=[&](init_to_kernel_info*transfer)->KURD_t{
        loaded_VM_interval*loaded_VM_interval_base=transfer->loaded_VM_intervals;
        phymem_segment*phymem_segment_base=transfer->memory_map;
        KURD_t kurd=KURD_t();
        pgaccess WB_ACCESS={
                .is_kernel=1,
                .is_writeable=1,
                .is_readable=1,
                .is_executable=0,
                .is_global=0,
                .cache_strategy=WB
            };
        pgaccess UC_ACCESS={
                .is_kernel=1,
                .is_writeable=1,
                .is_readable=1,
                .is_executable=0,
                .is_global=0,
                .cache_strategy=UC
            };
        pgaccess WB_RX_ACCESS{
            .is_kernel=1,
                .is_writeable=1,
                .is_readable=0,
                .is_executable=1,
                .is_global=0,
                .cache_strategy=WB
        };
        VM_DESC identity_map_template={
            .start=0,
            .end=0,
            .map_type=VM_DESC::MAP_PHYSICAL,
            .phys_start=0,
            .access=WB_ACCESS,
            .committed_full=1,
            .is_vaddr_alloced=1,
            .is_out_bound_protective=0,
            .SEG_SIZE_ONLY_UES_IN_BASIC_SEG=0xffffffffffffffff
        };
        for(uint64_t i=0;i<transfer->phymem_segment_count;i++){
            identity_map_template.start=phymem_segment_base[i].size;
            identity_map_template.phys_start=phymem_segment_base[i].start;
            identity_map_template.end=phymem_segment_base[i].size+phymem_segment_base[i].start;
            if(phymem_segment_base[i].type==PHY_MEM_TYPE::EFI_MEMORY_MAPPED_IO||
            phymem_segment_base[i].type==PHY_MEM_TYPE::EFI_ACPI_MEMORY_NVS||
            phymem_segment_base[i].type==PHY_MEM_TYPE::EFI_RESERVED_MEMORY_TYPE){
                identity_map_template.access=UC_ACCESS;
            }else{
                identity_map_template.access=WB_ACCESS;
                if(phymem_segment_base[i].type==PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_CODE){
                    identity_map_template.access=WB_RX_ACCESS;
                }
            }
            kurd=gKernelSpace->enable_VM_desc(identity_map_template);
            
            if(error_kurd(kurd))return kurd;
        }
        return kurd;
    }(transfer);
    Status=EFI_RT_SVS::Init(transfer->gST_ptr);
    gAcpiVaddrSapceMgr.Init(global_gST);
    gKernelSpace->unsafe_load_pml4_to_cr3(KERNEL_SPACE_PCID);
    GlobalKernelStatus=kernel_state::MM_READY;
    phymemspace_mgr::subtb_alloc_shift_pages_way(); 
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"textconsole_GoP Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    readonly_timer=new HPET_driver_only_read_time_stamp(
        (HPET::ACPItb::HPET_Table*)gAcpiVaddrSapceMgr.get_acpi_table((char*)"HPET")
    );
    x86_smp_processors_container::template_idt_init();
    x86_smp_processors_container::regist_core(0);
    bsp_init_kurd=readonly_timer->second_stage_init();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"HPET Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    ktime::hardware_time::inform_initialized_hpet();
    ktime::hardware_time::processor_regist();
    
    kio::bsp_kout<<kio::now<<"HPET Initialized Success"<<kio::kendl;
    kio::bsp_kout<<kio::now<<"BSP online"<<kio::kendl;
    gAnalyzer=new APIC_table_analyzer((MADT_Table*)gAcpiVaddrSapceMgr.get_acpi_table("APIC"));
    FreePagesAllocator::second_stage(FreePagesAllocator::BEST_FIT);
    bsp_init_kurd=kpoolmemmgr_t::multi_heap_enable();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"Kpoolmemmgr_t::multi_heap_enable Failed"<<kio::kendl;
    }
    
    ktime::time_interrupt_generator::bsp_init();
    all_scheduler_ptr=new per_processor_scheduler*[gAnalyzer->processor_x64_list->size()];
    bsp_init_kurd=x86_smp_processors_container::AP_Init_one_by_one();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"x86_smp_processors_container::AP_Init_one_by_one Failed maybe code bug"<<kio::kendl;
    }    
    asm volatile("sti");   
    //中断接管工作
    all_scheduler_ptr[0]=new per_processor_scheduler;
    gs_u64_write(SCHEDULER_PRIVATE_GS_INDEX,(uint64_t)all_scheduler_ptr[0]);
    create_first_kthread();
}
extern "C" void ap_final_work();
extern "C" void ap_init(uint32_t processor_id)
{
    longmode_enter_checkpoint.success_word=~processor_id;
    asm volatile("sfence");
    gKernelSpace->unsafe_load_pml4_to_cr3(KERNEL_SPACE_PCID);
    x86_smp_processors_container::regist_core(processor_id); 
    ktime::hardware_time::processor_regist();
    ktime::time_interrupt_generator::ap_init();
    all_scheduler_ptr[processor_id]=new per_processor_scheduler;
    gs_u64_write(SCHEDULER_PRIVATE_GS_INDEX,(uint64_t)all_scheduler_ptr[processor_id]);
    //x2apic::x2apic_driver::write_eoi();
    init_finish_checkpoint.success_word=~query_x2apicid();
    asm volatile("sfence");
    ap_final_work();
}
