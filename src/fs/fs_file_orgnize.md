# 文件系统中的文件数据存储方式与组织形式

本文件系统（以下简称 FS）采用基于 Inode 的结构来管理文件元数据和数据块分配，支持两种主要的数据组织形式：**索引表模式（Index Table Mode）** 和 **Extents 模式**。这些形式通过 Inode 中的 `flags.extents_or_indextable` 字段来区分（0 表示索引表模式，1 表示 Extents 模式）。文件数据以 **簇（Cluster）** 为基本分配单位，每个簇的大小固定（默认 4096 字节），簇由多个块（Block）组成。文件数据的存储强调逻辑簇（文件内部连续的簇索引）与物理簇（磁盘上的实际簇位置）的映射。

以下描述结合 `inode_content_read` 函数的逻辑，解释文件数据的存储方式、组织形式及其读取流程。`inode_content_read` 函数从指定偏移量读取数据，展示了如何根据模式处理簇索引转换和数据访问。

## 1. 基本概念
- **簇（Cluster）**：数据分配的最小单位。簇大小由 `HyperCluster.cluster_size` 定义，默认 4096 字节。每个簇对应多个块（`cluster_block_count = cluster_size / block_size`）。
- **逻辑簇索引（File Cluster Index）**：文件内部的簇序号，从 0 开始，表示文件数据的逻辑顺序（例如，文件第 N 个簇）。
- **物理簇索引（FS Cluster Index）**：磁盘上的实际簇位置，用于底层读写操作。
- **Inode 数据描述符（data_desc）**：Inode 中的联合体（union），根据模式存储索引表或 Extents 元信息。
- **文件大小边界**：读取时应检查 `stream_base_offset + size <= inode.file_size`，超出返回错误（当前代码中未显式实现，但逻辑上必需）。
- **内存盘 vs. 普通块设备**：系统支持内存盘模式（直接虚拟地址访问）和普通模式（通过 `phylayer->read` 接口）。读取逻辑类似，但内存盘使用 `ksystemramcpy` 和 `get_vaddr`。

## 2. 索引表模式（Index Table Mode）
在这种模式下（`flags.extents_or_indextable == 0`），文件数据通过多级指针表组织，类似于传统 Unix 文件系统（如 ext2）。这允许灵活分配非连续簇，适合随机访问和碎片化文件。Inode 的 `data_desc.index_table` 包含：
- **直接指针（Direct Pointers）**：数组 `direct_pointers[12]`，每个元素指向一个物理簇。覆盖逻辑簇 0~11（LEVLE1_INDIRECT_START_CLUSTER_INDEX = 12）。
- **单级间接指针（Single Indirect Pointer）**：指向一个簇，该簇包含指针数组（每个指针 8 字节，簇大小 / 8 = MaxClusterEntryCount，通常 512）。覆盖逻辑簇 12~523（LEVEL2_INDIRECT_START_CLUSTER_INDEX = 12 + 512）。
- **双级间接指针（Double Indirect Pointer）**：指向一个簇（一级表），该簇指向二级表簇，每个二级表指向数据簇。覆盖逻辑簇 524~262667（LEVEL3_INDIRECT_START_CLUSTER_INDEX = 524 + 512*512）。
- **三级间接指针（Triple Indirect Pointer）**：类似，指向三级表。覆盖逻辑簇 262668~134742779（LEVEL4_INDIRECT_START_CLUSTER_INDEX = 262668 + 512*512*512）。

### 组织形式
- **小文件（< 12 簇）**：直接使用直接指针。每个指针直接映射逻辑簇到物理簇，无需额外开销。
- **中等文件**：使用单级/双级间接指针。通过指针表嵌套扩展，支持更多簇。
- **大文件**：三级间接指针进一步扩展。理论上支持极大文件，但当前代码未实现四级（超出 LEVEL4 返回 OS_INVALID_PARAMETER）。
- **分配策略**：簇可非连续，适合碎片化磁盘。Inode 的 `allocated_clusters` 记录已分配簇数。
- **空洞支持**：如果指针为 0，表示未分配簇（读取返回 OS_FILE_NOT_FOUND），支持稀疏文件。

### 读取逻辑（基于 inode_content_read）
1. **计算逻辑簇**：从偏移 `stream_base_offset` 计算起始逻辑簇 `start_cluster_index = offset / cluster_size`，簇内偏移 `in_cluster_start_offset = offset % cluster_size`，结束逻辑簇 `end_cluster_index = (offset + size) / cluster_size`，结束簇内偏移 `in_cluster_end_offset = (offset + size) % cluster_size`。
2. **起始簇部分读取**：使用 `filecluster_to_fscluster_in_idxtb` 将起始逻辑簇转换为物理簇，读取从簇内偏移开始的部分数据（大小 `min(cluster_size - in_cluster_start_offset, size)`）。
3. **中间簇全簇读取**：循环处理逻辑簇（scanner 从 start+1 到 end-1）：
   - 如果在直接指针范围：直接从 `direct_pointers[scanner]` 获取物理簇，读取全簇。
   - 如果在单级间接范围：调用 `inode_level1_idiread(single_indirect_pointer, ...)` 批量读取该级别的簇。
   - 双级/三级类似，使用相应指针和 `inode_level2_idiread` / `inode_level3_idiread`。
4. **结束簇部分读取**：如果 `in_cluster_end_offset > 0`，转换结束逻辑簇到物理簇，读取前 `in_cluster_end_offset` 字节。
5. **转换函数（filecluster_to_fscluster_in_idxtb）**：
   - 直接范围：直接取指针。
   - 单级：加载单级表簇，计算偏移取指针。
   - 双级/三级：嵌套加载表簇，计算多级偏移（>>9 为 512 移位，>>18 为 512*512）。
   - 内存盘模式直接用虚拟地址，非内存盘分配/读取/释放数组。

这种组织高效处理小文件（直接访问），但多级间接可能导致读取开销（多次磁盘访问）。

## 3. Extents 模式
在这种模式下（`flags.extents_or_indextable == 1`），文件数据使用连续段（Extents）描述，类似于 ext4 文件系统，适合大连续文件，减少元数据开销。Inode 的 `data_desc.extents` 包含：
- **Extents 数组起始簇（first_cluster_index_of_extents_array）**：指向存储 Extents 条目的簇。
- **条目数量（entries_count）**：Extents 数组的大小。

每个 `FileExtentsEntry_t` 包含：
- **物理起始簇（first_cluster_index）**：该段的物理簇起始位置。
- **簇长度（length_in_clusters）**：该段连续簇数。

### 组织形式
- **连续段映射**：每个 Extent 描述一个逻辑连续的簇范围映射到物理连续簇。Extents 数组按逻辑顺序存储（从逻辑簇 0 开始累加）。
- **小/大文件**：统一使用 Extents，高效存储大文件（一个 Extent 可覆盖数 GB）。当前实现假设无空洞（逻辑簇连续），如果有空洞，转换失败。
- **分配策略**：优先分配连续簇，减少碎片。`allocated_clusters` 记录总分配。
- **空洞支持**：当前代码不支持（Extents 累加逻辑簇无间隙），若需支持，可在 `FileExtentsEntry_t` 添加逻辑起始簇字段。

### 读取逻辑（基于 inode_content_read）
1. **计算逻辑簇**：同索引表模式。
2. **起始簇部分读取**：使用 `filecluster_to_fscluster_in_extents` 转换起始逻辑簇到物理簇，读取部分数据。
3. **中间簇全簇读取**：循环处理逻辑簇（scanner 从 start+1 到 end-1），每次转换到物理簇，读取全簇（当前代码中截断，但逻辑上需添加）。
4. **结束簇部分读取**：如果 `in_cluster_end_offset > 0`，转换结束逻辑簇，读取部分。
5. **转换函数（filecluster_to_fscluster_in_extents）**：
   - 加载 Extents 数组（内存盘直接虚拟地址，非内存盘读取到新数组）。
   - 累加逻辑簇（cluster_scanner += length_in_clusters），查找包含目标逻辑簇的 Extent。
   - 计算物理簇：`physical = logical - cluster_scanner + first_cluster_index`（假设连续）。
   - 释放数组。

这种组织减少了指针嵌套，提高了大文件读取效率。

## 4. 比较与适用场景
- **索引表模式**：灵活，适合小文件和随机分配。缺点：多级间接增加读取延迟。
- **Extents 模式**：高效，适合大连续文件。缺点：碎片化时可能需更多 Extents。
- **切换**：由文件创建/扩展时决定，Inode 标志固定。
- **通用读取优化**：`inode_content_read` 统一处理部分/全簇读取，支持内存盘（直接拷贝）和普通设备（块读取）。未来可添加缓存以减少重复转换。

此设计平衡了灵活性和性能，适用于嵌入式或通用存储系统。