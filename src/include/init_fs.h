#pragma once
#include "BlockDevice.h"
#include <cstdint>
#include "MemoryDisk.h"

typedef uint64_t FileID_t;
enum inode_op_type:int{
    INODE_OP_READ=0,
    INODE_OP_WRITE=1,
    DIR_INODE_OP_ACCESS=2,
    INODE_OP_EXECUTE = 3,
};
enum access_check_result:int{
    ACCESS_PERMITTED=0,
    ACCESS_DENIED=1,
};
struct Visitor_t
{
   uint32_t uid;
   uint32_t gid;
};
struct FileOpenType
{
    uint8_t read:1;
    uint8_t write:1;
    uint8_t append:1;
};
// 定义无效的位图索引，用于返回错误状态
static constexpr uint32_t INVALID_BITMAP_INDEX = static_cast<uint32_t>(-1);

class init_fs_t {//支持卸载后向前移动，不卸载的情况下向后增加分区大小
    block_device_t_v1* phylayer;
    const uint64_t HYPER_CLUSTER_MAGIC = 0xF0F0F0F0F0F0F0F0;
    const uint32_t SUPER_CLUSTER_MAGIT = 0x01;
    // 按照依赖关系重新排列结构体定义
    struct InnerPointer {//一个分区内文件系统指导下的指针
        uint64_t cluster_index;
        uint32_t blk_offset;
    };
    MemoryDiskv1*memdiskv1_blockdevice;//内存盘模式下的文件系统读写基本上直接跳出内存盘之外解析内存手动读写内存盘
    struct BlocksgroupDescriptor;
    struct SuperCluster;
    struct Inode;
    struct FileEntryinDir;
static constexpr uint64_t LEVLE1_INDIRECT_START_CLUSTER_INDEX = 12;
static constexpr uint32_t MAX_FILE_NAME_LEN = 56;
static constexpr uint64_t DIRECT_CLUSTERS =  LEVLE1_INDIRECT_START_CLUSTER_INDEX;
static constexpr uint64_t LEVEL2_INDIRECT_START_CLUSTER_INDEX =  LEVLE1_INDIRECT_START_CLUSTER_INDEX +512;
static constexpr uint64_t LEVEL3_INDIRECT_START_CLUSTER_INDEX =  LEVEL2_INDIRECT_START_CLUSTER_INDEX +512*512;
static constexpr uint64_t LEVEL4_INDIRECT_START_CLUSTER_INDEX =  LEVEL3_INDIRECT_START_CLUSTER_INDEX +512*512*512;
static constexpr uint32_t TRIPLE_INDIRECT_CLUSTERS=512*512*512;
static constexpr uint64_t HYPER_CLUSTER_INDEX=0;
static constexpr uint64_t CLUSTER_DEFAULT_SIZE = 4096;
static constexpr uint64_t DEFAULT_BLOCKS_GROUP_MAX_CLUSTER = 8 * 4096;
static constexpr uint16_t DIR_TYPE = 1;
static constexpr uint16_t FILE_TYPE = 0;
static constexpr uint16_t INDEX_TABLE_TYPE = 0;


// Bitmap type constants
static constexpr uint32_t INODE_BITMAP = 0;
static constexpr uint32_t CLUSTER_BITMAP = 1;
static constexpr uint32_t FILE_PATH_MAX_LEN=8192;
    struct HyperCluster {
        uint64_t magic;                      // 魔数
        uint32_t version;                    // 版本号，暂时为0
        uint32_t block_size;                 // 块大小，必须为2的次幂
        uint16_t cluster_block_count;        // 每个簇包含的块数，2的次幂
        uint16_t hyper_cluster_size;         // HyperCluster结构体大小
        uint32_t cluster_size;               // 簇大小
        uint64_t total_clusters;             // 总簇数
        uint64_t total_blocks;               // 总块数
        uint64_t total_blocks_group_max;
        uint64_t total_blocks_group_valid;
        uint64_t block_group_descriptor_first_cluster; // 块组描述符数组起始簇索引
        uint64_t block_groups_array_size_in_clusters;  // 块组数组占用的簇数
        uint32_t root_block_group_index;     // 根目录所在块组索引
        uint32_t root_directory_inode_index; // 根目录inode索引
        uint32_t max_block_group_size_in_clusters;     // 块组大小上限(以簇为单位)
        uint16_t super_cluster_size;         // SuperCluster结构体大小
        uint16_t inode_size;                 // Inode大小
        uint16_t block_group_descriptor_size; // 块组描述符大小
        uint16_t fileEntryinDir_size;
    } __attribute__((packed));
    
    struct BlocksgroupDescriptor {
        uint64_t first_cluster_index;        // 块组第一个簇索引，同时指向块组的SuperCluster
        uint32_t cluster_count;              // 块组包含的簇数
        uint32_t flags;                      // 块组标志
    } __attribute__((packed));
    
    struct SuperCluster {
        uint64_t magic;                      // 魔数
        uint64_t first_cluster_index;        // 第一个簇索引
        uint32_t version;                    // 版本号
        uint32_t cluster_count;              // 簇数量
        uint32_t clusters_bitmap_first_cluster;      // 簇位图的第一个簇索引
        uint32_t clusters_bitmap_cluster_count;              // 簇位图占用的总共簇数
        uint32_t inodes_count_max;
        uint32_t inodes_array_first_cluster;         // inode数组的第一个簇索引
        uint32_t inodes_bitmap_first_cluster;        // inode位图的第一个簇索引
        uint32_t inodes_array_cluster_count;          // inode数组占用的簇数
        uint32_t inodes_bitmap_cluster_count;         // inode位图占用的簇数
    } __attribute__((packed));
    
    struct file_flags {
        uint64_t dir_or_normal : 8;          // 0: 普通文件, 1: 目录
        uint64_t user_access : 3;            // 用户权限 (0-7)
        uint64_t group_access : 3;           // 组权限 (0-7)
        uint64_t other_access : 3;           // 其他权限 (0-7)
        uint64_t extents_or_indextable : 1;  // 0: 索引表, 1: extents
        uint64_t is_root_inode : 1;        // 是否为根目录inode
    } __attribute__((packed));
    
    struct file_index_table {
        uint64_t direct_pointers[12];        // 直接指针数组
        uint64_t single_indirect_pointer;    // 一级间接指针
        uint64_t double_indirect_pointer;    // 二级间接指针
        uint64_t triple_indirect_pointer;    // 三级间接指针
    } __attribute__((packed));
    
    struct extents_metainf {
        uint64_t first_cluster_index_of_extents_array;        // 第一个簇索引
        uint32_t entries_count;              // extent条目数量
    } __attribute__((packed));
    
    struct FileExtentsEntry_t {
        uint64_t first_cluster_index;
        uint32_t length_in_clusters;
    } __attribute__((packed));
    
    union data_descript {
        file_index_table index_table;
        extents_metainf extents;
    };
    
    struct alignas(256) Inode 
    {
        uint32_t uid;                        // 用户ID
        uint32_t gid;                        // 组ID
        uint32_t parent_dir_inode_index;    // 父目录inode索引
        uint32_t parent_dir_block_group_index;          // 块组索引
        uint64_t file_size;                  // 文件大小
        file_flags flags;                    // 文件标志 
        uint64_t creation_time;              // 创建时间(秒)
        uint64_t last_modification_time;     // 最后修改时间(秒)
        uint64_t last_access_time;            // 最后访问时间(秒)     
        data_descript data_desc;             // 数据描述符
    };

    //传给文件系统的文件路径中.是当前目录，..是上级目录
    //若是根目录则报错，因为可能根据虚拟文件系统层，根目录的父目录在其它文件系统
    /**
 * 根据状态机思想以inode为主附带一些其它变量构造一个类解析路径
 */
    class FilePathAnalyzer{
    public:
    static constexpr int START=0;
    static constexpr int IN_DIR=1;
    static constexpr int IN_FILE=2;
    static constexpr int END=3;
    static constexpr int ERROR_STATE=4;
    static constexpr int ROOT_DIR_NEED_OTHER_FILE_SYSTEM=5;
    static constexpr int ACCESS_DENIED=6;
protected:
        Inode current_inode;
        int state;
        Visitor_t visitor;
        char*path;
        uint32_t pathname_length;
        uint32_t pathname_scanner;//每次扫描完要移动到下一个目录名/文件名开始的位置
        uint32_t current_dir_block_group_index;
        uint32_t current_dir_inode_index;
        public:
        FilePathAnalyzer(Inode root_inode,char*path,uint32_t pathname_length,Visitor_t visitor);
        int the_next(init_fs_t *fs_instance);
        uint32_t get_current_inode_index();
        uint32_t get_current_block_group_index();
        int get_state();
        Inode get_current_inode();
    };
    friend  class FilePathAnalyzer;
    struct FileEntryinDir {
        uint8_t filename[MAX_FILE_NAME_LEN];                // 文件名，使用uint8_t避免平台相关的符号扩展问题
        //显然 最多55个字节
        uint32_t inode_index;                // inode索引
        uint32_t block_group_index;          // 块组索引
    } __attribute__((packed));  
    class FileExtents_in_clusterScanner
    {
        FileExtentsEntry_t* extents_array;
        uint32_t extents_count;
        uint32_t extents_entry_scanner;
        uint32_t skipped_clusters_count;
        uint32_t in_entry_offset;
        public:
        int the_next();
        FileExtents_in_clusterScanner(FileExtentsEntry_t* extents_array,uint32_t extents_count);
        uint32_t convert_to_logical_cluster_index();
        uint64_t get_phyclsidx();
    };
    
        //此函数返回void*指针，指针处获得了原始的对应块组的对应种类的位图的原始数据
    void* get_bitmap_base(
        uint64_t block_group_index,
        uint32_t bitmap_type
    );
    
    uint32_t search_avaliable_inode_bitmap_bit(//未完成所有特性
        uint64_t&block_group_index//传入0则扫描所有块组，非0则只在特定块组中搜索
    );
    uint32_t search_avaliable_cluster_bitmap_bit(//未完成所有特性
        uint64_t&block_group_index//传入0则扫描所有块组，非0则只在特定块组中搜索
    );
    /**
     * @brief 搜索指定块组中连续的可用的簇位图位
     * 
     * @param aquired_avaliable_clusters_count 获取的簇数
     * @param result_base 获取的起始簇索引
     * @param block_group_index 块组索引
     * @return int 0: 成功, 非0: 失败

     */
    int search_avaliable_cluster_bitmap_bits(
        uint64_t aquired_avaliable_clusters_count,
        uint64_t&result_base,
        uint64_t&block_group_index
    );
    int FileExtentsEntry_merger(FileExtentsEntry_t *old_buff, FileExtentsEntry_t *new_buff, uint64_t &entryies_count);
    int clusters_bitmap_alloc(
        uint64_t alloc_clusters_count,
        FileExtentsEntry_t *extents_entry,
        uint64_t&entry_count);
    SuperCluster*get_supercluster(uint32_t block_group_index);
    int set_inode_bitmap_bit(
        uint64_t block_group_index,
        uint64_t inode_index,
        bool value);
    int set_inode_bitmap_bits(
        uint64_t block_group_index,
        uint64_t base_index,
        uint64_t bit_count,
        bool value
    );
    int logical_offset_toindex(
            FileExtentsEntry_t* extents_array,
            uint64_t extents_count,
            uint64_t logical_offset ,
            uint64_t &result_index
    );
    bool get_inode_bitmap_bit(
        uint64_t block_group_index,
        uint64_t inode_index,
        int &status
    );
    int set_cluster_bitmap_bit(
        uint64_t block_group_index,
        uint64_t cluster_idx,
        bool value
    );
    int set_cluster_bitmap_bits(
        uint64_t block_group_index,
        uint64_t base_index,
        uint64_t bit_count,
        bool value
    );
    /*之前的接口是在特定块组的位图下的接口，需要在特定块组特定偏移量以及范围设置
    全局接口是在全局位图下的接口，不需要知道块组，全局的簇的索引解析出对应块组，然后
    使用对应块组的接口去设置
    */
        int global_set_cluster_bitmap_bit(
        uint64_t cluster_idx,
        bool value
    );
    int global_set_cluster_bitmap_bits(
        uint64_t base_index,
        uint64_t bit_count,
        bool value
    );
    bool get_cluster_bitmap_bit(
        uint64_t block_group_index,
        uint64_t cluster_idx,
        int &status
    );
    /**
     *内部接口处理inode大小改变，
     *会根据inode指向的文件大小的改变去修改相应bitmap
     */
    int resize_inode(Inode& the_inode, uint64_t new_size);//未完成所有特性
    int Increase_inode_allocated_clusters(Inode& the_inode, uint64_t Increase_clusters_count);
    int idxtbmode_set_inode_lcluster_phyclsidx(Inode& the_inode, uint64_t lcluster_index, uint64_t phyclsidx);
    int Decrease_inode_allocated_clusters(Inode& the_inode, uint64_t Decrease_clusters_count);//未完成所有特性
    int idxtbmod_delete_lcluster(Inode& the_inode, uint64_t lcluster_index);
    int write_inode(
        uint64_t block_group_index,
        uint64_t inode_index,
        Inode* inode
    );  
    int get_inode(
        uint64_t block_group_index,
        uint64_t inode_index,
        Inode& inode
    );
    int inode_content_write(//从特定偏移量上覆写，若超出大小会报错
        Inode the_inode,
        uint64_t stream_base_offset,
        uint64_t size,
        uint8_t*buffer
    );
        int inode_level1_idiwrite(//从一级索引表上读取
        uint64_t rootClutser_of_lv1_index,
        uint64_t fsize,
        uint64_t start_cluster_index_of_datastream,
        uint64_t end_cluster_index_of_datastream,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
        int inode_level2_idiwrite(//从一级索引表上读取
        uint64_t rootClutser_of_lv1_index,
        uint64_t fsize,
        uint64_t start_cluster_index_of_datastream,
        uint64_t end_cluster_index_of_datastream,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
        int inode_level3_idiwrite(//从一级索引表上读取
        uint64_t rootClutser_of_lv3_index,
        uint64_t fsize,
        uint64_t datastream_start_logical_clst,
        uint64_t datastream_end_logical_clst,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
    int inode_content_read(//从特定偏移量上读取，若超出大小会报错
        Inode the_inode,
        uint64_t stream_base_offset,
        uint64_t size,
        uint8_t*buffer
    );
    int inode_level1_idiread(//从一级索引表上读取
        uint64_t rootClutser_of_lv1_index,
        uint64_t fsize,
        uint64_t start_cluster_index_of_datastream,
        uint64_t end_cluster_index_of_datastream,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
        int inode_level2_idiread(//从一级索引表上读取
        uint64_t rootClutser_of_lv1_index,
        uint64_t fsize,
        uint64_t start_cluster_index_of_datastream,
        uint64_t end_cluster_index_of_datastream,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
        int inode_level3_idiread(//从一级索引表上读取
        uint64_t rootClutser_of_lv3_index,
        uint64_t fsize,
        uint64_t start_cluster_index_of_datastream,
        uint64_t end_cluster_index_of_datastream,//从start到end_cluster_index-1引索的簇读取
        uint8_t*buffer
    );
    int filecluster_to_fscluster_in_idxtb(
        Inode the_inode,
        uint64_t file_cluster_index,
        uint64_t&cluster_index
    );
    int filecluster_to_fscluster_in_extents(
        Inode the_inode,
        uint64_t file_cluster_index,
        uint64_t&cluster_index,
        FileExtentsEntry_t& extents_entry
    );
    int dir_content_search_by_name(
        Inode be_searched_dir_inode,
        char* filename,
        FileEntryinDir& result_entry    
    );
    int dir_content_append(
        Inode&dir_inode,
        FileEntryinDir append_entry    
    );
    int dir_content_delete_by_name(
        Inode&dir_inode,
        FileEntryinDir&del_entry
    );
    access_check_result access_check(
        Visitor_t visitor,
        Inode the_inode,
        inode_op_type op_type
    );
    int path_analyze(char*path,Inode& inode,Visitor_t visitor);
    HyperCluster*fs_metainf;
    SuperCluster*SuperClusterArray;
    bool is_valid;
    bool is_memdiskv1;
    Inode opened_file_inode_table[4096];
public: 

    init_fs_t(block_device_t_v1* phylayer);
    int Mkfs();//格式化
    int CreateFile(Visitor_t executor,char* relative_path);
    int DeleteFile(Visitor_t executor,char* relative_path);
    int mvFile(Visitor_t executor, char* oldname, char* newname);
    int cpFile(Visitor_t executor, char* oldname, char* newname);
    int CreateDir(Visitor_t executor, char* relative_path);
    int DeleteDir(Visitor_t executor, char* relative_path);//删除目录,必须是空目录
    int WriteFile(Visitor_t executor, FileID_t in_fs_id, void* src, uint64_t size);
    int ReadFile(Visitor_t executor, FileID_t in_fs_id, void* dest, uint64_t size);
    int OpenFile(Visitor_t executor, char* relative_path, FileID_t& in_fs_id);
    /**
     * 这是个测试四个增删函数的函数，以及其它相关用户空间接口的函数，纯用户空间函数，不在内核态编译，可以使用各种用户态工具
     */
    int Create_del_and_Inner_surface_test();
    int CloseFile(Visitor_t executor, FileID_t in_fs_id);
    ~init_fs_t();
};