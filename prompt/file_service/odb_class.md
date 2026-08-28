# odb 数据库映射类实现

**身份** : 你是一个资深的 C++ 后端开发工程师 , 对微服务架构有深入了解 , 熟悉 odb 数据库映射类的编写 , 以及 odb 工具使用

任务 : 帮我给文件子服务编写 odb 数据库表映射类, 完成文件信息表和 WorkSheet 表的 C++ 类映射 , 并完成映射类的编译

## 1. 文件信息表设计

用户上传的 Excel 元信息将存储在 MySQL 文件信息表中 , FastDFS 中存储的是 Excel 的二进制文件内容
文件信息表包含字段如下 : 

| 字段名 | 类型 | 约束 | 主键 | 说明|
| --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | NOT NULL | PRIMARY KEY AUTO_INCREMENT | | 文件信息表主键 |
| file_id | VARCHAR(32) | NOT NULL | UNIQUE | Excel 文件唯一标识符 |
| file_name | VARCHAR(64) | NOT NULL | | Excel 文件名 |
| file_extension | VARCHAR(16) | NOT NULL | | Excel 文件扩展名 |
| file_size | BIGINT UNSIGNED | NOT NULL | | Excel 文件大小 |
| file_upload_time | BIGINT UNSIGNED | NOT NULL | | Excel 文件上传时间 |
| fastdfs_file_id | VARCHAR(64) | NOT NULL | | Excel 文件在 FastDFS 中的文件 ID |
| user_id | VARCHAR(32) | NOT NULL | | 文件所属用户 ID |
| session_id | VARCHAR(32) | NOT NULL | | 文件所属会话 ID |

说明 : 
1. 通过 file_id , 用户可以获取表中指定文件的文件元信息
2. 数据库表明为 tbl_file_info
3. 创建数据库表的 SQL 语句 , 通过 odb 工具生成

## 2. WorkSheet 表设计

Excel 解析之后会将每个 WorkSheet 的元信息存储在 MySQL WorkSheet 表中 , 表包含字段如下 : 
| 字段名 | 类型 | 约束 | 主键 | 说明|
| --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | NOT NULL | PRIMARY KEY AUTO_INCREMENT | | WorkSheet 表主键 |
| file_id | VARCHAR(32) | NOT NULL | INDEX | WorkSheet 所属文件 ID |
| worksheet_name | VARCHAR(64) | NOT NULL | | WorkSheet 名称 |
| table_name | VARCHAR(64) | NOT NULL | | 该 WorkSheet 真实数据存储在的数据库表名 |

说明 : 
1. file_id ,  一个 Excel 文件可以包含多个 WorkSheet , 每个 WorkSheet 都对应一个数据库表
2. 数据库表名为 tbl_worksheet_info
3. 创建数据库表的 SQL 语句 , 通过 odb 工具生成

# 3. 要求

1. 编写 odb 数据库表映射类
    1. 编写文件信息表的 odb 映射类 FileEntity , 表名称为 tbl_file_info , 严格按照文件信息表设计中的字段进行映射
    2. 编写 WorkSheet 表的 odb 映射类 WorkSheetEntity , 表名称为 tbl_worksheet_info , 严格按照 WorkSheet 表设计中的字段进行映射
    3. FileEntity 头文件名称 为 file_entity.h , WorkSheetEntity 头文件名称 为 work_sheet_entity.h
    4. 头文件存放在 chat-excel/data 目录下
2. 编写 odb 映射类编译的 CMakeLists.txt 文件 , 完成 FileEntity 和 WorkSheetEntity 的编译 , 要求 : 
    1. CMakeLists.txt 文件存放在 chat-excel/data 目录下
    2. odb 编译后生成的代码文件放置在 chat-excel/data/odb 目录下
    3. odb 生成的 .sql 文件放置在 chat-excel/data/sql 目录下

仔细阅读文件信息表和 WorkSheet 表的设计 , 以及映射类的实现要求 , 完成文件子服务中文件信息表和 WorkSheet 表的 C++ 类映射 , 并完成映射类的编译
