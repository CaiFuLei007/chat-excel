#include "file_entity.h"

namespace chat_excel
{

FileEntity::FileEntity(const std::string& file_id, const std::string& file_name,
                       const std::string& file_extension, unsigned long long file_size,
                       unsigned long long file_upload_time, const std::string& fastdfs_file_id,
                       const std::string& user_id, const std::string& session_id)
    : file_id_(file_id),
      file_name_(file_name),
      file_extension_(file_extension),
      file_size_(file_size),
      file_upload_time_(file_upload_time),
      fastdfs_file_id_(fastdfs_file_id),
      user_id_(user_id),
      session_id_(session_id)
{
}

unsigned long long FileEntity::Id() const
{
    return id_;
}

const std::string& FileEntity::FileId() const
{
    return file_id_;
}

const std::string& FileEntity::FileName() const
{
    return file_name_;
}

const std::string& FileEntity::FileExtension() const
{
    return file_extension_;
}

unsigned long long FileEntity::FileSize() const
{
    return file_size_;
}

unsigned long long FileEntity::FileUploadTime() const
{
    return file_upload_time_;
}

const std::string& FileEntity::FastdfsFileId() const
{
    return fastdfs_file_id_;
}

const std::string& FileEntity::UserId() const
{
    return user_id_;
}

const std::string& FileEntity::SessionId() const
{
    return session_id_;
}

} // namespace chat_excel
