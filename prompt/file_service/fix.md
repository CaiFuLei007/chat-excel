
**身份** : 你是一位资深的 C++ 开发者 , 擅长对 RPC 接口进行修改 和 实现.

**任务** : 为了更好的实现分布式部署 , 需要将文件服务的 RPC 接口进行修改 , 当进行文件解析时 , 传递 FastDFS 文件ID 而不是文件路径 , 以支持分布式部署下的文件解析。

## 1. Excel 解析子服务进行 Excel 文件下载, 文件子服务不需要保存 Excel 文件

现在文件子服务上传 Excel 接口逻辑 : 
1. 调用 FastDFS 中上传文件接口 , 上传文件到 FastDFS 中
2. 更新 MySQL 中FileInfo 结构体中的 fastdfs_file_id 字段 , 为上传的文件 ID
3. 将上传的文件在本地保存一份 , 用于后续 Excel 解析子服务进行使用 , 保存在 build/excel_files/{user_id}/ 目录下 , 将每个用户的文件保存到不同的目录下 , 使用用户 ID 作为目录名进行分类
4. 调用 Excel 解析子服务的 RPC 接口获取 Excel 所有工作表名称, 解析 Excel 文件中所有 WorkSheet 工作表信息(包括表头 , 列信息 , worksheet 数据) , 具体的 RPC 接口在 : chat-excel/proto/excel_parse_service.proto
5. 通过 数据库子服务将解析的 Excel 数据保存到数据库中 , 表名称使用 {file_id}_{worksheet_name} ({worksheet_name} 可以包含字母 , 数字 , 汉字 , 下划线 , 对于其他字符使用下划线替代) (TODO 注释进行标记 , 暂不实现)
6. 使用每个 worksheet 数据存储的表名称 更新 MySQL 中 WorkSheet 表中的 table_name 字段
7. 删除本地保存的 Excel 文件 

为了更好的实现分布式部署 , 需要将文件子服务的 RPC 接口进行修改 , 当进行文件解析时 , 传递 FastDFS 文件ID 而不是文件路径 , 以支持分布式部署下的文件解析 , 并且不需要将文件在本地进行保存了, 修改后实现流程如下 : 
1. 调用 FastDFS 中上传文件接口 , 上传文件到 FastDFS 中
2. 更新 MySQL 中FileInfo 结构体中的 fastdfs_file_id 字段 , 为上传的文件 ID
3. 调用 Excel 解析子服务的 RPC 接口 , 传递 FastDFS 文件ID , 获取 Excel 所有工作表名称, 解析 Excel 文件中所有 WorkSheet 工作表信息(包括表头 , 列信息 , worksheet 数据) , 具体的 RPC 接口在 : chat-excel/proto/excel_parse_service.proto
4. 通过 数据库子服务将解析的 Excel 数据保存到数据库中 , 表名称使用 {file_id}_{worksheet_name} ({worksheet_name} 可以包含字母 , 数字 , 汉字 , 下划线 , 对于其他字符使用下划线替代) (TODO 注释进行标记 , 暂不实现)
5. 使用每个 worksheet 数据存储的表名称 更新 MySQL 中 WorkSheet 表中的 table_name 字段

## 2. 删除文件信息业务实现逻辑 FileBusiness::DeleteFileInfo

当前核心实现逻辑 :
1. 删除 MySQL 和 Redis 中的文件信息元数据

删除文件信息元数据应当删除 MySQL 和 Redis 中的文件信息元数据 , 还有 Excel 对应的 Worksheet 数据(在 WorkSheet 表中) , 修改后的核心实现逻辑 : 
1. 删除 MySQL 和 Redis 中的文件信息元数据(在 tbl_file_info 表中)
2. 删除 Excel 对应的 Worksheet 数据(在 tbl_worksheet_info 表中)

## 3. 数据库子服务负责下载 SQLite 文件 , 而不是文件子服务负责下载

当前 获取 SQLite 文件 , 实现流程如下 :
1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
3. 通过 MySQL 存储的对应的 fastdfs_file_id 字段 , 从 FastDFS 中下载 sqlite 文件保存到本地
4. 调用数据库子服务 , 连接 sqlite 数据库 (TODO 注释进行标记 , 暂不实现)

为了更好的实现分布式部署 , 文件子服务不可以将 SQLite 文件保存到自己的机器上 , 应该由数据库子服务负责下载 SQLite 文件 , 并且将 SQLite 文件保存到数据库子服务的本地目录下 , 用于后续数据库操作 , 修改后的逻辑:
1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
3. 调用数据库子服务 , 传入 FastDFS 文件ID , 下载 sqlite 文件保存到数据库子服务的本地目录下 , 并进行解析(TODO 注释进行标记 , 暂不实现)\



## 4. 补充

1. 对文件子服务中 fastdfs 的操作方式进行修改 , 统一使用 <cpptoolkit/fdfs.h> 封装的接口操作 fastdfs

