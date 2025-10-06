#include "global.h"
#include "gSTResloveAPIs.h"
#include  "error.h"
#define OUT_API
#define In_API
#define DEBUG_DRAWING_FUNCTIONS

#ifdef DEBUG_DRAWING_FUNCTIONS
  #define DRAW_DEBUG_PRINT(PrintString) Print(PrintString)
  #define DRAW_DEBUG_PRINT_PARAMS(PrintString, ...) Print(PrintString, __VA_ARGS__)
#else
  #define DRAW_DEBUG_PRINT(PrintString)
  #define DRAW_DEBUG_PRINT_PARAMS(PrintString, ...)
#endif

int OUT_API gSTtraverse_APIs(EFI_SYSTEM_TABLE *gST)
{
    for(int i = 0; i < gST->NumberOfTableEntries; i++)
    {
        
    }
}