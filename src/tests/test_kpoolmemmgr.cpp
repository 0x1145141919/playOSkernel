#include "kpoolmemmgr.h"
#include "stdlib.h"
#include  <cstdio>
 //gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
    //gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
int main()
{ 
    printf("kpoolmemmgr test start\n");
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    HeapObjectMetav2*meta2=new HeapObjectMetav2[33];
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    HCB_chainlist_node* node=new(false,4) HCB_chainlist_node;
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    delete node;
    delete meta2;
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    
    printf("kpoolmemmgr test end\n");
    return 0;
}