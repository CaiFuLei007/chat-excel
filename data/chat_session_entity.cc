#include "chat_session_entity.h"

namespace chat_excel
{

ChatSessionEntity::ChatSessionEntity(const std::string& chat_session_id, const std::string& user_id,
                                     const odb::nullable<std::string>& title, unsigned long long create_time,
                                     unsigned long long update_time, unsigned long long total_message_count,
                                     const std::string& model_name, const odb::nullable<std::string>& file_id,
                                     const std::string& type, const odb::nullable<std::string>& connection_info)
    : chat_session_id_(chat_session_id),
      user_id_(user_id),
      title_(title),
      create_time_(create_time),
      update_time_(update_time),
      total_message_count_(total_message_count),
      model_name_(model_name),
      file_id_(file_id),
      type_(type),
      connection_info_(connection_info)
{
}

unsigned long long ChatSessionEntity::Id() const
{
    return id_;
}

const std::string& ChatSessionEntity::ChatSessionId() const
{
    return chat_session_id_;
}

const std::string& ChatSessionEntity::UserId() const
{
    return user_id_;
}

const odb::nullable<std::string>& ChatSessionEntity::Title() const
{
    return title_;
}

unsigned long long ChatSessionEntity::CreateTime() const
{
    return create_time_;
}

unsigned long long ChatSessionEntity::UpdateTime() const
{
    return update_time_;
}

unsigned long long ChatSessionEntity::TotalMessageCount() const
{
    return total_message_count_;
}

const std::string& ChatSessionEntity::ModelName() const
{
    return model_name_;
}

const odb::nullable<std::string>& ChatSessionEntity::FileId() const
{
    return file_id_;
}

const std::string& ChatSessionEntity::Type() const
{
    return type_;
}

const odb::nullable<std::string>& ChatSessionEntity::ConnectionInfo() const
{
    return connection_info_;
}

void ChatSessionEntity::SetTitle(const odb::nullable<std::string>& title)
{
    title_ = title;
}

void ChatSessionEntity::SetUpdateTime(unsigned long long update_time)
{
    update_time_ = update_time;
}

void ChatSessionEntity::SetTotalMessageCount(unsigned long long total_message_count)
{
    total_message_count_ = total_message_count;
}

void ChatSessionEntity::SetFileId(const odb::nullable<std::string>& file_id)
{
    file_id_ = file_id;
}

void ChatSessionEntity::SetConnectionInfo(const odb::nullable<std::string>& connection_info)
{
    connection_info_ = connection_info;
}

} // namespace chat_excel
