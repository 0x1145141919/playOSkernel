#pragma once
#include <stdint.h>
#include <os_error_definitions.h>
namespace MEMMODULE_LOCAIONS{

        constexpr uint8_t INVALID=0;
        constexpr uint8_t LOCATION_CODE_BASE_MEMMGR=1;
        constexpr uint8_t LOCATION_CODE_PHYMEM_ACCESSOR=2;
        
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR=8;//[8~15]是phymemspace_mgr的子模块
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_LOW1MB_MGR=9;
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_ATOM_PAGES_COMPLEX_STRUCT=10;
        constexpr uint8_t LOCATION_CODE_PHYMEMSPACE_MGR_MEMSEG_DOUBLE_LINK_LIST=11;
        constexpr uint8_t LOCATION_CODE_KSPACE_MAP_MGR=16;
        
        constexpr uint8_t LOCATION_CODE_ADDRESSPACE=24;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR=28;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK=32;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY=33;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST=34;
    
}