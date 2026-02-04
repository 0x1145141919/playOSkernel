#include "core_hardwares/HPET.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "util/OS_utils.h"
HPET_driver_only_read_time_stamp*readonly_timer=nullptr;
HPET_driver_only_read_time_stamp::HPET_driver_only_read_time_stamp(HPET::ACPItb::HPET_Table * hpet_table)
{
    table = hpet_table;
    phy_reg_base = 0;
    virt_reg_base = 0;
    hpet_timer_period_fs = 0;
    comparator_count = 0;
}
KURD_t HPET_driver_only_read_time_stamp::default_kurd()
{
    return KURD_t(0,0,module_code::DEVICES_CORE,COREHARDWARES_LOCATIONS::LOCATION_CODE_HPET,0,0,err_domain::CORE_MODULE);
}
KURD_t HPET_driver_only_read_time_stamp::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t HPET_driver_only_read_time_stamp::second_stage_init()
{
    KURD_t status = KURD_t();
    KURD_t fail=default_kurd();
    fail=set_result_fail_and_error_level(fail);
    fail.event_code=COREHARDWARES_LOCATIONS::HPET_READONLY_DRIVERS_EVENTS::INIT;
    //先防重复调用
    if (virt_reg_base != 0||
        phy_reg_base != 0||
        hpet_timer_period_fs != 0)
        {
            fail.reason=COREHARDWARES_LOCATIONS::HPET_READONLY_DRIVERS_EVENTS::INIT_RESULTS::FAIL_REASONS::ALLREADE_INIT;
            return fail;
        }


     //再对ACPI表地址合法性进行检查
     if(table == nullptr){
            fail.reason=COREHARDWARES_LOCATIONS::HPET_READONLY_DRIVERS_EVENTS::INIT_RESULTS::FAIL_REASONS::INVALID_ACPI_ADDR;
            return fail;
        }
    phy_reg_base = table->Base_Address;
    if(phy_reg_base%4096 != 0){
            fail.reason=COREHARDWARES_LOCATIONS::HPET_READONLY_DRIVERS_EVENTS::INIT_RESULTS::FAIL_REASONS::ACPI_ADDR_NOT_ALIGN;
            return fail;
        }
    //先找物理页框系统注册mmio物理页
    status=phymemspace_mgr::pages_mmio_regist(phy_reg_base,1);
    if(status.event_code==MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_MMIO_REGIST||
    status.reason==MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::MMIO_REGIST_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MMIOSEG_NOT_EXIST)//说明不落在一个mmio段内
        {
            status=phymemspace_mgr::blackhole_acclaim(
                phy_reg_base,
                1,
                MMIO_SEG,
                phymemspace_mgr::blackhole_acclaim_flags_t{0}
            );
            if(status.result!=result_code::SUCCESS)
                return status;
            status=phymemspace_mgr::pages_mmio_regist(phy_reg_base,1);
            if(status.result!=result_code::SUCCESS)
                return status;
        }
    pgaccess access=KspaceMapMgr::PG_RW;
    access.cache_strategy=UC;
    virt_reg_base=(vaddr_t)KspaceMapMgr::pgs_remapp(status,
        phy_reg_base,
        4096,
        access
    );
    if(virt_reg_base==0)
        return status;
    uint64_t gen_cap_id_reg=atomic_read64_rmb((void*)(virt_reg_base+HPET::regs::offset_General_Capabilities_and_ID));
    hpet_timer_period_fs=gen_cap_id_reg>>HPET::regs::COUNTER_CLK_PERIOD_LEFT_OFFSET;
    comparator_count=((gen_cap_id_reg>>HPET::regs::COMPARATOR_COUNT_LEFT_OFFSET)&HPET::regs::COMPARATOR_COUNT_MASK)+1;
    for(uint8_t i=0;i<comparator_count;i++)
    {
        // 初始化每个定时器,关闭其中断使能
        vaddr_t comparator_config_regbase=virt_reg_base
            +HPET::regs::offset_Timer0_Configuration_and_Capabilities
            +i*HPET::regs::size_per_timer;
        uint64_t comparator_config_reg=atomic_read64_rmb((void*)(comparator_config_regbase));
        comparator_config_reg &= ~HPET::regs::Tn_INT_ENB_CNF_BIT;
        atomic_write64_rdbk((void*)(comparator_config_regbase), comparator_config_reg);
    }
    // 启动HPET
    uint64_t gen_config_reg=atomic_read64_rmb((void*)(virt_reg_base+HPET::regs::offset_General_Config));
    gen_config_reg |= HPET::regs::GCONFIG_ENABLE_BIT;
    atomic_write64_rdbk((void*)(virt_reg_base+HPET::regs::offset_General_Config), gen_config_reg);
    KURD_t success=default_success();
    success.event_code=COREHARDWARES_LOCATIONS::HPET_READONLY_DRIVERS_EVENTS::INIT;
    return success;
}

HPET_driver_only_read_time_stamp::~HPET_driver_only_read_time_stamp()
{
    KURD_t status = KURD_t();
    status=phymemspace_mgr::pages_mmio_unregist(phy_reg_base,1);
    if(status.result!=result_code::SUCCESS)
        return;
    status=KspaceMapMgr::pgs_remapped_free(virt_reg_base);
    if(status.result!=result_code::SUCCESS)
        return;
    uint64_t gen_config_reg=atomic_read64_rmb((void*)(virt_reg_base+HPET::regs::offset_General_Config));
    gen_config_reg &= ~HPET::regs::GCONFIG_ENABLE_BIT;
    atomic_write64_rdbk((void*)(virt_reg_base+HPET::regs::offset_General_Config), gen_config_reg);
    phy_reg_base = 0;
    virt_reg_base = 0;
    hpet_timer_period_fs = 0;
    comparator_count = 0;   
}

uint64_t HPET_driver_only_read_time_stamp::get_time_stamp_in_ns()
{
    uint64_t tmp_count=atomic_read64_rmb((void*)(virt_reg_base+HPET::regs::offset_main_counter_value));
    __uint128_t result=__uint128_t(tmp_count)*hpet_timer_period_fs/1000000;
    return uint64_t(result);
}

uint64_t HPET_driver_only_read_time_stamp::get_time_stamp_in_mius()
{
    uint64_t tmp_count=atomic_read64_rmb((void*)(virt_reg_base+HPET::regs::offset_main_counter_value));
    __uint128_t result=__uint128_t(tmp_count)*hpet_timer_period_fs/1000000000;
    return uint64_t(result);
}
