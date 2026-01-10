#pragma once
#include <efi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASCII 0
#define UTF_8 1
typedef struct{ 
    UINT32 horizentalResolution;
    UINT32 verticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT pixelFormat;
    UINT32 PixelsPerScanLine;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINT32 FrameBufferSize;
}GlobalBasicGraphicInfoType;
//给我搜索涉及到这个结构体的实例，相关函数，并且解释那些实例变量，函数的关系
extern GlobalBasicGraphicInfoType GlobalBasicGraphicInfo;
typedef struct{
    UINT32 height;
    UINT32 width;
    UINT32* pixelDataBase;
}BGRR32bitsRawPictureType;/**/
typedef struct{
    UINT32 CharacterWidth;
    UINT32 CharacterHeight;
    BOOLEAN isPreRendered;
    UINT32 CharacterSetType;
    UINT32 BackGroundColor;
    UINT32 CharacterColor;
    UINT8* BitMapBase;
    UINT32* ReanderedCharactersBase;
}
GlobalBitmapControlerType;
extern GlobalBitmapControlerType GlobalCharacterSetBitmapControler;
typedef struct 
{
    UINT16 WindowHeight;//整个shell窗体宽度分辨率
    UINT16 WindowWidth;
    UINT32 CharacterSetType;//暂时只支持ascii,utf8字符集
    // 将原来的UINT64 InnerMargins改为四个UINT16变量
    UINT16 TopMargin;     // 上边框厚度
    UINT16 BottomMargin;  // 下边框厚度
    UINT16 LeftMargin;    // 左边框厚度
    UINT16 RightMargin;   // 右边框厚度
    UINT8 *InputTextBuffer;
    UINT8 *OutputTextBuffer;
    GlobalBitmapControlerType* BitmapControler;
    UINT32 InputTextBufferSize;
    UINT32 OutputTextBufferSize;
    UINT32 ScreenModeFlag;
    /*
    暂定第一位为滚屏标志位，
    为1的时候若要输出字符则要滚动屏幕
    */
    UINT32 MarginColor;
    UINT32 UnrenderedSpaceColor;
    UINT16 WindowBeginX;
    UINT16 WindowBeginY;
    UINT16 CharacterBeginX;//以(windowbeginX,windowBeginY)+（topmargin，leftmargin）为原点的LCD坐标
    UINT16 CharacterBeginY;
    
}KernelShellControllerType;
extern KernelShellControllerType KernelShellController;
// 声明为extern避免多重定义


// 文件读取状态码
#define EFI_BMP_SUCCESS     0
#define EFI_BMP_INVALID    -1
#define EFI_BMP_UNSUPPORTED -2

// 外部安全接口
int DrawHorizentalLineSecure(
    UINT32 start_vector_x,
    UINT32 start_vector_y,
    UINT32 length,
    UINT32 PixelData
);
int FillRectangleSecure(int x, int y, int width, int height, UINT32 PixelData);
int DrawPictureSecure(int start_vec_x, int start_vec_y, BGRR32bitsRawPictureType* picture);
int DrawVerticalLineSecure(int x, int y, int length, UINT32 PixelData);
int DrawCharacterWithoutRenderSecure
(
    int start_x, 
    int start_y, 
    CHAR16 ch
);
int InitialGlobalBasicGraphicInfo(
        UINT32 horizentalResolution,
    UINT32 verticalResolution,
    EFI_GRAPHICS_PIXEL_FORMAT pixelFormat,
    UINT32 PixelsPerScanLine,
    EFI_PHYSICAL_ADDRESS FrameBufferBase,
    UINT32 FrameBufferSize
);
int InitialGlobalCharacterSetBitmapControler(
    UINT32 CharacterHeight,
    UINT32 CharacterWidth,
    UINT32 BackGroundColor,
    UINT32 CharacterColor,
    UINT8* BitMapBase,
    BOOLEAN isPreRendered,
    UINT32* ReanderedCharactersBase
);
int InitialKernelShellControler(
    UINT16 WindowHeight,
    UINT16 WindowWidth,
    UINT32 CharacterSetType,
    UINT16 TopMargin,
    UINT16 BottomMargin,
    UINT16 LeftMargin,
    UINT16 RightMargin,
    UINT8 *InputTextBuffer,
    UINT8 *OutputTextBuffer,
    UINT32 InputTextBufferSize,
    GlobalBitmapControlerType *GlobalBitmapControler,
    UINT32 OutputTextBufferSize,
    UINT16 WindowBeginX,
    UINT16 WindowBeginY
);
extern UINT8 GlobalKernelShellInputBuffer[4096];
extern UINT8 GlobalKernelShellOutputBuffer[4096];
int kputsSecure(char*strbuff);
int kputcharSecure(UINT8 ch);
int kernelshellmoveupSecure(int yMovement);
int kpnumSecure(void* numptr, int format, int len);
int kputchar(char ch);
//这里的len是numptr开始的字节数，不是打印的位数
//比如8位就使用1,16位就使用2,32位就使用4，64位就使用8
#define UNBIN 1
#define UNDEC 3
#define INDEC 4
#define UNHEX 5
#ifdef __cplusplus
}
#endif
