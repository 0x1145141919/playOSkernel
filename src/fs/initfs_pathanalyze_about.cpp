#include "init_fs.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#ifdef USER_MODE
#include <cstdio>
#endif
/**
 * 根据状态机思想以inode为主附带一些其它3变量构造一个类解析路径
 * 有缺陷，没有保存当前inode的块组1号，块组内引索之类的信息
 */
int init_fs_t::path_analyze(char *path, Inode &inode,Visitor_t visitor)
{   
    Inode root_inode;
    get_inode(fs_metainf->root_block_group_index,fs_metainf->root_directory_inode_index,root_inode);
    FilePathAnalyzer path_analyzer(root_inode, path, strlen(path),visitor);//还暂时没有提供根inode的参数
    while (true)
    {
        int result = path_analyzer.the_next(this);
        if(result==path_analyzer.ERROR_STATE)
        {
            return OS_FILE_NOT_FOUND;
        }
        else if(result==path_analyzer.END)
        {
            inode=path_analyzer.get_current_inode();
            return OS_SUCCESS;
        }else if(result==path_analyzer.ROOT_DIR_NEED_OTHER_FILE_SYSTEM)
        {
            return OS_NOT_SUPPORT;
        }

    }
    
}
int init_fs_t::FileExtents_in_clusterScanner::the_next()
{
    if (extents_entry_scanner >= extents_count) {
        return -1; // No more extents
    }

    FileExtentsEntry_t& current_extent = extents_array[extents_entry_scanner];

    if (in_entry_offset < current_extent.length_in_clusters) {
        // Still within the current extent
        in_entry_offset++;
        return 0; // Successfully moved to the next cluster
    } else {
        // Move to the next extent
        extents_entry_scanner++;
        in_entry_offset = 0;
        skipped_clusters_count += current_extent.length_in_clusters;
        if (extents_entry_scanner >= extents_count) {
            return -1; // No more extents
        } else {
            // Move to the first cluster of the new extent
            in_entry_offset++;
            return 0; // Successfully moved to the next cluster
        }
    }
}
init_fs_t::FilePathAnalyzer::FilePathAnalyzer(
    Inode root_inode, 
    char *path, uint32_t pathname_length,
    Visitor_t visitor
)//todo
{
    this->current_inode = root_inode;
    this->path = path;
    this->pathname_length = pathname_length;
    this->visitor=visitor;
    this->current_dir_block_group_index=0;
    this->current_dir_inode_index=0;//默认写死为0,默认文件系统根inode
    this->state=START;
    pathname_scanner = 0;
}

int init_fs_t::FilePathAnalyzer::the_next(init_fs_t *fs_instance)
{
    switch (state)
    {
    case IN_DIR:
    {
        uint32_t to_search_inode_name_startidx = pathname_scanner;
        
        // 检查是否已到达路径末尾
        if (pathname_scanner >= pathname_length)
        {
            state = END;
            return state;
        }
        
        // 查找下一个目录分隔符
        while (pathname_scanner < pathname_length && path[pathname_scanner] != '/')pathname_scanner++;
        
        // 提取目录/文件名
        uint32_t name_len = pathname_scanner - to_search_inode_name_startidx;
        if (name_len >= fs_instance->MAX_FILE_NAME_LEN)
        {
            state = ERROR_STATE;
            return state;
        }
        
        char inode_name[fs_instance->MAX_FILE_NAME_LEN];
        ksystemramcpy(
            path + to_search_inode_name_startidx,
            inode_name,
            name_len
        );
        inode_name[name_len] = '\0';  // 正确设置终止符
        
        // 检查空文件名
        if (inode_name[0] == '\0')
        {
            state = ERROR_STATE;
            return state;
        }
        access_check_result cresult=fs_instance->access_check(visitor,current_inode,DIR_INODE_OP_ACCESS);
        if(cresult==ACCESS_DENIED){
            state=ACCESS_DENIED;
            break;
        }
        // 处理特殊目录 . 和 ..
        if (inode_name[0] == '.')
        {
            if (inode_name[1] == '\0')
            {
                // 当前目录：继续处理
                if (pathname_scanner < pathname_length && path[pathname_scanner] == '/')
                {
                    pathname_scanner++; // 跳过 '/'
                }
                return state;
            }
            else if (inode_name[1] == '.' && inode_name[2] == '\0')
            {
                // 上级目录
                if (current_inode.flags.is_root_inode)
                {
                    state = ERROR_STATE;
                    return state;
                }
                else
                {
                    Inode parent_inode;
                    current_dir_block_group_index = current_inode.parent_dir_block_group_index;
                    current_dir_inode_index = current_inode.parent_dir_inode_index;
                    fs_instance->get_inode(
                        current_inode.parent_dir_block_group_index,
                        current_inode.parent_dir_inode_index,
                        parent_inode
                    );
                    current_inode = parent_inode;
                    
                    if (pathname_scanner < pathname_length && path[pathname_scanner] == '/')
                    {
                        pathname_scanner++; // 跳过 '/'
                    }
                    else
                    {
                        // 路径结束，根据当前inode类型设置状态
                        state = (current_inode.flags.dir_or_normal == DIR_TYPE) ? IN_DIR : IN_FILE;
                    }
                    return state;
                }
            }
        }
        
        // 在目录中查找对应的文件/目录项
        uint32_t dir_entries_count = current_inode.file_size / sizeof(FileEntryinDir);
        FileEntryinDir* index_content = new FileEntryinDir[dir_entries_count];
        
        if (!fs_instance->inode_content_read(
            current_inode,
            0,
            current_inode.file_size,
            (uint8_t*)index_content
        ))
        {
            delete[] index_content;
            state = ERROR_STATE;
            return state;
        }
        
        // 遍历目录项查找匹配的文件名
        bool found = false;
        for (uint32_t i = 0; i < dir_entries_count; i++)
        {
            if (strcmp((char*)index_content[i].filename, inode_name,fs_instance->MAX_FILE_NAME_LEN) == 0)//这里长度不要直接最大长度，应该改成文件长度
            {
                Inode next_inode;
                int status=fs_instance->get_inode(
                    index_content[i].block_group_index,
                    index_content[i].inode_index,
                    next_inode);
                if (status!=OS_SUCCESS)
                {
                    delete[] index_content;
                    state = ERROR_STATE;
                    return state;
                }
                current_dir_block_group_index = index_content[i].block_group_index;
                current_dir_inode_index = index_content[i].inode_index;
                current_inode = next_inode;
                found = true;    
                if (pathname_scanner < pathname_length && path[pathname_scanner] == '/')
                {
                    pathname_scanner++; // 跳过 '/'
                }else{
                    if(pathname_scanner>=pathname_length)
                    {
                        state=END;
                        return state;
                    }
                }
                state = (current_inode.flags.dir_or_normal == DIR_TYPE) ? IN_DIR : IN_FILE;
                break;
            }
        }
        
        delete[] index_content;
        
        if (!found)
        {
            state = ERROR_STATE;
        }
        return state;
    }
    
    case IN_FILE:
        if(pathname_scanner!=pathname_length){
            state = ERROR_STATE;
            return state;
        }
        state = END;
        return state;
        
    case START:
    {
        if (pathname_scanner != 0)
        {
            state = ERROR_STATE;
            return state;
        }
        
        // 绝对路径以'/'开头，跳过它
        if (pathname_length > 0 && path[0] == '/')
        {
            pathname_scanner = 1;
        }
        else
        {
            pathname_scanner = 0;
        }
        
        state = IN_DIR;
        // 递归调用处理第一个路径组件
        return the_next(fs_instance);
    }
    
    case END:
    case ROOT_DIR_NEED_OTHER_FILE_SYSTEM:
    case ERROR_STATE:
    case ACCESS_DENIED:
        return state;
        
    default:
        state = ERROR_STATE;
        return state;
    }
    return state;
}

uint32_t init_fs_t::FilePathAnalyzer::get_current_inode_index()
{
    return current_dir_inode_index;
}

uint32_t init_fs_t::FilePathAnalyzer::get_current_block_group_index()
{
    return current_dir_block_group_index;
}

init_fs_t::Inode init_fs_t::FilePathAnalyzer::get_current_inode()
{
    return current_inode;
}
