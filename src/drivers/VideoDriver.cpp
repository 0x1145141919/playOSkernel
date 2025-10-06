#include"kernelTypename.h"
#define OS_SUCCESS 0
#include "PortDriver.h"
// 添加对error.h的引用，这样可以使用标准错误码
#include "errno.h"
#include "VideoDriver.h"
#include "kcirclebufflogMgr.h"
#include "OS_utils.h"
#ifdef TEST_MODE
#include "stdio.h"
#include <inttypes.h>
#endif
//#define PRINT_OUT
#define MAX_CHAR_WIDTH   32
#define MAX_CHAR_HEIGHT  64
// 调试信息控制宏
#define DEBUG_DRAWING_FUNCTIONS

#ifdef DEBUG_DRAWING_FUNCTIONS
  #define DRAW_DEBUG_PRINT(PrintString) serial_puts((CHAR8*)PrintString)
  #define DRAW_DEBUG_PRINT_PARAMS(PrintString, ...) Print((CHAR8*)PrintString, __VA_ARGS__)
#else
  #define DRAW_DEBUG_PRINT(PrintString)
  #define DRAW_DEBUG_PRINT_PARAMS(PrintString, ...)
#endif
#define SERIAL_DEBUG 
#ifdef SERIAL_DEBUG
  #define SERIAL_PUTS(PrintString) serial_puts((CHAR8*)PrintString)
 
#else
  #define SERIAL_PUTS(PrintString)

#endif
/**
  将 CHAR16 字符串转换为 CHAR8 字符串。
  
  @param[out]  Dest      转换后的 CHAR8 字符串。
  @param[in]   Src       输入的 CHAR16 字符串。
  @param[in]   MaxLength 转换的最大字符数。
  
  @return  成功返回 OS_SUCCESS，失败返回错误码。
**/

UINT8 GlobalKernelShellInputBuffer[4096]={0};
UINT8 GlobalKernelShellOutputBuffer[4096]={0};
// 在此文件中定义这些全局变量，其他文件通过extern声明使用
GlobalBitmapControlerType GlobalCharacterSetBitmapControler={0};
BGRR32bitsRawPictureType SingleCharacterRenderedPicture={0};
GlobalBasicGraphicInfoType GlobalBasicGraphicInfo={0};
KernelShellControllerType KernelShellController={0};

static inline void drawpixel(int x,int y,UINT32 PixelData)
/*理论上只支持PixelBlueGreenRedReserved8BitPerColor像素格式
实际上只要单个像素函数符合32位像素格式，那么其他像素格式都可以位像素格式即可
*/

{
   UINT32* pixel=(UINT32*)GlobalBasicGraphicInfo.FrameBufferBase+x+y*GlobalBasicGraphicInfo.PixelsPerScanLine;
    *pixel=PixelData;
}
static inline UINT32 getpixel(
    int x,
    int y
)
{
    UINT32* pixel=(UINT32*)GlobalBasicGraphicInfo.FrameBufferBase+x+y*GlobalBasicGraphicInfo.PixelsPerScanLine;
    return *pixel;
}

static inline void DrawHorizentalLine(// 这只是一个内部私有函数,
    UINT32 start_vector_x,
    UINT32 start_vector_y,
    int length,
    UINT32 PixelData
)

{  
    if(length<=0)
    {
        for (int i = 0; i > length; i--)
        {
            drawpixel(start_vector_x+i,start_vector_y,PixelData);
        }
        
    }else
    {
        for(int i=0;i<length;i++)
    {
        drawpixel(start_vector_x+i,start_vector_y,PixelData);
    }
    }
    
}

static inline void FillRectangle( // 这是一个内部接口，不会进行安全检查
    UINT32 start_vector_x,
    UINT32 start_vector_y,
    int width,
    int height,
    UINT32 PixelData
)
{   
    if(height>0){
    for (int i = 0; i < height; i++)
    {
        DrawHorizentalLine(start_vector_x, start_vector_y + i, width, PixelData);
    }
    }else{
        for (int i = 0; i >width; i--)
        {
            DrawHorizentalLine(start_vector_x+i, start_vector_y, width, PixelData);
        }
        
    }
}
static inline void kernelshellclearScreenInternal()
{
    FillRectangle(0, 0, 
                  GlobalBasicGraphicInfo.horizentalResolution, 
                  GlobalBasicGraphicInfo.verticalResolution, 
                  KernelShellController.UnrenderedSpaceColor);
    KernelShellController.CharacterBeginX = 0;
    KernelShellController.CharacterBeginY = 0;
    KernelShellController.ScreenModeFlag &= 0xFFFFFFFE;
}

// 2. 渲染边框内部接口
static inline void drawKernelShellBorderInternal()
{
    // 解包边距值
    UINT16 topMargin = KernelShellController.TopMargin;
    UINT16 bottomMargin = KernelShellController.BottomMargin;
    UINT16 leftMargin = KernelShellController.LeftMargin;
    UINT16 rightMargin = KernelShellController.RightMargin;
    
    // 绘制上边框
    FillRectangle(KernelShellController.WindowBeginX,
                  KernelShellController.WindowBeginY,
                  KernelShellController.WindowWidth,
                  topMargin,
                  KernelShellController.MarginColor);
    
    // 绘制下边框
    FillRectangle(KernelShellController.WindowBeginX,
                  KernelShellController.WindowBeginY + KernelShellController.WindowHeight - bottomMargin,
                  KernelShellController.WindowWidth,
                  bottomMargin,
                  KernelShellController.MarginColor);
    
    // 绘制左边框
    FillRectangle(KernelShellController.WindowBeginX,
                  KernelShellController.WindowBeginY + topMargin,
                  leftMargin,
                  KernelShellController.WindowHeight - topMargin - bottomMargin,
                  KernelShellController.MarginColor);
    
    // 绘制右边框
    FillRectangle(KernelShellController.WindowBeginX + KernelShellController.WindowWidth - rightMargin,
                  KernelShellController.WindowBeginY + topMargin,
                  rightMargin,
                  KernelShellController.WindowHeight - topMargin - bottomMargin,
                  KernelShellController.MarginColor);
}

// 3. 向下滚动内部接口
static inline void DrawPicture
(  
     int start_vector_x,
      int start_vector_y,
      BGRR32bitsRawPictureType* picture
)
{ 
    for (UINT32 i = 0; i < picture->height; i++)
    {
        for (UINT32 j = 0; j < picture->width; j++)
        {
            drawpixel(start_vector_x + j, start_vector_y + i, picture->pixelDataBase[i * picture->width + j]);
        }
        
    }
    
}

static inline int drawCharacterWithoutRendered(int start_x, int start_y, CHAR16 ch)
{
     UINT8 ascii = (UINT8)ch;
    UINT32 chheight = GlobalCharacterSetBitmapControler.CharacterHeight;
    UINT32 chwidth = GlobalCharacterSetBitmapControler.CharacterWidth;
    UINT8 last_bit_index = chwidth % 8;
    UINT32 widthInByte=(chwidth+8-1)/8;
    UINT8 *WillBeFilttedBytePtr = GlobalCharacterSetBitmapControler.BitMapBase+ascii*chheight*widthInByte;
    static const UINT8 Filterbitmasks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    switch (KernelShellController.CharacterSetType)
    {
    case ASCII:
       
    
    for (UINT32 i = 0; i < chheight; i++)
    {
       if (last_bit_index != 0)
       {
        for (UINT32 j = 0; j < widthInByte; j++)
        {   
            if (j==widthInByte-1)
            {
                for (UINT8 k = 0; k < last_bit_index; k++)
                {
                    UINT32 pixelColor=*WillBeFilttedBytePtr& Filterbitmasks[k]?GlobalCharacterSetBitmapControler.CharacterColor:GlobalCharacterSetBitmapControler.BackGroundColor;
                    drawpixel(start_x + j * 8 + k, start_y + i, pixelColor);
                }
                WillBeFilttedBytePtr++;
                break;
            }
            
            for (UINT32 k = 0; k < 8; k++)
            {
               UINT32 pixelColor=*WillBeFilttedBytePtr& Filterbitmasks[k]?GlobalCharacterSetBitmapControler.CharacterColor:GlobalCharacterSetBitmapControler.BackGroundColor;
                    drawpixel(start_x + j * 8 + k, start_y + i, pixelColor);
            }WillBeFilttedBytePtr++;
        }
       }else{ 
        for (UINT32 j = 0; j < widthInByte; j++)
        {   
            for (UINT32 k = 0; k < 8; k++)
            {
               UINT32 pixelColor=*WillBeFilttedBytePtr& Filterbitmasks[k]?GlobalCharacterSetBitmapControler.CharacterColor:GlobalCharacterSetBitmapControler.BackGroundColor;
                    drawpixel(start_x + j * 8 + k, start_y + i, pixelColor);
            }WillBeFilttedBytePtr++;
        }
    }
       
    }
    return OS_SUCCESS;
    case 1:
        return EOPNOTSUPP;
        break;
    default:
        return EFI_BMP_INVALID;
    }
    
}

static inline int ValidateDrawingParameters(int x, int y)
{
    DRAW_DEBUG_PRINT("ValidateDrawingParameters: x=%d, y=%d\n");
    
    // 帧缓冲区基础状态检查
    if (GlobalBasicGraphicInfo.FrameBufferBase == 0 || 
        GlobalBasicGraphicInfo.FrameBufferSize == 0)
    {
        DRAW_DEBUG_PRINT("ValidateDrawingParameters: Invalid frame buffer\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }

    // 坐标边界检查（单次判断提高效率）
    if ((unsigned int)x >= GlobalBasicGraphicInfo.horizentalResolution || 
        (unsigned int)y >= GlobalBasicGraphicInfo.verticalResolution)
    {
        DRAW_DEBUG_PRINT("ValidateDrawingParameters: Coordinate out of range\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    
    if (x < 0 || y < 0) {
        DRAW_DEBUG_PRINT("ValidateDrawingParameters: Negative coordinates\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }

    // 像素格式检查
   if (GlobalBasicGraphicInfo.pixelFormat != PixelBlueGreenRedReserved8BitPerColor) 
    {
        DRAW_DEBUG_PRINT("ValidateDrawingParameters: Invalid pixel format\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }

    DRAW_DEBUG_PRINT("ValidateDrawingParameters: Success\n");
    return OS_SUCCESS;
}


int InitialKernelShellControler(
    UINT16 WindowHeight,
    UINT16 WindowWidth,
    UINT32 CharacterSetType,
    UINT16 topMargin,
    UINT16 bottomMargin,
    UINT16 leftMargin,
    UINT16 rightMargin,
    UINT8 *InputTextBuffer,
    UINT8 *OutputTextBuffer,
    UINT32 InputTextBufferSize,
    GlobalBitmapControlerType *GlobalBitmapControler,
    UINT32 OutputTextBufferSize,
    UINT16 WindowBeginX,
    UINT16 WindowBeginY
){
    // 1. 检查窗口尺寸
    if(WindowHeight == 0 || WindowWidth == 0) {
        DRAW_DEBUG_PRINT("Window dimensions cannot be zero\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }

    // 2. 检查边距有效性

    
    if (topMargin + bottomMargin >= WindowHeight || 
        leftMargin + rightMargin >= WindowWidth) {
        DRAW_DEBUG_PRINT("InnerMargins exceed window dimensions\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }

    // 3. 检查全局图形信息
    if(GlobalBasicGraphicInfo.FrameBufferBase == 0 || 
       GlobalBasicGraphicInfo.FrameBufferSize == 0) {
        DRAW_DEBUG_PRINT("GlobalBasicGraphicInfo not initialized\n");
        return ENODEV; // 原为OS_UNINITIALIZED
    }
    
    // 4. 检查屏幕边界
    if(WindowBeginX + WindowWidth > GlobalBasicGraphicInfo.horizentalResolution ||
       WindowBeginY + WindowHeight > GlobalBasicGraphicInfo.verticalResolution) {
        DRAW_DEBUG_PRINT("Window exceeds screen: %dx%d vs %dx%d\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    
    // 5. 校验缓冲区
    if(InputTextBuffer == NULL || OutputTextBuffer == NULL) {
        DRAW_DEBUG_PRINT("Text buffer pointers cannot be NULL\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    if(InputTextBufferSize < 16 || OutputTextBufferSize < 16) { // 最小合理值
        DRAW_DEBUG_PRINT("Text buffer size too small\n");
        return ENOBUFS; // 原为OS_INSUFFICIENT_BUFFER
    }
    
    // 6. 检测缓冲区重叠
    UINT8* inputEnd = InputTextBuffer + InputTextBufferSize;
    UINT8* outputEnd = OutputTextBuffer + OutputTextBufferSize;
    if((InputTextBuffer >= OutputTextBuffer && InputTextBuffer < outputEnd) ||
       (OutputTextBuffer >= InputTextBuffer && OutputTextBuffer < inputEnd)) {
        DRAW_DEBUG_PRINT("Input and output buffers overlap\n");
        return EFAULT; // 原为OS_BUFFER_OVERLAP
    }
    
    // 7. 字符集支持
    if(CharacterSetType != ASCII && CharacterSetType != UTF_8) {
        DRAW_DEBUG_PRINT("Unsupported charset: %d\n");
        return EPROTONOSUPPORT; // 原为OS_UNSUPPORTED_FORMAT
    }
    
    // 8. 位图控制器验证
    if(GlobalBitmapControler == NULL) {
        DRAW_DEBUG_PRINT("Bitmap controller pointer cannot be NULL\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    if(GlobalBitmapControler->CharacterHeight == 0 ||
       GlobalBitmapControler->CharacterWidth == 0 ||
       GlobalBitmapControler->BitMapBase == NULL||
       GlobalBitmapControler->CharacterSetType!=KernelShellController.CharacterSetType) {
        DRAW_DEBUG_PRINT("BitmapControler not properly initialized\n");
        return ENODEV; // 原为OS_UNINITIALIZED
    }
    
    // 9. 字符尺寸兼容性检查
    UINT32 contentWidth = WindowWidth - leftMargin - rightMargin;
    UINT32 contentHeight = WindowHeight - topMargin - bottomMargin;
    UINT32 charWidth = GlobalBitmapControler->CharacterWidth;
    UINT32 charHeight = GlobalBitmapControler->CharacterHeight;
    
    if(contentWidth < charWidth || contentHeight < charHeight) {
        DRAW_DEBUG_PRINT("Window too small for characters\n");
        return EDOM; // 原为OS_DIMENSION_MISMATCH
    }
    
    // === 初始化逻辑 ===
    KernelShellController.WindowHeight = WindowHeight;
    KernelShellController.WindowWidth = WindowWidth;
    KernelShellController.CharacterSetType = CharacterSetType;
    KernelShellController.TopMargin = topMargin;
    KernelShellController.BottomMargin = bottomMargin;
    KernelShellController.LeftMargin = leftMargin;
    KernelShellController.RightMargin = rightMargin;
    KernelShellController.InputTextBuffer = InputTextBuffer;
    KernelShellController.OutputTextBuffer = OutputTextBuffer;
    KernelShellController.InputTextBufferSize = InputTextBufferSize;
    KernelShellController.OutputTextBufferSize = OutputTextBufferSize;
    KernelShellController.ScreenModeFlag = 0;
    KernelShellController.UnrenderedSpaceColor = 0x000000;
    KernelShellController.MarginColor = 0x000000FF;
    KernelShellController.CharacterBeginX = KernelShellController.LeftMargin;
    KernelShellController.CharacterBeginY = KernelShellController.TopMargin;
    KernelShellController.WindowBeginX = WindowBeginX;
    KernelShellController.WindowBeginY = WindowBeginY;
    KernelShellController.BitmapControler = GlobalBitmapControler;
    kernelshellclearScreenInternal();   
    drawKernelShellBorderInternal();
    return OS_SUCCESS;
}

int InitialGlobalBasicGraphicInfo(
        UINT32 horizentalResolution,
    UINT32 verticalResolution,
    EFI_GRAPHICS_PIXEL_FORMAT pixelFormat,
    UINT32 PixelsPerScanLine,
    EFI_PHYSICAL_ADDRESS FrameBufferBase,
    UINT32 FrameBufferSize
){
    // === 新增参数校验 ===
    if(horizentalResolution == 0 || verticalResolution == 0) {
        DRAW_DEBUG_PRINT("InitialGlobalBasicGraphicInfo: Invalid resolution\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }
    
    if(FrameBufferBase == 0 || FrameBufferSize == 0) {
        DRAW_DEBUG_PRINT("InitialGlobalBasicGraphicInfo: Invalid frame buffer\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    if(pixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
        DRAW_DEBUG_PRINT("InitialGlobalBasicGraphicInfo: Unsupported pixel format\n");
        return EPROTONOSUPPORT; // 原为OS_UNSUPPORTED_FORMAT
    }
    // === 校验结束 ===
    GlobalBasicGraphicInfo.horizentalResolution=horizentalResolution;
    GlobalBasicGraphicInfo.verticalResolution=verticalResolution;
    GlobalBasicGraphicInfo.pixelFormat=pixelFormat;
    GlobalBasicGraphicInfo.PixelsPerScanLine=PixelsPerScanLine;
    GlobalBasicGraphicInfo.FrameBufferBase=FrameBufferBase;
    GlobalBasicGraphicInfo.FrameBufferSize=FrameBufferSize;
    return OS_SUCCESS;
}

int InitialGlobalCharacterSetBitmapControler(
    UINT32 CharacterHeight,
    UINT32 CharacterWidth,
    UINT32 BackGroundColor,
    UINT32 CharacterColor,
    UINT8* BitMapBase,
    BOOLEAN isPreRendered,
    UINT32* ReanderedCharactersBase
)
{
    // === 新增参数校验 ===
    if(CharacterHeight == 0 || CharacterWidth == 0) {
        DRAW_DEBUG_PRINT("InitialGlobalCharacterSetBitmapControler: Invalid character size\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }
    
    if(BitMapBase == NULL) {
        DRAW_DEBUG_PRINT("InitialGlobalCharacterSetBitmapControler: NULL bitmap base\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    if(isPreRendered && ReanderedCharactersBase == NULL) {
        DRAW_DEBUG_PRINT("InitialGlobalAsciiBitmapControler: Pre-rendered requires valid render base\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    if(CharacterHeight > MAX_CHAR_HEIGHT || CharacterWidth > MAX_CHAR_WIDTH) {
        DRAW_DEBUG_PRINT("Character size exceeds limit (H:%d W:%d)\n");
        return EDOM; // 原为OS_DIMENSION_EXCEEDED
    }
    GlobalCharacterSetBitmapControler.CharacterHeight=CharacterHeight;
    GlobalCharacterSetBitmapControler.CharacterWidth=CharacterWidth;
    GlobalCharacterSetBitmapControler.BackGroundColor=BackGroundColor;
    GlobalCharacterSetBitmapControler.CharacterColor=CharacterColor;
    GlobalCharacterSetBitmapControler.BitMapBase=BitMapBase;
    GlobalCharacterSetBitmapControler.isPreRendered=isPreRendered;
    GlobalCharacterSetBitmapControler.ReanderedCharactersBase=ReanderedCharactersBase;
    return OS_SUCCESS;
}

/*int KernelShellOutInterfaceputs(
    UINT8* string
){

}*/

int DrawHorizentalLineSecure(
    UINT32 start_vector_x,
    UINT32 start_vector_y,
    UINT32 length,
    UINT32 PixelData
)
/*Secure 版本是外部调用接口，会进行安全检查
*/
{
    DRAW_DEBUG_PRINT("DrawHorizentalLineSecure: x=%d, y=%d, length=%d\n");
    
    if (ValidateDrawingParameters(start_vector_x, start_vector_y) != OS_SUCCESS)
    {
        DRAW_DEBUG_PRINT("DrawHorizentalLineSecure: Invalid parameters\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }
    if(start_vector_x+length    >=GlobalBasicGraphicInfo.horizentalResolution) {
        DRAW_DEBUG_PRINT("DrawHorizentalLineSecure: Coordinate out of range\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
     
    DrawHorizentalLine(start_vector_x, start_vector_y, length, PixelData);
    DRAW_DEBUG_PRINT("DrawHorizentalLineSecure: Success\n");
    return OS_SUCCESS;
}

int FillRectangleSecure(int x, int y, int width, int height, UINT32 PixelData)
{
    DRAW_DEBUG_PRINT("FillRectangleSecure: x=%d, y=%d, width=%d, height=%d\n");
    
    // 参数校验
    int status = ValidateDrawingParameters(x, y);
    if(status != OS_SUCCESS) {
        DRAW_DEBUG_PRINT("FillRectangleSecure: Invalid parameters\n");
        return status;
    }
    if (x+width>(int)GlobalBasicGraphicInfo.horizentalResolution||y+height>(int)GlobalBasicGraphicInfo.verticalResolution)
    {
        DRAW_DEBUG_PRINT("FillRectangleSecure: Coordinate out of range\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    
    // 调用内部高效实现
    FillRectangle(x, y, width, height, PixelData);
    DRAW_DEBUG_PRINT("FillRectangleSecure: Success\n");
    return OS_SUCCESS;
}

int DrawPictureSecure(int start_vec_x, int start_vec_y, BGRR32bitsRawPictureType* picture)
{
    DRAW_DEBUG_PRINT("DrawPictureSecure: x=%d, y=%d\n");
    
    // 参数校验
    if(picture == NULL) {
        DRAW_DEBUG_PRINT("DrawPictureSecure: NULL picture pointer\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    
    int status = ValidateDrawingParameters(start_vec_x, start_vec_y);
    if(status != OS_SUCCESS) {
        DRAW_DEBUG_PRINT("DrawPictureSecure: Invalid parameters\n");
        return status;
    }
    
    // 图片数据校验
    if(picture->pixelDataBase == NULL) {
        DRAW_DEBUG_PRINT("DrawPictureSecure: NULL pixel data\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    if (start_vec_x+picture->width>=GlobalBasicGraphicInfo.horizentalResolution||start_vec_y+picture->height>=GlobalBasicGraphicInfo.verticalResolution
    ||start_vec_y+picture->height>=GlobalBasicGraphicInfo.verticalResolution
    )
    {
        DRAW_DEBUG_PRINT("DrawPictureSecure: Coordinate out of range\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    
    
    // 调用内部高效实现
    DrawPicture(start_vec_x, start_vec_y, picture);
    DRAW_DEBUG_PRINT("DrawPictureSecure: Success\n");
    return OS_SUCCESS;
}

int DrawVerticalLineSecure(int x, int y, int length, UINT32 PixelData)
{
    DRAW_DEBUG_PRINT("DrawVerticalLineSecure: x=%d, y=%d, length=%d\n");
    
    // 参数校验
    int status = ValidateDrawingParameters(x, y);
    if(status != OS_SUCCESS) {
        DRAW_DEBUG_PRINT("DrawVerticalLineSecure: Invalid parameters\n");
        return status;
    }
    if(y+length>=GlobalBasicGraphicInfo.verticalResolution) {
        DRAW_DEBUG_PRINT("DrawVerticalLineSecure: Coordinate out of range\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    // 内部高效实现
    for(int i = 0; i < length; i++) {
        drawpixel(x, y + i, PixelData);
    }
    
    DRAW_DEBUG_PRINT("DrawVerticalLineSecure: Success\n");
    return OS_SUCCESS;
}

int KernelShellClearScreenSecure()
{
    // 检查帧缓冲区是否初始化
    if (GlobalBasicGraphicInfo.FrameBufferBase == 0 || 
        GlobalBasicGraphicInfo.FrameBufferSize == 0) {
        DRAW_DEBUG_PRINT("KernelShellClearScreenSecure: Frame buffer not initialized\n");
        return ENODEV; // 原为OS_UNINITIALIZED
    }
    
    // 检查Shell控制器是否初始化
    if (KernelShellController.WindowWidth == 0 || 
        KernelShellController.WindowHeight == 0) {
        DRAW_DEBUG_PRINT("KernelShellClearScreenSecure: Shell controller not initialized\n");
        return ENODEV; // 原为OS_UNINITIALIZED
    }
    
    // 调用内部实现
    kernelshellclearScreenInternal();
    return OS_SUCCESS;
}

// 2. 渲染边框安全接口
int DrawKernelShellBorderSecure()
{
    // 检查Shell控制器是否初始化
    if (KernelShellController.WindowWidth == 0 || 
        KernelShellController.WindowHeight == 0) {
        DRAW_DEBUG_PRINT("DrawKernelShellBorderSecure: Shell controller not initialized\n");
        return ENODEV; // 原为OS_UNINITIALIZED
    }
    
    // 解包边距值
    UINT16 topMargin = KernelShellController.TopMargin;
    UINT16 bottomMargin = KernelShellController.BottomMargin;
    UINT16 leftMargin = KernelShellController.LeftMargin;
    UINT16 rightMargin = KernelShellController.RightMargin;
    
    // 验证边距有效性
    if (topMargin + bottomMargin >= KernelShellController.WindowHeight ||
        leftMargin + rightMargin >= KernelShellController.WindowWidth) {
        DRAW_DEBUG_PRINT("DrawKernelShellBorderSecure: Invalid margins\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }
    
    // 验证窗口位置
    if (KernelShellController.WindowBeginX + KernelShellController.WindowWidth > 
            GlobalBasicGraphicInfo.horizentalResolution ||
        KernelShellController.WindowBeginY + KernelShellController.WindowHeight > 
            GlobalBasicGraphicInfo.verticalResolution) {
        DRAW_DEBUG_PRINT("DrawKernelShellBorderSecure: Window out of bounds\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }
    
    // 调用内部实现
    drawKernelShellBorderInternal();
    return OS_SUCCESS;
}

int DrawCharacterWithoutRenderSecure(int start_x, int start_y, CHAR16 ch)
{
    DRAW_DEBUG_PRINT("DrawCharacterSecure: x=%d, y=%d, ch=0x%X\n");

    // 检查全局位图控制器状态
    if (GlobalCharacterSetBitmapControler.BitMapBase == NULL) {
        DRAW_DEBUG_PRINT("DrawCharacterSecure: Uninitialized bitmap controller\n");
        return EFAULT; // 原为OS_INVALID_ADDRESS
    }
    if (GlobalCharacterSetBitmapControler.CharacterWidth == 0 || 
        GlobalCharacterSetBitmapControler.CharacterHeight == 0) {
        DRAW_DEBUG_PRINT("DrawCharacterSecure: Invalid character dimensions\n");
        return EINVAL; // 原为OS_INVALID_ARGUMENT
    }

    // 验证基础坐标
    int status = ValidateDrawingParameters(start_x, start_y);
    if (status != OS_SUCCESS) {
        DRAW_DEBUG_PRINT("DrawCharacterSecure: Invalid start coordinates\n");
        return status;
    }

    // 计算字符边界
    UINT32 charWidth = GlobalCharacterSetBitmapControler.CharacterWidth;
    UINT32 charHeight = GlobalCharacterSetBitmapControler.CharacterHeight;
    UINT32 end_x = start_x + charWidth - 1;
    UINT32 end_y = start_y + charHeight - 1;

    // 验证字符绘制区域
    if (end_x >= GlobalBasicGraphicInfo.horizentalResolution || 
        end_y >= GlobalBasicGraphicInfo.verticalResolution) {
        DRAW_DEBUG_PRINT("DrawCharacterSecure: Character out of bounds (end_x=%d, end_y=%d)\n");
        return ERANGE; // 原为OS_COORDINATE_OUT_OF_RANGE
    }

    // 调用内部绘制函数
    status = drawCharacterWithoutRendered(start_x, start_y, ch);
    if (status != OS_SUCCESS) {
        DRAW_DEBUG_PRINT("DrawCharacterSecure: Internal draw failed (status=%d)\n");
        return status;
    }

    DRAW_DEBUG_PRINT("DrawCharacterSecure: Character drawn successfully\n");
    return OS_SUCCESS;
}
static inline void kernelshellmoveup(
    UINT16 yMovement,
    UINT16 ContentStartX,
    UINT16 ContentStartY,
    UINT16 ContentWidth,
    UINT16 ContentHeight
){ //此函数用lcd绝对坐标系
    
    UINT32 pixel=0;
    for (int y=ContentStartY+yMovement;y<ContentStartY+ContentHeight;y++)//列移动
    {
        for (int x=ContentStartX;x<ContentStartX+ContentWidth;x++)
        {
            pixel=getpixel(x,y);
            drawpixel(x,y-yMovement,pixel);
        }
    }
    FillRectangle(
        ContentStartX,
        ContentStartY+ContentHeight-yMovement,
        ContentWidth,
        yMovement,
        KernelShellController.UnrenderedSpaceColor
    );
}
 int kernelshellmoveupSecure(int yMovement)
{ 
    UINT16 ContentStartX=
    KernelShellController.WindowBeginX+
    KernelShellController.LeftMargin;
    UINT16 ContentStartY=
    +KernelShellController.WindowBeginY+
    +KernelShellController.TopMargin;
    UINT16 ContentWidth=
    KernelShellController.WindowWidth-
    KernelShellController.LeftMargin-
    KernelShellController.RightMargin;
    UINT16 ContentHeight=
    KernelShellController.WindowHeight-
    KernelShellController.TopMargin-
    KernelShellController.BottomMargin;
    if (ContentStartX+ContentWidth>GlobalBasicGraphicInfo.horizentalResolution||
    ContentStartY+ContentHeight>GlobalBasicGraphicInfo.verticalResolution)
    {
        DRAW_DEBUG_PRINT("kernelshellmoveupSecure: Window out of bounds\n");
        return ERANGE;
    }
    kernelshellmoveup(yMovement,ContentStartX,ContentStartY,ContentWidth,ContentHeight);
    return OS_SUCCESS;
}   
static inline int kputchar(char ch) {
    // 1. 获取字符尺寸
    UINT32 charWidth = KernelShellController.BitmapControler->CharacterWidth;
    UINT32 charHeight = KernelShellController.BitmapControler->CharacterHeight;
    
    // 2. 计算内容区域尺寸（相对坐标）
    UINT32 contentWidth = KernelShellController.WindowWidth - 
                          KernelShellController.LeftMargin - 
                          KernelShellController.RightMargin;
    
    UINT32 contentHeight = KernelShellController.WindowHeight - 
                           KernelShellController.TopMargin - 
                           KernelShellController.BottomMargin;
    //2.5 特殊字符处理
    switch (ch)
    {//退格符的处理交给puts使用栈数据结构处理
        case '\n':goto linebreak;

    default:
        break;
    }
    // 3. 检查是否需要换行（使用相对坐标）
    if((KernelShellController.CharacterBeginX + charWidth) > contentWidth) {
linebreak:
        // 换行处理
        KernelShellController.CharacterBeginX = 0; // 不是LeftMargin!
        KernelShellController.CharacterBeginY += charHeight;
        
        // 检查是否需要滚屏
        if((KernelShellController.CharacterBeginY + charHeight) > contentHeight) {
            // 记录当前Y位置（滚屏前）
            UINT16 prevY = KernelShellController.CharacterBeginY;
            
            // 尝试滚屏
            int status = kernelshellmoveupSecure(charHeight);
            if(status != OS_SUCCESS) {
                // 滚屏失败时恢复Y位置
                KernelShellController.CharacterBeginY = prevY;
                return status;
            }
            
            // 滚屏后光标定位在最后一行
            KernelShellController.CharacterBeginY = contentHeight - charHeight;
        }
    }
    
    // 4. 转换为LCD绝对坐标
    UINT16 screenX = KernelShellController.WindowBeginX + 
                     KernelShellController.LeftMargin + 
                     KernelShellController.CharacterBeginX;
    
    UINT16 screenY = KernelShellController.WindowBeginY + 
                     KernelShellController.TopMargin + 
                     KernelShellController.CharacterBeginY;
    
    // 5. 安全渲染
    int status = drawCharacterWithoutRendered(screenX, screenY, (UINT64)ch);
    if(status != OS_SUCCESS) return status;
    
    // 6. 更新光标位置（使用相对坐标）
    KernelShellController.CharacterBeginX += charWidth;
    
    return OS_SUCCESS;
}
#ifdef KERNEL_MODE
int kputcharSecure(UINT8 ch)//对于特殊字符只能处理换行符，退格符等其他特殊字符交由puts使用栈数据结构处理
{

    // ==================== 1. 全局状态校验 ====================
    // 检查帧缓冲区是否初始化
    if (GlobalBasicGraphicInfo.FrameBufferBase == 0 || 
        GlobalBasicGraphicInfo.FrameBufferSize == 0) {
        DRAW_DEBUG_PRINT("Frame buffer not initialized\n");
        return ENODEV; // 设备未准备好
    }

    // 检查Shell控制器是否初始化
    if (KernelShellController.WindowWidth == 0 || 
        KernelShellController.WindowHeight == 0) {
        DRAW_DEBUG_PRINT("Shell controller not initialized\n");
        return ENODEV;
    }

    // 检查位图控制器有效性
    if (KernelShellController.BitmapControler == NULL ||
        KernelShellController.BitmapControler->BitMapBase == NULL) {
        DRAW_DEBUG_PRINT("Invalid bitmap controller\n");
        return EFAULT; // 错误的内存地址
    }

    // ==================== 2. 字符尺寸校验 ====================
    UINT32 charWidth = KernelShellController.BitmapControler->CharacterWidth;
    UINT32 charHeight = KernelShellController.BitmapControler->CharacterHeight;
    
    if (charWidth == 0 || charHeight == 0) {
        DRAW_DEBUG_PRINT("Invalid character dimensions\n");
        return EINVAL; // 无效参数
    }


    

    // ==================== 4. 边界安全检查 ====================
    // 计算内容区域尺寸 (相对坐标)
    UINT32 contentWidth = KernelShellController.WindowWidth - 
                          KernelShellController.LeftMargin - 
                          KernelShellController.RightMargin;
    
    UINT32 contentHeight = KernelShellController.WindowHeight - 
                           KernelShellController.TopMargin - 
                           KernelShellController.BottomMargin;

    // 检查当前光标是否越界
    if (KernelShellController.CharacterBeginX > contentWidth ||
        KernelShellController.CharacterBeginY > contentHeight) {
        DRAW_DEBUG_PRINT("Cursor out of bounds\n");
        return ERANGE; // 超出范围
    }
    kputchar(ch);
    return OS_SUCCESS; 
}
#endif
#ifdef TEST_MODE
int kputcharSecure(UINT8 ch)
{
    putchar(ch);
    return OS_SUCCESS;
}
#endif
#define TextBufferMaxCount 4096
static inline void kputsascii(char*strbuff)//只会打印到屏幕，不会打印到串口
{
    //缓冲区制作
    char ch=0;
    int i1=0;
    for(int i=0;i>=0&&i<TextBufferMaxCount;i++)
    {
        ch=strbuff[i];
        if(32<=ch&&ch<=126)
        {GlobalKernelShellOutputBuffer[i1]=ch;
        i1++;}
        else{
            switch (ch)
            {
            case '\b':
                i1--;
                break;
            case '\n':
            GlobalKernelShellOutputBuffer[i1]=ch;
            i1++;
            break;
            case '\r':
                i1=0;
                break;
            case 0:
                goto bufferout;
                
            default:
                break;
            }
        }
    }
bufferout:    
    for(int i2=0;i2<i1;i2++)
    {
        kputchar(GlobalKernelShellOutputBuffer[i2]);
    }

}
#ifdef KERNEL_MODE
int kputsSecure(char*strbuff)
{
    // 检查帧缓冲区是否初始化
    if (GlobalBasicGraphicInfo.FrameBufferBase == 0 || 
        GlobalBasicGraphicInfo.FrameBufferSize == 0) {
        DRAW_DEBUG_PRINT("Frame buffer not initialized\n");
        return ENODEV; // 设备未准备好
    }

    // 检查Shell控制器是否初始化
    if (KernelShellController.WindowWidth == 0 || 
        KernelShellController.WindowHeight == 0) {
        DRAW_DEBUG_PRINT("Shell controller not initialized\n");
        return ENODEV;
    }

    // 检查位图控制器有效性
    if (KernelShellController.BitmapControler == NULL ||
        KernelShellController.BitmapControler->BitMapBase == NULL) {
        DRAW_DEBUG_PRINT("Invalid bitmap controller\n");
        return EFAULT; // 错误的内存地址
    }

    // ==================== 2. 字符尺寸校验 ====================
    UINT32 charWidth = KernelShellController.BitmapControler->CharacterWidth;
    UINT32 charHeight = KernelShellController.BitmapControler->CharacterHeight;
    
    if (charWidth == 0 || charHeight == 0) {
        DRAW_DEBUG_PRINT("Invalid character dimensions\n");
        return EINVAL; // 无效参数
    }
    switch (KernelShellController.CharacterSetType)
    {
    case UTF_8:
        DRAW_DEBUG_PRINT("UTF-8 is not supported\n");
        return EOPNOTSUPP;
    case ASCII:
#ifdef PRINT_OUT
    kputsascii(strbuff);
    
#endif
        SERIAL_PUTS(strbuff);
        gkcirclebufflogMgr.putsk(strbuff);
        break;
    default:
        DRAW_DEBUG_PRINT("Invalid character set type\n");
        return EINVAL;

    }
    return OS_SUCCESS;
}
#endif
#ifdef TEST_MODE
    int kputsSecure(char* strbuff) {
    if (!strbuff) return -1;
    return printf("%s", strbuff);  // 用户空间直接使用printf
}
#endif
#ifdef KERNEL_MODE


int kpnumSecure(void* numptr, int format, int len)//有符号十进制的情况下只能处理1,2,4,8字节
{
    /* 安全校验 */
    switch (format) {
    case UNBIN:
    case UNDEC:
    case INDEC:
    case UNHEX:
        break;
    default:
        DRAW_DEBUG_PRINT("Invalid number format\n");
        return EINVAL;
    }

    if (len < 1 || len > 8) {
        DRAW_DEBUG_PRINT("Invalid length, must be 1~8\n");
        return EINVAL;
    }

    uint64_t num = 0;
    unsigned char* src = (unsigned char*)numptr;
    unsigned char* dst = (unsigned char*)&num;
    
    // 复制数据到num的低字节部分
    for (int i = 0; i < len; i++) {
        dst[i] = src[i];
    }

    char buf[70];  // 足够容纳所有格式的输出
    int buffer_index = sizeof(buf) - 1;  // 从缓冲区末尾开始填充
    buf[buffer_index] = '\0';  // 字符串终止符

    switch (format) {
    case UNBIN: {
        // 二进制输出：固定位数显示
        int total_bits = len * 8;
        for (int bit_pos = total_bits - 1; bit_pos >= 0; bit_pos--) {
            buffer_index--;
            buf[buffer_index] = (num & (1ULL << bit_pos)) ? '1' : '0';
        }
        break;
    }
    
    case UNDEC: {
        // 无符号十进制输出
        uint64_t value = num;
        if (value == 0) {
            buffer_index--;
            buf[buffer_index] = '0';
        } else {
            while (value) {
                buffer_index--;
                buf[buffer_index] = '0' + (value % 10);
                value /= 10;
            }
        }
        break;
    }
    
    case INDEC: {
        /* 有符号十进制处理原理：
         * 1. 将原始数据扩展为64位有符号整数
         * 2. 处理特殊边界情况：-2⁶³（0x8000000000000000）
         * 3. 负数取绝对值并标记符号
         * 4. 将数字逐位转换为字符
         */
        
        // 符号扩展（将原始数据按当前长度扩展为完整的64位有符号值）
        int64_t s_val;
        switch(len) {
            case 1: s_val = (int8_t)num; break;
            case 2: s_val = (int16_t)num; break;
            case 4: s_val = (int32_t)num; break;
            case 8: s_val = (int64_t)num; break;
            default: 
                serial_puts("Invalid length for INDEC format\n");
                return EINVAL;
        }
        
        // 处理特殊边界情况：-2⁶³（无法直接取绝对值）
        if (len == 8 && num == 0x8000000000000000ULL) {
            kputsascii("-9223372036854775808");
            return 0;
        }
        
        // 处理符号
        int is_negative = 0;
        if (s_val < 0) {
            is_negative = 1;
            s_val = -s_val;
        }
        
        // 数字转换（包括0的处理）
        int start_pos = buffer_index;
        do {
            buffer_index--;
            buf[buffer_index] = '0' + (s_val % 10);
            s_val /= 10;
        } while (s_val > 0);
        
        // 添加符号（如果需要）
        if (is_negative) {
            buffer_index--;
            buf[buffer_index] = '-';
        }
        break;
    }
    
    case UNHEX: {
        // 十六进制输出（智能省略前导零）
        unsigned char* bytes = (unsigned char*)&num;
        
        buffer_index--;
        buf[buffer_index]=0;
        for (int byte_idx = 0; byte_idx <len; byte_idx++) {
            unsigned char byte = bytes[byte_idx];
            // 处理低4位
            unsigned char low_nibble = byte & 0x0F;

                buffer_index--;
                buf[buffer_index] = "0123456789ABCDEF"[low_nibble];

            // 处理高4位
            unsigned char high_nibble = byte >> 4;

                buffer_index--;
                buf[buffer_index] = "0123456789ABCDEF"[high_nibble];

            
            
        }

        break;
    }
    }

    // 输出最终结果
#ifdef PRINT_OUT
    kputsascii(buf + buffer_index);
    
#endif
    SERIAL_PUTS(buf + buffer_index);
    gkcirclebufflogMgr.putsk(buf + buffer_index);
    return 0;
}
#endif
#ifdef TEST_MODE
int kpnumSecure(void* numptr, int format, int len)
{ 
    if (!numptr) return -1;
    
    uint32_t num = *(uint32_t*)numptr;
    char buffer[64] = {0};
    
    switch (format) {
        case UNBIN:  // 二进制
            for (int i = len-1; i >= 0; i--) {
                buffer[len-1-i] = (num & (1 << i)) ? '1' : '0';
            }
            break;
        case UNDEC:  // 无符号十进制
            snprintf(buffer, sizeof(buffer), "%*"PRIu32, len, num);
            break;
        case INDEC:  // 有符号十进制
            snprintf(buffer, sizeof(buffer), "%*"PRId32, len, (int32_t)num);
            break;
        case UNHEX:  // 十六进制
            snprintf(buffer, sizeof(buffer), "%0*"PRIX64, len*2, num);
            break;
        default:
            return -1;
    }
    
    return printf("%s", buffer);
}
#endif