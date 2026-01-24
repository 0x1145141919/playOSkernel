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

int HPET_driver_only_read_time_stamp::second_stage_init()
{
    int status = 0;
    //先防重复调用
    if (virt_reg_base != 0||
        phy_reg_base != 0||
        hpet_timer_period_fs != 0)
        return 0;


     //再对ACPI表地址合法性进行检查
     if(table == nullptr)   
        return HPET::error_code::ERROR_INVALID_ACPI_ADDR;
    phy_reg_base = table->Base_Address;
    if(phy_reg_base%4096 != 0)
        return HPET::error_code::ERROR_ACPI_ADDR_NOT_ALIGN;
    //先找物理页框系统注册mmio物理页
    status=phymemspace_mgr::pages_mmio_regist(phy_reg_base,1);
    if(status==OS_INVALID_ADDRESS)//说明不落在一个mmio段内
        {
            status=phymemspace_mgr::blackhole_acclaim(
                phy_reg_base,
                1,
                phymemspace_mgr::MMIO_SEG,
                phymemspace_mgr::blackhole_acclaim_flags_t{0}
            );
            if(status!=OS_SUCCESS)
                return status;
            status=phymemspace_mgr::pages_mmio_regist(phy_reg_base,1);
            if(status!=OS_SUCCESS)
                return status;
        }
    pgaccess access=KspaceMapMgr::PG_RW;
    access.cache_strategy=UC;
    virt_reg_base=(vaddr_t)KspaceMapMgr::pgs_remapp(
        phy_reg_base,
        4096,
        access
    );
    if(virt_reg_base==0)
        return HPET::error_code::ERROR_INVALID_STATE;
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
    return OS_SUCCESS;
}

HPET_driver_only_read_time_stamp::~HPET_driver_only_read_time_stamp()
{
    int status = 0;
    status=phymemspace_mgr::pages_mmio_unregist(phy_reg_base,1);
    if(status!=OS_SUCCESS)
        return;
    status=KspaceMapMgr::pgs_remapped_free(virt_reg_base);
    if(status!=OS_SUCCESS)
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
