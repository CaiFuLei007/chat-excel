
# Excel 解析子服务实现

**系统身份** : 你是一个资深的 C++ 开发工程师 , 熟悉后端开发中业务逻辑的实现 , 熟悉后端的 RPC 接口实现 , 熟悉使用 Rpc 框架实现 Rpc 服务

**任务** : 完成 Excel 解析子服务的实现

## 1. Excel 解析子服务介绍

1. Excel 解析子服务属于内部服务 , 不与网关进行交互 , 仅为文件子服务提供 Excel 文件解析的服务. 
2. 功能包含 : 
	1. 解析 Excel 中的所有 worksheet
	2. 解析 worksheet 中的各个数据

用户上传 Excel 文件之后 , 前后端整体的数据流如下 : 
1. 用户在前端将 Excel 文件进行上传
2. 网关接收到 Excel 文件之后, 进行鉴权以及协议转换 , 将 Excel 文件交给文件子服务
3. 文件子服务将 Excel 保存到本地 , 然后向 Excel 解析子服务发送 RPC 请求对 Excel 文件进行解析
4. Excel 解析子服务解析出 Excel 文件的所有 worksheet 以及各个 worksheet 中的数据 , 并返回给文件子服务
5. 文件解析子服务获取到 各个 worksheet 中的数据 之后 , 将其保存到数据库中
6. 文件子服务将文件上传结果返回给网关子服务
7. 网关子服务将结果返回给前端

## 2. 接口定义

Excel 解析子服务包含两个 RPC 接口 : 
1. 解析 Excel 包含的所有 worksheet 工作表列表
2. 解析 Excel 文件中指定的 worksheet 表结构以及表数据
3. 具体的 RPC 接口查看 : chat-excel/proto/excel_parse_service.proto

## 3. Excel 解析器

Excel 解析器基于 OpenXLSX 库实现.

1. ExcelParse 类 , 功能如下 : 
	1. 解析 Excel 文件所有 worksheet 的 名称
	2. 解析指定 worksheet 的表数据 : 需要单独解析表头信息 , 列类型信息 , 表数据 , 文件子服务将来需要用这些信息将 Excel 的 worksheet 数据以数据库表的形式存储到数据库中


### 3.1 表头信息

1. 主要获取列名 , 并检查列名是否有效
2. 表的第一行作为表头信息 , 每列的第一个单元格作为列名
3. 列名只能包含 : 大小写字母 , 数字(数字不能作为开头) , 汉字 , 下划线
4. 非法字符使用下划线进行替换 
5. 如果列名为空 , 使用 column_列号 作为类名
6. 如果列名开头是数字 , 添加前缀 : col_列名

### 3.2 列类型信息

采样 worksheet 的前 100 行数据 , 将每列的 100 个单元格类型出现次数最多的类型作为该列类型 , 在逐个检查该列的各个单元格类型的时候遵循以下规则 : 
1. 如果单元格为空或 N/A , 不统计该单元格 , 检测时需要将单元格数值的前后空白字符去掉 
2. 单元格类型包含 : 浮点型 , 布尔类型 , 日期 , 字符串类型 , 整型
3. 对于整形 , 如果单元格内只存在 数值 , 以及开头的 +/- 号 , 则视为整型
4. 对于浮点型 , 如果单元格内存在小数 ,不包含数字以及 +/- 号以外的其他字符, 说明该单元格是浮点型
5. 对于布尔类型需要考虑所有布尔类型的表示 : 真 : true , 1 , t , yes , y , 假 : false , 0 , f ,no , n . 检查时忽略大小写
6. 对于日期类型需要考虑所有日期表示 : yyyy-mm-dd , yyyy/mm/dd , mm-dd-yyyy , mm/dd/yyyy , yyyy.mm.dd , yyyy-mm-dd-HH:MM:SS 
7. 如果不属于以上所有类型就视为字符串类型
8. 将某列的 100 个单元格(如果不超过100个单元格 , 全部进行统计)类型统计完成之后 , 出现次数最多的类型作为该列类型

### 3.3 表数据

解析出 worksheet 中除表头行外的所有数据行 , 返回结果中每个单元格包含值以及单元格类型 , 解析单元格的时候需要遵循一下规则 : 
1. 单元格为整形时 , 将其转化为 int64 类型后再转化为字符串
2. 单元格为浮点型时 , 将其转化为 double 类型后再转化为字符串
3. 单元格为布尔类型时 , 将其转化 "0" 或 "1"
4. 其余类型统一转化为字符串类型

注意 : 实现时避免大段代码 , 将每个功能封装层独立的函数

## 4. 业务层实现

Excel 解析业务层主要由 ExcelParseBusiness 类实现, 是对 ExcelParse 的封装 , 主要包含 : 
1. Excel 解析器实例智能指针
2. 解析 Excel 文件所有 worksheet 的 worksheet 名称
3. 解析指定 worksheet 的表数据 , 组织成 RPC 接口需要的 WorkSheetData 结构化数据
4. 实现流程可参考邮箱通知子服务的 NotifyBusiness 接口定义和实现 : chat-excel/svc_email_notify_service/notify_business.h , chat-excel/svc_email_notify_service/notify_business.cpp

## 5. RPC 接口实现

1. Excel 解析 RPC 接口由 ExcelParseServiceImpl 类实现 , RPC 接口会通过 proto 工具 生成 C++ 基类 , 保存在 chat-excel/proto/proto_code 中 
2. ExcelParseServiceImpl 继承 其是生成的 ExcelParserService 基类 , 并实现解析Excel 文件所有 worksheet 的 worksheet 名称 以及  解析指定 worksheet 的表数据的虚函数
3. 实现流程可参考邮箱通知子服务的 NotifyServiceImpl 接口定义和实现 : chat-excel/svc_email_notify_service/notify_service_impl.h , chat-excel/svc_email_notify_service/notify_service_impl.cpp


## 6. RPC 服务器搭建

RPC 服务器搭建由 ExcelParseServer 类实现 , 该类包含 : 
1. ExcelParseServiceImpl  智能指针 , 调用业务逻辑接口
2. <cpp-toolkit/etcd.h> 中的 SvcProvider 智能指针 ,  用于服务注册
3. brpc::Server (使用 <cpp-toolkit/rpc.h> 中的 ServerFactory 构建)

ExcelParseServerBuilder 构建 ExcelParseServer :
1. 成员 : 
	1. etcd 服务器地址
	2. brpc 子服务服务器端口 , 子服务名称 , 子服务地址 : 使用 struct BrpcSettings 来存储
2. 主要功能 :  完成 RPC 服务器端口 , 注册中心结构初始化 , 最后通过 Build 构造好 NotifyServer 对象并返回
3. 实现流程可参考邮箱通知子服务的 NotifyServer 服务器搭建 : chat-excel/svc_email_notify_service/notify_server.h , chat-excel/svc_email_notify_service/notify_server.cpp
## 7. 主流程

1. 完成gflags参数解析 , 日志初始化 , 注册中心初始化 , 以及 RPC服务器构建和启动 , 源文件为main.cc, 放置在 chat-excel/svc_excel_parse_service 目录下
2. 完成对 Excel 解析子服务的编译，生成CMakeLists.txt文件，放置在 chat-excel/svc_excel_parse_service 目录下
3. 实现流程可参考邮箱通知子服务的 子流程实现 : chat-excel/svc_email_notify_service/main.cc

## 8. 实现流程

Excel 解析子服务实现流程如下：
1. 封装 ExcelParse 类 :
	1. 解析 Excel 文件所有 worksheet 的 名称
	2. 解析指定 worksheet 的表数据 : 需要单独解析表头信息 , 列类型信息 , 表数据 , 文件子服务将来需要用这些信息将 Excel 的 worksheet 数据以数据库表的形式存储到数据库中
2. 封装 ExcelParseBusiness 类 , 负责 Exccel 解析的业务逻辑 , 头文件为 excel_parse_business.h , 源文件为 excel_parse_business.cc , 包含在 excel_parse_service 命名空间中
3. 封装 ExcelParseServiceImpl 类 , 实现发送验证码以及普通邮件的RPC接口 , 头文件为 excel_parse_service_impl.h , 源文件为 excel_parse_service_impl.cc , 包含在 excel_parse_service 命名空间中
4. 封装 ExcelParseServer 类 , 搭建RPC服务器 , 并完成RPC服务器构建 , 头文件为 excel_parse_server.h , 源文件为 excel_parse_server.cc , 包含在 excel_parse_service 命名空间中
5. 实现主流程 , 头文件为main.cc
6. 实现CMakeLists.txt , 完成项目构建 注意：
- 所有类包含在 excel_parse_service 命名空间中
- 代码实现必须严格遵守项目规则文档 以及 上述要求
- 所有文件放置在 chat-excel/svc_excel_parse_service 目录中

## 9. 补充要求

1. OpenXLSX 接口会抛异常 , 要考虑异常捕获
2. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
3. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印

请仔细阅读上述需求 , 然后列出你的详细实现规划 , 具体到每个类的职责以及实现逻辑 , 等我看完确保你和我理解一致后 , 我再告诉你后续实现