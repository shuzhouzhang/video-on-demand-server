#include "../../server/repository/admin_repository.h"
#include "../../server/svc_user/source/user_repository.h"
#include "../../server/svc_video/source/video_repository.h"

#include <iostream>
#include <type_traits>

int main() {
    static_assert(std::is_base_of_v<biterepo::IUserRepository,
                                    biteuser::MySqlUserRepository>);
    static_assert(!std::is_base_of_v<biterepo::IVideoRepository,
                                     biteuser::MySqlUserRepository>);
    static_assert(!std::is_base_of_v<biterepo::IInteractionRepository,
                                     biteuser::MySqlUserRepository>);
    static_assert(std::is_base_of_v<biterepo::IVideoRepository,
                                    bitevideo::MySqlVideoRepository>);
    static_assert(std::is_base_of_v<biterepo::IInteractionRepository,
                                    bitevideo::MySqlVideoRepository>);
    static_assert(!std::is_base_of_v<biterepo::IUserRepository,
                                     bitevideo::MySqlVideoRepository>);
    static_assert(std::is_base_of_v<biterepo::IAdminRepository,
                                    biterepo::MySqlAdminRepository>);
    std::cout << "[PASS] repository domain interfaces are separated\n";
    return 0;
}
