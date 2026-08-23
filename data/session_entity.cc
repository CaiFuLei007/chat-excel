#include "session_entity.h"

namespace chat_excel
{

SessionEntity::SessionEntity(const std::string& session_id, const std::string& user_id)
    : session_id_(session_id),
      user_id_(user_id)
{
}

unsigned long long SessionEntity::Id() const
{
    return id_;
}

const std::string& SessionEntity::SessionId() const
{
    return session_id_;
}

const std::string& SessionEntity::UserId() const
{
    return user_id_;
}

} // namespace chat_excel
