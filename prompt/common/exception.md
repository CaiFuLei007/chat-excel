
- 你是一个资深的 C++ 后端开发工程师 , 在项目开始过程中经常会使用自定义异常来处理项目中出现的异常以及错误情况。

任务 : 为本项目实现错误码枚举类 ErrorCode , 以及 ChatExcelException 异常类定义

# ErrorCode 错误码说明:
1. 错误码放在 ErrorCode 枚举类中 , ErrorCode 枚举使用 enum class 定义 , 枚举值为整形 , 错误码分为六种 , 对应每一种服务错误 
2. 用户子服务错误码范围 100 - 199 , 错误码格式 : USER_错误码描述 , eg : USER_NICKNAME_EXISTS 用户昵称已存在
3. 文件子服务错误码范围 200 - 299 , 错误码格式 : FILE_错误码描述 , eg : FILE_NOT_FOUND 文件不存在
4. 数据库子服务错误码范围 300 - 399 , 错误码格式 : DB_错误码描述 , eg : DB_CONNECTION_FAILED 数据库连接失败
5. Excel 解析子服务错误码范围 400 - 499 , 错误码格式 : EXCEL_错误码描述 , eg : EXCEL_PARSE_FAILED Excel 解析失败
6. 通知子服务错误码范围 500 - 599 , 错误码格式 : NOTIFY_错误码描述 , eg : NOTIFY_SEND_FAILED 通知发送失败
7. AI 子服务错误码范围 600 - 699 , 错误码格式 : AI_错误码描述 , eg : AI_MODEL_NOT_FOUND AI 模型不存在
8. 网关子服务错误码范围 700 - 799 , 错误码格式 : GATEWAY_错误码描述 , eg : GATEWAY_CONNECTION_FAILED 网关连接失败
9. SUCCESS 表示成功 , 其值为 0 
10. 目前只需要定义出 SUCCESS 错误码 , 其值为 0 , 其余错误码在项目开发过程中根据可能出现的异常情况再进行定义 , 但是预留出每个子服务的错误码范围以及对应的注释

# ErrorMessage 错误码转错误码描述
1. 添加一个 ErrorMessage 方法 , 将错误码转换为对应的错误信息(中文描述)
2. 通过 unordered_map 来存储错误码和对应的错误信息 , 错误码作为键 , 错误信息作为值
3. 如果错误码不存在 , 则返回 "未知错误"
4. 错误码描述展示不需要添加 , 后续开发过程中根据出现的错误与 ErrorCode 一同添加

# GetServiceName 错误码转服务名称
1. 添加一个全局的方法 GetServiceName  , 接受一个错误码 , 返回错误码所属的子服务名称
2. 网关子服务名称 : GatewayService
3. 用户子服务名称 : UserService
4. 文件子服务名称 : FileService
5. 数据库子服务名称 : DatabaseService
6. Excel 解析子服务名称 : ExcelService
7. 通知子服务名称 : NotifiyService
8. AI 子服务名称 : AIService
9. 错误码不存在 , 则返回 "未知错误"

# ChatExcelException 异常类
1. 继承自 std::exception , 用于处理项目中出现的异常以及错误情况
2. 添加两个私有成员变量存储 errorCode错误码 , 和错误码描述 
3. 添加构造函数 , 接受一个错误码
4. 重写 what 方法 , 返回错误码信息 , 格式要求 : "服务名称 : 错误码描述"

# 输出要求
1. 头文件放在 common/exception.h 中
   1. 包含 ErrorCode 枚举类定义
   2. 包含 ErrorMessage 方法定义
   3. 包含 GetServiceName 方法定义
   4. 包含 ChatExcelException 异常类定义
2. 源文件放在 common/exception.cpp 中
   1. 包含 ErrorMessage 方法实现
   2. 包含 GetServiceName 方法实现
   3. 包含 ChatExcelException 异常类实现

严格按照上述要求帮我实现错误码枚举类型以及异常类的声明和定义