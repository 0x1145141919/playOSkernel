#!/bin/bash

# 数据库配置
DB_USER="root"
DB_PASS="trEE(3)>>g(64);"
DB_NAME="cmake_pj_properties"
BASE_PATH_ID="1"

# 获取基础目录
BASE_DIR=$(mariadb -u $DB_USER -p$DB_PASS $DB_NAME -s -N -e "SELECT base_path_content FROM base_path_tb WHERE base_id = $BASE_PATH_ID")

if [ ! -d "$BASE_DIR" ]; then
    echo "错误: 目录不存在: $BASE_DIR"
    exit 1
fi

echo "=== 开始全量文件扫描 ==="
echo "基础目录: $BASE_DIR"
echo "基础路径ID: $BASE_PATH_ID"
echo ""

# 清空该基础路径下的现有记录（可选）
echo "清空现有记录..."
mariadb -u $DB_USER -p$DB_PASS $DB_NAME -e "DELETE FROM pjfiles WHERE base_path_id = $BASE_PATH_ID"

# 全量插入所有文件
echo "开始插入文件..."
file_count=0
error_count=0

find "$BASE_DIR" -type f | while read -r file_path; do
    # 转义单引号
    safe_path=$(echo "$file_path" | sed "s/'/''/g")
    # 再去除基础路径
    relative_path=$(echo "$safe_path" | sed "s|$BASE_DIR||g")
    
    # 插入数据库
    result=$(mariadb -u $DB_USER -p$DB_PASS $DB_NAME -e "
    INSERT INTO pjfiles (file_id,file_path, base_path_id) 
    VALUES ('$file_count','$relative_path', $BASE_PATH_ID)" 2>&1)
    
    if [ $? -eq 0 ]; then
        echo "✓ [$((++file_count))] $(basename "$file_path")"
    else
        echo "✗ 错误: $(basename "$file_path")"
        echo "   详情: $result"
        ((error_count++))
    fi
done

echo ""
echo "=== 全量插入完成 ==="
echo "成功插入: $file_count 个文件"
echo "插入失败: $error_count 个文件"
