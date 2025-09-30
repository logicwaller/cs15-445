#!/bin/bash

# compare_sql.sh - 比较两个SQL文件的输出结果

DB="olympics-cmudb2024.db"
FILE1="$1"
FILE2="$2"

if [ $# -ne 2 ]; then
    echo "用法: $0 <sql文件1> <sql文件2>"
    echo "示例: $0 query1.sql query2.sql"
    exit 1
fi

if [ ! -f "$DB" ]; then
    echo "错误: 数据库文件 $DB 不存在"
    exit 1
fi

if [ ! -f "$FILE1" ] || [ ! -f "$FILE2" ]; then
    echo "错误: SQL文件不存在"
    exit 1
fi

echo "比较 $FILE1 和 $FILE2 的输出结果..."
echo "========================================"

# 执行SQL并保存输出
duckdb -header -column "$DB" < "$FILE1" > output1.txt
duckdb -header -column "$DB" < "$FILE2" > output2.txt

# 比较结果
if diff -q output1.txt output2.txt > /dev/null; then
    echo "✅ 两个SQL文件输出一致"
    EXIT_CODE=0
else
    echo "❌ 两个SQL文件输出不一致"
    echo "差异详情:"
    diff -u output1.txt output2.txt
    EXIT_CODE=1
fi

# 清理临时文件
rm output1.txt output2.txt

echo "========================================"
exit $EXIT_CODE