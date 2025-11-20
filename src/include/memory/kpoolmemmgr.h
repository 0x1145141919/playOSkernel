#pragma once
#include "stdint.h"
#include "bitmap.h"
//#include <new>
typedef uint64_t size_t;
typedef uint64_t phyaddr_t;
typedef uint64_t vaddr_t;
//此模块待重构

enum data_type_t:uint8_t
{
    DT_UNKNOWN = 0,
    DT_ARRAY = 1,
    DT_STRUCT = 2,
    DT_CLASS = 3,
};
class kpoolmemmgr_t
{
private:
    static constexpr uint64_t HCB_ARRAY_MAX_COUNT = 0x1000;
    class HCB_v2//堆控制块，必须是连续物理地址空间的连续内存
    {
        private:
            // 活跃分配魔数（推荐）
        uint32_t belonged_to_cpu_apicid=0;//只考虑x2apic的32位apic_id
        static constexpr uint64_t MAGIC_ALLOCATED    = 0xDEADBEEFCAFEBABEull;
        class HCB_bitmap:public bitmap
        { 
            public:
            int Init();
            HCB_bitmap();
            ~HCB_bitmap();

            bool target_bit_seg_is_avaliable(uint64_t bit_idx,uint64_t bit_count);//考虑用uint64_t来优化扫描，扫描的时候要加锁
            int bit_seg_set(uint64_t bit_idx,uint64_t bit_count,bool value);//优化的set函数，中间会解析，然后调用对应的bits_set，bytes_set，u64s_set，并且会参数检查，加锁
            friend class HCB_v2;
        };
        HCB_bitmap bitmap_controller;
        phyaddr_t phybase;
        vaddr_t vbase;
        uint32_t total_size_in_bytes;
        struct data_flags{
            uint32_t alignment:4;//实际对齐为2<<alignment字节对齐，最大记录为64kb对齐，实际分配上最大1024字节对齐
            uint32_t is_longtime_alloc:1;//是否是长时间分配的变量
            uint32_t is_crucial_variable:1;//是否是关键变量,如果是，在free，in_heap_realloc的时候检查到魔数被篡改，会触发内核恐慌
        };
        struct alignas(16) data_meta{//每个被分配的都有元信息以及相应魔数
            //是分配在堆内，后面紧接着就是数据
            uint16_t data_size;
            data_type_t data_type;
            data_flags flags;
            uint64_t magic;
        };
        static constexpr uint8_t bytes_per_bit = 0x10;//一个bit控制16字节数
        public: 
        int first_linekd_heap_Init();//只能由first_linekd_heap调用的初始化
        //用指针检验是不是那个特殊堆
        HCB_v2(uint32_t apic_id);//给某个逻辑处理器初始化一个HCB
        ~HCB_v2();
        //分配之后对于数据区的清零操作
        //必须是魔数有效才会操作
        int clear(void*ptr);
        /**
         * @brief 分配内存  
         * 思路：
         * 1.根据大小与对齐需求选择扫描器
         * 2.制造扫描大小
         * 3.使用对应扫描器扫描内存
         * 4.分配数据与元数据
         * 对于16字节对齐的时候是直接元数据，数据一起分配
         * 对于128,1024字节对齐则是元数据单独分配一个bit
         * 遵守最低浪费原则，先解析出数据区要分配的bit数目，再解析出byte,u64要分配的数目
         * 由前到后先大单元分配，再逐渐减小
         * 5.填写元数据
         * 
         */
        int in_heap_alloc(void *&addr, uint32_t size,bool is_longtime=false, bool vaddraquire=true, uint8_t alignment=4);
        /**
         * @brief 释放内存
         * 思路：
         * 1.先检查元信息表项，若魔数被篡改，返回应该触发内核恐慌的返回值，
         * 2.释放元信息表项
         * 3.释放数据
         * 4.出了函数，在内核池管理器内核恐慌，并且报告相关错误信息
         */
        int free(void*ptr);
        /**
         * @brief 重新分配内存
         * 思路：
         * 1.先检查元信息表项，若魔数被篡改，则根据魔数判断是否是关键变量，若是则返回应该触发内核恐慌的返回值，反之则返回另外的返回值
         * 2.先尝试原地拓展
         * 3.若失败则尝试in_heap_alloc，释放原始数据，进行复制
         * 4.实在不行这个堆内存空间无法使用，返回错误码
         * 5.返回管理器的realloc接口的时候根据返回值尝试新堆alloc,释放原堆，或者内核恐慌，并且报告相关错误信息
         * */
        int in_heap_realloc(void*&ptr,uint32_t new_size,bool vaddraquire=true,uint8_t alignment=4);
        uint64_t get_used_bytes_count();
        void count_used_bytes();
        bool is_addr_belong_to_this_hcb(void* addr);
    };
    bool is_able_to_alloc_new_hcb;//是否允许在HCB_ARRAY中分配新的HCB,应该在全局页管理器初始化完成之后调用
    //这个位开启后会优先在cpu专属堆里面操作，再尝试first_linekd_heap
    void enable_new_hcb_alloc();
    class HCB_v2*HCB_ARRAY[HCB_ARRAY_MAX_COUNT];
    HCB_v2 first_linekd_heap;
public:
/**
 * @param vaddraquire true返回虚拟地址，false返回物理地址
 * @param alignment 实际对齐值=2<<alignment,最高支持到13，8kb对齐
 */
   void*kalloc(uint64_t size,bool is_longtime=false,bool vaddraquire=true,uint8_t alignment=4);
   void*realloc(void*ptr,uint64_t size,bool vaddraquire=true,uint8_t alignment=4);//根据表在优先在基地址不变的情况下尝试修改堆对象大小
   //实在不行就创建一个新对象
   void clear(void*ptr);// 主要用于结构体清理内存，new一个结构体后用这个函数根据传入的起始地址查找堆的元信息表项，并把该元信息项对应的内存空间全部写0
   //别用这个清理new之后的对象
   int Init();//真正的初始化，全局对象手动初始化函数
    void kfree(void*ptr);
    kpoolmemmgr_t();
    ~kpoolmemmgr_t();
};
constexpr int INDEX_NOT_EXIST = -100;
extern kpoolmemmgr_t gKpoolmemmgr;
// 全局 new/delete 操作符重载声明
void* operator new(size_t size);
void* operator new(size_t size, bool vaddraquire, uint8_t alignment = 3);
void* operator new[](size_t size);
void* operator new[](size_t size, bool vaddraquire, uint8_t alignment = 3);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, size_t) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, size_t) noexcept;

// 放置 new 操作符
void* operator new(size_t, void* ptr) noexcept;
void* operator new[](size_t, void* ptr) noexcept;