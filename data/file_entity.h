#pragma once

#include <string>
#include <odb/core.hxx>
#include <odb/nullable.hxx>

namespace chat_excel
{

/**
 * @brief 文件信息表(tbl_file_info)的 odb 数据库映射类, 完成文件信息表字段与 C++ 类成员的映射
 *        Excel 文件的二进制内容存储在 FastDFS, 本表仅存储文件的元信息
 *        建表 SQL 语句以及 CURD 支持代码由 odb 编译器生成
 *        file_id 为唯一键, 修改操作通过 file_id 定位记录, 主键 id 与 file_id 不提供修改
 */
// odb pragma 指令仅 odb 编译器识别, 使用宏守卫防止普通 C++ 编译器产生未知 pragma 警告
#ifdef ODB_COMPILER
#pragma db object table("tbl_file_info")
#endif
class FileEntity
{
public:
    /**
     * @brief 构造函数, 创建文件信息表映射对象, 用于新文件元信息的插入
     * @param file_id Excel 文件唯一标识符
     * @param file_name Excel 文件名
     * @param file_extension Excel 文件扩展名
     * @param file_size Excel 文件大小, 单位字节
     * @param file_upload_time Excel 文件上传时间, 由上层传递
     * @param fastdfs_file_id Excel 文件在 FastDFS 中的文件 ID
     * @param user_id 文件所属用户 ID
     * @param session_id 文件所属会话 ID
     */
    FileEntity(const std::string& file_id, const std::string& file_name, const std::string& file_extension,
               unsigned long long file_size, unsigned long long file_upload_time,
               const odb::nullable<std::string>& fastdfs_file_id, const std::string& user_id,
               const odb::nullable<std::string>& session_id);

    /**
     * @brief 获取自增主键 ID
     * @return 自增主键 ID
     */
    unsigned long long Id() const;

    /**
     * @brief 获取 Excel 文件唯一标识符
     * @return Excel 文件唯一标识符
     */
    const std::string& FileId() const;

    /**
     * @brief 获取 Excel 文件名
     * @return Excel 文件名
     */
    const std::string& FileName() const;

    /**
     * @brief 获取 Excel 文件扩展名
     * @return Excel 文件扩展名
     */
    const std::string& FileExtension() const;

    /**
     * @brief 获取 Excel 文件大小, 单位字节
     * @return Excel 文件大小
     */
    unsigned long long FileSize() const;

    /**
     * @brief 获取 Excel 文件上传时间
     * @return Excel 文件上传时间
     */
    unsigned long long FileUploadTime() const;

    /**
     * @brief 获取 Excel 文件在 FastDFS 中的文件 ID
     * @return FastDFS 中的文件 ID, 可为空(文件元信息先入库, 上传 FastDFS 后再更新)
     */
    const odb::nullable<std::string>& FastdfsFileId() const;

    /**
     * @brief 获取文件所属用户 ID
     * @return 文件所属用户 ID
     */
    const std::string& UserId() const;

    /**
     * @brief 获取文件所属会话 ID
     * @return 文件所属会话 ID, 可为空(文件可以不属于任何聊天会话)
     */
    const odb::nullable<std::string>& SessionId() const;

    /**
     * @brief 设置 Excel 文件名
     * @param file_name Excel 文件名
     */
    void SetFileName(const std::string& file_name);

    /**
     * @brief 设置 Excel 文件扩展名
     * @param file_extension Excel 文件扩展名
     */
    void SetFileExtension(const std::string& file_extension);

    /**
     * @brief 设置 Excel 文件大小
     * @param file_size Excel 文件大小, 单位字节
     */
    void SetFileSize(unsigned long long file_size);

    /**
     * @brief 设置 Excel 文件上传时间
     * @param file_upload_time Excel 文件上传时间
     */
    void SetFileUploadTime(unsigned long long file_upload_time);

    /**
     * @brief 设置 Excel 文件在 FastDFS 中的文件 ID
     * @param fastdfs_file_id FastDFS 中的文件 ID, 空值表示文件尚未上传到 FastDFS
     */
    void SetFastdfsFileId(const odb::nullable<std::string>& fastdfs_file_id);

    /**
     * @brief 设置文件所属用户 ID
     * @param user_id 文件所属用户 ID
     */
    void SetUserId(const std::string& user_id);

    /**
     * @brief 设置文件所属会话 ID
     * @param session_id 文件所属会话 ID, 空值表示文件不属于任何聊天会话
     */
    void SetSessionId(const odb::nullable<std::string>& session_id);

private:
    // odb 框架通过友元访问私有默认构造函数与私有数据成员
    friend class odb::access;

    // 默认构造函数, 仅供 odb 框架从数据库加载数据时使用
    FileEntity() = default;

    // 自增主键 ID
#ifdef ODB_COMPILER
#pragma db id auto column("id")
#endif
    unsigned long long id_;

    // Excel 文件唯一标识符, 通过该字段获取指定文件的元信息
    // unique 约束自动创建唯一索引, 保证字段唯一性的同时支持快速查找文件信息
#ifdef ODB_COMPILER
#pragma db column("file_id") type("VARCHAR(32)") unique
#endif
    std::string file_id_;

    // Excel 文件名
#ifdef ODB_COMPILER
#pragma db column("file_name") type("VARCHAR(64)") not_null
#endif
    std::string file_name_;

    // Excel 文件扩展名
#ifdef ODB_COMPILER
#pragma db column("file_extension") type("VARCHAR(16)") not_null
#endif
    std::string file_extension_;

    // Excel 文件大小, 单位字节
#ifdef ODB_COMPILER
#pragma db column("file_size") type("BIGINT UNSIGNED") not_null
#endif
    unsigned long long file_size_;

    // Excel 文件上传时间, 由上层业务层传递, 数据库不自动生成
#ifdef ODB_COMPILER
#pragma db column("file_upload_time") type("BIGINT UNSIGNED") not_null
#endif
    unsigned long long file_upload_time_;

    // Excel 文件在 FastDFS 中的文件 ID, 用于定位二进制文件内容
    // 可为空(文件元信息先入库, 上传 FastDFS 后再更新该字段)
#ifdef ODB_COMPILER
#pragma db column("fastdfs_file_id") type("VARCHAR(64)")
#endif
    odb::nullable<std::string> fastdfs_file_id_;

    // 文件所属用户 ID, 普通索引用于快速查找用户的文件信息
#ifdef ODB_COMPILER
#pragma db column("user_id") type("VARCHAR(32)") not_null
#pragma db index
#endif
    std::string user_id_;

    // 文件所属会话 ID, 可为空(文件可以不属于任何聊天会话)
#ifdef ODB_COMPILER
#pragma db column("session_id") type("VARCHAR(32)")
#endif
    odb::nullable<std::string> session_id_;
};

} // namespace chat_excel
