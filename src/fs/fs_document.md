文件系统元数据布局文档

概述

本文档描述基于 init_fs_t::Mkfs() 函数实现的文件系统元数据布局。该文件系统采用分层结构，支持动态分区大小调整。

整体布局结构


+-------------------+-------------------+-------------------+-------------------+
|    HyperCluster   | 块组描述符数组    |     块组0        |     块组1        | ...
|   (超级块)        | (Block Group      |                  |                  |
|   簇索引: 0       | Descriptors)      |                  |                  |
+-------------------+-------------------+-------------------+-------------------+


1. HyperCluster（超级块）

位置信息

• 起始簇索引: 0

• 占用簇数: 1

• 块索引: 0 * cluster_block_count

数据结构

struct HyperCluster {
    uint64_t magic;                      // 魔数: 0xF0F0F0F0F0F0F0F0
    uint32_t version;                    // 版本号
    uint32_t block_size;                 // 块大小（字节）
    uint16_t cluster_block_count;        // 每个簇包含的块数
    uint16_t hyper_cluster_size;         // HyperCluster结构体大小
    uint32_t cluster_size;               // 簇大小（字节）
    uint64_t total_clusters;             // 总簇数
    uint64_t total_blocks;               // 总块数
    uint64_t total_blocks_group_max;     // 最大块组数
    uint64_t total_blocks_group_valid;   // 有效块组数
    uint64_t clusters_per_group;         // 每个块组包含的簇数
    uint64_t block_group_descriptor_first_cluster; // 块组描述符起始簇
    uint64_t block_groups_array_size_in_clusters;   // 块组数组占用簇数
    uint32_t root_block_group_index;    // 根目录块组索引
    uint32_t root_directory_inode_index; // 根目录inode索引
    uint32_t max_block_group_size_in_clusters; // 块组大小上限
    uint16_t super_cluster_size;        // SuperCluster大小
    uint16_t inode_size;                 // Inode大小
    uint16_t block_group_descriptor_size; // 块组描述符大小
};


2. 块组描述符数组

位置信息

• 起始簇索引: block_group_descriptor_first_cluster（默认1）

• 占用簇数: block_groups_array_size_in_clusters

• 计算公式: (total_blocks_group_valid * sizeof(BlocksgroupDescriptor) + cluster_size - 1) / cluster_size

数据结构

struct BlocksgroupDescriptor {
    uint64_t first_cluster_index;        // 块组第一个簇索引
    uint32_t cluster_count;              // 块组包含的簇数
    uint32_t flags;                      // 块组标志
};


3. 块组（Block Group）内部结构

每个块组包含以下部分，按顺序排列：

3.1 SuperCluster（块组超级块）


+-------------------+
|   SuperCluster    |
|   (块组元信息)    |
+-------------------+


数据结构:
struct SuperCluster {
    uint64_t magic;                      // 魔数: 0x01
    uint64_t first_cluster_index;        // 第一个簇索引
    uint32_t version;                    // 版本号
    uint32_t cluster_count;              // 簇数量
    uint32_t clusters_bitmap_first_cluster;      // 簇位图的第一个簇索引
    uint32_t inodes_count_max;           // 最大inode数
    uint32_t inodes_array_first_cluster;         // inode数组的第一个簇索引
    uint32_t inodes_bitmap_first_cluster;        // inode位图的第一个簇索引
    uint8_t inodes_array_cluster_count;          // inode数组占用的簇数
    uint8_t clusters_bitmap_cluster_count;       // 簇位图占用的簇数
    uint8_t inodes_bitmap_cluster_count;         // inode位图占用的簇数
};


3.2 Inode数组


+-------------------+
|    Inode数组      |
|   (文件元数据)    |
+-------------------+


位置计算:
• 起始簇索引: inodes_array_first_cluster = scanner_index + 1

• 占用簇数: (inodes_count_max * inode_size + cluster_size - 1) / cluster_size

3.3 Inode位图


+-------------------+
|   Inode位图       |
| (inode分配状态)  |
+-------------------+


位置计算:
• 起始簇索引: inodes_bitmap_first_cluster = scanner_index + 1 + inodes_array_cluster_count

• 占用簇数: ((inodes_count_max + 7) / 8 + cluster_size - 1) / cluster_size

3.4 簇位图


+-------------------+
|   簇位图          |
| (数据簇分配状态)  |
+-------------------+


位置计算:
• 起始簇索引: clusters_bitmap_first_cluster = inodes_bitmap_first_cluster + inodes_bitmap_cluster_count

• 占用簇数: ((cluster_count + 7) / 8 + cluster_size - 1) / cluster_size

3.5 数据簇区域


+-------------------+
|     数据簇        |
|   (文件数据存储)  |
+-------------------+


位置: 簇位图之后的所有剩余空间

4. 关键计算参数

4.1 默认值

constexpr uint64_t CLUSTER_DEFAULT_SIZE = 4096;
constexpr uint64_t DEFAULT_BLOCKS_GROUP_MAX_CLUSTER = 8 * 4096;  // 32MB块组


4.2 动态计算

// 总簇数
total_clusters = total_blocks / cluster_block_count;

// 块组数量
total_blocks_group_valid = (total_clusters + DEFAULT_BLOCKS_GROUP_MAX_CLUSTER - 1) / DEFAULT_BLOCKS_GROUP_MAX_CLUSTER;

// 每个块组实际簇数
cluster_count = min(DEFAULT_BLOCKS_GROUP_MAX_CLUSTER, remaining_clusters);


5. 根目录初始化

5.1 根目录Inode

• 块组索引: 0

• Inode索引: 0

• 类型: 目录 (DIR_TYPE = 1)

• 权限: 777 (用户/组/其他都有读写执行权限)

5.2 根目录数据簇

• 通过搜索块组0的簇位图找到第一个空闲簇

• 设置对应的簇位图位为已使用

• 设置inode位图位为已使用

6. 位图管理

6.1 位图结构

• 使用512位的位图块 (bitset512_t)

• 每个位代表一个inode或数据簇的分配状态

• true表示已分配，false表示空闲

6.2 位图操作函数

// 搜索可用位
search_avaliable_inode_bitmap_bit()
search_avaliable_cluster_bitmap_bit()

// 设置位状态
set_inode_bitmap_bit(), set_cluster_bitmap_bit()
set_inode_bitmap_bits(), set_cluster_bitmap_bits()

// 获取位状态
get_inode_bitmap_bit(), get_cluster_bitmap_bit()


7. 扩展特性

7.1 动态分区调整

• 支持不卸载的情况下向后增加分区大小

• 块组描述符数组设计为可扩展结构

7.2 权限管理

• 支持用户/组/其他三级权限控制

• 每个文件/目录有独立的权限设置

7.3 时间戳

• 创建时间、修改时间、访问时间记录

• 支持文件系统级别的时序管理

总结

该文件系统采用经典的分层块组设计，具有良好的可扩展性和容错性。元数据布局清晰，支持大容量存储和高效的空间管理。