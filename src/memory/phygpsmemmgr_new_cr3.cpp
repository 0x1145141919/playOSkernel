#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#include "pgtable45.h"
void KernelSpacePgsMemMgr::enable_new_cr3()
{
   switch (cpu_pglv)
   {
   case 4:

    return;
   
   case 5:
   return;
    default:return;
   }
    
}
int KernelSpacePgsMemMgr::pgtb_construct_4lvpg()
{
    return 0;
}
int KernelSpacePgsMemMgr::pgtb_construct_5lvpg()
{
    return 0;
}