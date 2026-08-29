
#  文件业务逻辑实现

**系统身份** : 你是一个资深的 C++ 开发工程师 , 熟悉后端开发中业务逻辑的实现

**任务** : 完成文件子服务的业务逻辑的实现

## 1. 文件业务

1. 定义 FileBusiness 类 , 包含在 chat_excel::file_service 命名空间中,负责管理文件业务逻辑
2. 头文件 file_business.h , 源文件 file_business.cpp , 放在 chat-excel/svc_file_service 路径下
3. 成员如下 :
	1. FileData 智能指针 , 用于操作管理 Excel 元信息
	2. WorkSheetData 智能指针,用于操作管理 WorkSheet 工作表信息
	4. <cpp-toolkit/rpc.h> 中的 ChannelManager 智能指针, 用于操作RPC通道
	5. 所有成员变量的初始化必须在 FileBusiness 构造函数中进行,但具体实例其他位置传入
4. FileBusiness 类涉及到的操作如下 : 
	1. 上传文件(Excel/SQLite)信息 , 实现流程如下
        1. 构建 FileInfo 结构体 , 填充各个字段 , 通过 <cpp-toolkit/util.h> 中的 uuid 生成器接口来创建文件 ID , file_upload_time 为当前时间戳 , fastdfs_file_id 为空字符串
        2. 调用 FileData 中保存文件信息接口 , 保存文件信息到数据库
    2. 获取文件信息
    3. 删除文件信息
    4. 上传文件数据 , 实现流程如下  :
        1. 调用 FastDFS 中上传文件接口 , 上传文件到 FastDFS 中
        2. 更新 MySQL 中FileInfo 结构体中的 fastdfs_file_id 字段 , 为上传的文件 ID
        3. 将上传的文件在本地保存一份 , 用于后续 Excel 解析子服务进行使用 , 保存在 build/excel_files/{user_id}/ 目录下 , 将每个用户的文件保存到不同的目录下 , 使用用户 ID 作为目录名进行分类
        4. 调用 Excel 解析子服务的 RPC 接口获取 Excel 所有工作表名称, 解析 Excel 文件中所有 WorkSheet 工作表信息(包括表头 , 列信息 , worksheet 数据) , 具体的 RPC 接口在 : chat-excel/proto/excel_parse_service.proto
        5. 通过 数据库子服务将解析的 Excel 数据保存到数据库中 , 表名称使用 {file_id}_{worksheet_name} ({worksheet_name} 可以包含字母 , 数字 , 汉字 , 下划线 , 对于其他字符使用下划线替代) (TODO 注释进行标记 , 暂不实现)
        6. 使用每个 worksheet 数据存储的表名称 更新 MySQL 中 WorkSheet 表中的 table_name 字段
        7. 删除本地保存的 Excel 文件 
    5. 下载 Excel 文件数据 :
        1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
        2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
        3. 通过 MySQL 存储的对应的 fastdfs_file_id 字段 , 从 FastDFS 中下载文件 
        4. 返回下载的文件数据 
    6. 删除文件 
        1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
        2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
        3. 调用 FastDFS 中删除文件接口 , 删除文件从 FastDFS 中
        5. 调用数据库子服务 , 删除数据库中对应的 WorkSheet 表数据  (TODO 注释进行标记 , 暂不实现)
        4. 删除 MySQL 中对应的 FileInfo 结构体
        5. 删除 Redis 中对应的文件信息
    7. 获取用户上传的所有文件信息列表
    8. 预览 Excel 文件 , 实现流程如下  
        1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
        2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
        3. 通过数据库子服务 , 从数据库中获取 Excel 文件的解析结果   (TODO 注释进行标记 , 暂不实现)
    9. 上传 SQLite 文件数据 : 实现方逻辑与上传 Excel 文件相同 , 将 sqlite 文件保存到 FastDFS 中
    10. 获取 SQLite 文件 , 实现流程如下 :
        1. 通过 文件 ID 查找 Redis , MySQL 中获取文件信息
        2. 检查当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
        3. 通过 MySQL 存储的对应的 fastdfs_file_id 字段 , 从 FastDFS 中下载 sqlite 文件保存到本地
        4. 调用数据库子服务 , 连接 sqlite 数据库 (TODO 注释进行标记 , 暂不实现)
    11. 关联文件和聊天会话 

## 3. 注意事项

1. 每一个操作都需要先检查 : 当前 user_id 与 文件信息中的 user_id 是否一致 ,如果不一致 , 则抛出异常
2. 涉及到 AI 子服务 , 数据库子服务的接口 , 暂时不进行实现
3. Excel 文件元信息 , WorkSheet 工作表元信息都保存在 MySQL 数据库中 , 分别对应 FileInfo 表 , WorkSheet 表 , 文件真实数据保存在 FastDFS 中
4. MySQL 中文件信息更新之后要将 Redis 原来存储的文件信息删除 , 等待后续重新缓存
5. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
6. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
7. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
8. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性

请求仔细阅读业务逻辑层的要求,然后帮我复述一遍,并给出你的实现详细思路,等我确认你和我理解一致之后,我再逐步告诉你实现实现步骤