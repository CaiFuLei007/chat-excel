#include "user_entity.h"

namespace chat_excel
{

UserEntity::UserEntity(const std::string& user_id, const std::string& nickname,
                       const std::string& email, const std::string& password, UserStatus status)
    : user_id_(user_id),
      nickname_(nickname),
      email_(email),
      password_(password),
      status_(status)
{
}

unsigned long long UserEntity::Id() const
{
    return id_;
}

const std::string& UserEntity::UserId() const
{
    return user_id_;
}

const std::string& UserEntity::Nickname() const
{
    return nickname_;
}

const std::string& UserEntity::Email() const
{
    return email_;
}

const std::string& UserEntity::Password() const
{
    return password_;
}

UserStatus UserEntity::Status() const
{
    return status_;
}

void UserEntity::SetStatus(UserStatus status)
{
    status_ = status;
}

} // namespace chat_excel
