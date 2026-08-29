
**身份** : 你是一位资深的 C++ 开发者 , 擅长对 RPC 接口进行修改 和 实现.

**任务** : 为了更好的实现分布式部署 , 需要将 Excel 解析服务的 RPC 接口进行修改 , 当进行 Excel 文件解析时 , 传递 FastDFS 文件ID 而不是文件路径 , 以支持分布式部署下的文件解析。

## 1.  文件解析 RPC 接口

文件解析子服务由两个 RPC 接口组成 : 
1. 获取工作表列表 RPC 接口 : 用于获取 Excel 文件的工作表列表
2. 解析 Excel 文件 RPC 接口 : 解析 Excel 文件 , 并返回解析结果

当前两个接口的实现逻辑是 : 
1. 通过 RPC 接口传入的 Excel 文件路径找到对应的 Excel 文件
2. 对 Excel 文件进行解析 , 两个 RPC 接口对应操作是 : 解析 Excel 中所有工作表名称 , 以及解析 Excel 每个工作表中的具体信息(包括列名 , 数据类型 , 数据值等)
3. 将解析出来的数据进行返回

现在为了更好的实现分布式部署 , 需要对两个 RPC 接口逻辑进行修改: 
1. RPC 接口不再需要传递 Excel 文件路径 , 而是传递 FastDFS 文件ID 
2. 通过 FastDFS 文件ID 找到对应的 Excel 文件 , 并下载到本地 , 本地保存路径是 /tmp/excel_files/{request_id}/ 此处的 request_id 表示的本次请求的 ID , 防止不同请求之间的文件冲突
3. 对本地保存的 Excel 文件进行解析 
4. 解析完成后将 保存的文件进行删除 : 删除 {request_id}/ 中的所有文件
5. 将解析出来的数据进行返回

## 2. 注意

1. 对 fastdfs 操作使用 <cpptoolkit/fdfs.h> 中的封装的接口进行操作
2. 下载逻辑放在 ExcelParseBusiness 层完成
3. FdfsClient 初始化配置: 在 ExcelParseServerBuilder 中进行完成 , 传入FdfsSettings 在内部进行初始化
4. 如果新增了错误 , 需要在 exception.h 中新增对应的错误码  
5. 异常时的临时文件清理：中途抛异常（文件打开失败等）时，/tmp/excel_files/{request_id}/ 也要保证清理
6. 调用方暂时不修改 , 本次只对 excel 解析子服务进行修改
7. 两次下载可接受：UploadFileData 先调 GetWorksheets 再调 ParseExcel，新方案下同一文件会从 FastDFS 下载两次