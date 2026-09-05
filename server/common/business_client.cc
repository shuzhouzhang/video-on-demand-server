#include "business_client.h"
#include "business_adapter.h"
#include <brpc/channel.h>
#include <brpc/controller.h>
namespace biterpc {
namespace {
template <class Request, class Response, class Stub, class Method>
bool callBusiness(brpc::Channel &channel, const httplib::Request &request,
                  const std::optional<std::string> &account,
                  const std::string &requestId, ForwardResponse &response,
                  std::string &error, const char *methodName, Method method,
                  bool multipart, const char *arrayField) {
    Request input;
    Response output;
    input.mutable_context()->set_request_id(requestId);
    if (account)
        input.mutable_context()->set_authenticated_account(*account);
    if (request.path == "/logout") {
        auto token = biteauth::parseBearerToken(
            request.get_header_value("Authorization"));
        if (token)
            input.mutable_context()->set_session_token(*token);
    }
    Json::Value payload(Json::objectValue);
    bool valid = true;
    if (request.method == "GET") {
        for (const auto &pair : request.params)
            payload[pair.first] = pair.second;
    } else if (multipart) {
        valid = request.is_multipart_form_data();
        if (request.path == "/videos/upload") {
            const auto metadata = biteutil::JSON::unserialize(
                request.get_file_value("metadata").content);
            valid = valid && metadata && metadata->isObject();
            if (metadata)
                payload = *metadata;
        } else if (request.has_file("account"))
            payload["account"] = request.get_file_value("account").content;
        const auto *descriptor = input.GetDescriptor();
        auto *reflection = input.GetReflection();
        for (int i = 2; i < descriptor->field_count(); ++i) {
            const auto *field = descriptor->field(i);
            if (!request.has_file(field->name().c_str()))
                continue;
            const auto part = request.get_file_value(field->name().c_str());
            auto *target = reflection->MutableMessage(&input, field);
            auto *pr = target->GetReflection();
            const auto *pd = target->GetDescriptor();
            pr->SetString(target, pd->FindFieldByName("filename"),
                          part.filename);
            pr->SetString(target, pd->FindFieldByName("content"), part.content);
            pr->SetString(target, pd->FindFieldByName("contentType"),
                          part.content_type);
        }
    } else {
        const auto parsed = biteutil::JSON::unserialize(request.body);
        valid = parsed && parsed->isObject();
        if (parsed)
            payload = *parsed;
    }
    // Ignore extra legacy UI fields, while checking every declared field's
    // type.
    Json::Value filtered(Json::objectValue);
    const auto *pd = input.payload().GetDescriptor();
    for (int i = 0; i < pd->field_count(); ++i) {
        const auto &key = pd->field(i)->name();
        if (payload.isMember(key))
            filtered[key] = payload[key];
    }
    valid = valid && fillMessage(filtered, *input.mutable_payload());
    if (!valid) {
        response.status = 400;
        response.contentType = "application/json; charset=utf-8";
        response.body =
            R"({"success":false,"message":"invalid business request"})";
        return true;
    }
    Stub stub(&channel);
    brpc::Controller controller;
    (stub.*method)(&controller, &input, &output, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    auto body = messageJson(output.result());
    if (output.result().success() && arrayField && !body.isMember(arrayField))
        body[arrayField] = Json::arrayValue;
    if (request.path == "/videos" && request.method == "GET" &&
        output.result().success())
        body = body["videos"];
    response.status = output.status().code();
    response.contentType = "application/json; charset=utf-8";
    response.body = biteutil::JSON::serialize(body).value_or("{}");
    response.requestId = requestId;
    response.headers.emplace("X-Vod-Rpc-Method", methodName);
    return true;
}
} // namespace
bool hasBusinessRoute(const httplib::Request &request) {
    if (request.method == "POST" && request.path == "/login")
        return true;
    if (request.method == "POST" && request.path == "/login/password")
        return true;
    if (request.method == "POST" && request.path == "/login/email-code")
        return true;
    if (request.method == "POST" && request.path == "/login/email")
        return true;
    if (request.method == "POST" && request.path == "/logout")
        return true;
    if (request.method == "GET" && request.path == "/users/profile")
        return true;
    if (request.method == "POST" && request.path == "/users/profile")
        return true;
    if (request.method == "POST" && request.path == "/users/avatar")
        return true;
    if (request.method == "GET" && request.path == "/admin/users")
        return true;
    if (request.method == "POST" && request.path == "/admin/users/action")
        return true;
    if (request.method == "GET" && request.path == "/videos")
        return true;
    if (request.method == "POST" && request.path == "/videos")
        return true;
    if (request.method == "POST" && request.path == "/videos/upload")
        return true;
    if (request.method == "GET" && request.path == "/videos/detail")
        return true;
    if (request.method == "GET" && request.path == "/videos/search")
        return true;
    if (request.method == "GET" && request.path == "/videos/play-url")
        return true;
    if (request.method == "GET" && request.path == "/users/videos")
        return true;
    if (request.method == "GET" && request.path == "/admin/reviews")
        return true;
    if (request.method == "POST" && request.path == "/admin/reviews/action")
        return true;
    if (request.method == "GET" && request.path == "/videos/like-status")
        return true;
    if (request.method == "POST" && request.path == "/videos/like")
        return true;
    if (request.method == "POST" && request.path == "/videos/unlike")
        return true;
    if (request.method == "GET" && request.path == "/videos/watch-progress")
        return true;
    if (request.method == "POST" && request.path == "/videos/watch-progress")
        return true;
    if (request.method == "GET" && request.path == "/videos/favorite-status")
        return true;
    if (request.method == "POST" && request.path == "/videos/favorite")
        return true;
    if (request.method == "POST" && request.path == "/videos/unfavorite")
        return true;
    if (request.method == "GET" && request.path == "/users/favorites")
        return true;
    if (request.method == "GET" && request.path == "/videos/comments")
        return true;
    if (request.method == "POST" && request.path == "/videos/comments")
        return true;
    if (request.method == "GET" && request.path == "/videos/barrages")
        return true;
    if (request.method == "POST" && request.path == "/videos/barrages")
        return true;
    if (request.method == "POST" && request.path == "/transcode/jobs")
        return true;
    if (request.method == "GET" && request.path == "/transcode/jobs")
        return true;
    if (request.method == "POST" && request.path == "/transcode/jobs/retry")
        return true;
    return false;
}
bool forwardBusiness(const std::string &endpoint, int timeoutMs,
                     const httplib::Request &request,
                     const std::optional<std::string> &account,
                     const std::string &requestId, ForwardResponse &response,
                     std::string &error) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeoutMs;
    options.connect_timeout_ms = timeoutMs;
    options.max_retry = 0;
    const auto address =
        endpoint.rfind("http://", 0) == 0 ? endpoint.substr(7) : endpoint;
    if (channel.Init(address.c_str(), &options) != 0) {
        error = "cannot initialize business RPC channel";
        return false;
    }
    if (request.method == "POST" && request.path == "/login")
        return callBusiness<vod::business::LoginRequest,
                            vod::business::LoginResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.Login", &vod::business::UserOperations_Stub::Login,
            false, nullptr);
    if (request.method == "POST" && request.path == "/login/password")
        return callBusiness<vod::business::PasswordLoginRequest,
                            vod::business::PasswordLoginResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.PasswordLogin",
            &vod::business::UserOperations_Stub::PasswordLogin, false, nullptr);
    if (request.method == "POST" && request.path == "/login/email-code")
        return callBusiness<vod::business::SendEmailCodeRequest,
                            vod::business::SendEmailCodeResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.SendEmailCode",
            &vod::business::UserOperations_Stub::SendEmailCode, false, nullptr);
    if (request.method == "POST" && request.path == "/login/email")
        return callBusiness<vod::business::EmailLoginRequest,
                            vod::business::EmailLoginResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.EmailLogin",
            &vod::business::UserOperations_Stub::EmailLogin, false, nullptr);
    if (request.method == "POST" && request.path == "/logout")
        return callBusiness<vod::business::LogoutRequest,
                            vod::business::LogoutResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.Logout",
            &vod::business::UserOperations_Stub::Logout, false, nullptr);
    if (request.method == "GET" && request.path == "/users/profile")
        return callBusiness<vod::business::GetProfileRequest,
                            vod::business::GetProfileResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.GetProfile",
            &vod::business::UserOperations_Stub::GetProfile, false, nullptr);
    if (request.method == "POST" && request.path == "/users/profile")
        return callBusiness<vod::business::UpdateProfileRequest,
                            vod::business::UpdateProfileResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.UpdateProfile",
            &vod::business::UserOperations_Stub::UpdateProfile, false, nullptr);
    if (request.method == "POST" && request.path == "/users/avatar")
        return callBusiness<vod::business::UploadAvatarRequest,
                            vod::business::UploadAvatarResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.UploadAvatar",
            &vod::business::UserOperations_Stub::UploadAvatar, true, nullptr);
    if (request.method == "GET" && request.path == "/admin/users")
        return callBusiness<vod::business::ListUsersRequest,
                            vod::business::ListUsersResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.ListUsers",
            &vod::business::UserOperations_Stub::ListUsers, false, "users");
    if (request.method == "POST" && request.path == "/admin/users/action")
        return callBusiness<vod::business::UpdateUserRequest,
                            vod::business::UpdateUserResponse,
                            vod::business::UserOperations_Stub>(
            channel, request, account, requestId, response, error,
            "UserOperations.UpdateUser",
            &vod::business::UserOperations_Stub::UpdateUser, false, nullptr);
    if (request.method == "GET" && request.path == "/videos")
        return callBusiness<vod::business::ListVideosRequest,
                            vod::business::ListVideosResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.ListVideos",
            &vod::business::VideoOperations_Stub::ListVideos, false, "videos");
    if (request.method == "POST" && request.path == "/videos")
        return callBusiness<vod::business::CreateVideoRequest,
                            vod::business::CreateVideoResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.CreateVideo",
            &vod::business::VideoOperations_Stub::CreateVideo, false, nullptr);
    if (request.method == "POST" && request.path == "/videos/upload")
        return callBusiness<vod::business::UploadVideoRequest,
                            vod::business::UploadVideoResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.UploadVideo",
            &vod::business::VideoOperations_Stub::UploadVideo, true, nullptr);
    if (request.method == "GET" && request.path == "/videos/detail")
        return callBusiness<vod::business::GetDetailRequest,
                            vod::business::GetDetailResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.GetDetail",
            &vod::business::VideoOperations_Stub::GetDetail, false, nullptr);
    if (request.method == "GET" && request.path == "/videos/search")
        return callBusiness<vod::business::SearchRequest,
                            vod::business::SearchResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.Search",
            &vod::business::VideoOperations_Stub::Search, false, "videos");
    if (request.method == "GET" && request.path == "/videos/play-url")
        return callBusiness<vod::business::GetPlayUrlRequest,
                            vod::business::GetPlayUrlResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.GetPlayUrl",
            &vod::business::VideoOperations_Stub::GetPlayUrl, false, nullptr);
    if (request.method == "GET" && request.path == "/users/videos")
        return callBusiness<vod::business::ListOwnerVideosRequest,
                            vod::business::ListOwnerVideosResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.ListOwnerVideos",
            &vod::business::VideoOperations_Stub::ListOwnerVideos, false,
            "videos");
    if (request.method == "GET" && request.path == "/admin/reviews")
        return callBusiness<vod::business::ListReviewsRequest,
                            vod::business::ListReviewsResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.ListReviews",
            &vod::business::VideoOperations_Stub::ListReviews, false,
            "reviews");
    if (request.method == "POST" && request.path == "/admin/reviews/action")
        return callBusiness<vod::business::ReviewVideoRequest,
                            vod::business::ReviewVideoResponse,
                            vod::business::VideoOperations_Stub>(
            channel, request, account, requestId, response, error,
            "VideoOperations.ReviewVideo",
            &vod::business::VideoOperations_Stub::ReviewVideo, false, nullptr);
    if (request.method == "GET" && request.path == "/videos/like-status")
        return callBusiness<vod::business::GetLikeRequest,
                            vod::business::GetLikeResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.GetLike",
            &vod::business::InteractionOperations_Stub::GetLike, false,
            nullptr);
    if (request.method == "POST" && request.path == "/videos/like")
        return callBusiness<vod::business::LikeRequest,
                            vod::business::LikeResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.Like",
            &vod::business::InteractionOperations_Stub::Like, false, nullptr);
    if (request.method == "POST" && request.path == "/videos/unlike")
        return callBusiness<vod::business::UnlikeRequest,
                            vod::business::UnlikeResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.Unlike",
            &vod::business::InteractionOperations_Stub::Unlike, false, nullptr);
    if (request.method == "GET" && request.path == "/videos/watch-progress")
        return callBusiness<vod::business::GetProgressRequest,
                            vod::business::GetProgressResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.GetProgress",
            &vod::business::InteractionOperations_Stub::GetProgress, false,
            nullptr);
    if (request.method == "POST" && request.path == "/videos/watch-progress")
        return callBusiness<vod::business::SaveProgressRequest,
                            vod::business::SaveProgressResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.SaveProgress",
            &vod::business::InteractionOperations_Stub::SaveProgress, false,
            nullptr);
    if (request.method == "GET" && request.path == "/videos/favorite-status")
        return callBusiness<vod::business::GetFavoriteRequest,
                            vod::business::GetFavoriteResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.GetFavorite",
            &vod::business::InteractionOperations_Stub::GetFavorite, false,
            nullptr);
    if (request.method == "POST" && request.path == "/videos/favorite")
        return callBusiness<vod::business::FavoriteRequest,
                            vod::business::FavoriteResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.Favorite",
            &vod::business::InteractionOperations_Stub::Favorite, false,
            nullptr);
    if (request.method == "POST" && request.path == "/videos/unfavorite")
        return callBusiness<vod::business::UnfavoriteRequest,
                            vod::business::UnfavoriteResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.Unfavorite",
            &vod::business::InteractionOperations_Stub::Unfavorite, false,
            nullptr);
    if (request.method == "GET" && request.path == "/users/favorites")
        return callBusiness<vod::business::ListFavoritesRequest,
                            vod::business::ListFavoritesResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.ListFavorites",
            &vod::business::InteractionOperations_Stub::ListFavorites, false,
            "videos");
    if (request.method == "GET" && request.path == "/videos/comments")
        return callBusiness<vod::business::ListCommentsRequest,
                            vod::business::ListCommentsResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.ListComments",
            &vod::business::InteractionOperations_Stub::ListComments, false,
            "comments");
    if (request.method == "POST" && request.path == "/videos/comments")
        return callBusiness<vod::business::AddCommentRequest,
                            vod::business::AddCommentResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.AddComment",
            &vod::business::InteractionOperations_Stub::AddComment, false,
            nullptr);
    if (request.method == "GET" && request.path == "/videos/barrages")
        return callBusiness<vod::business::ListBarragesRequest,
                            vod::business::ListBarragesResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.ListBarrages",
            &vod::business::InteractionOperations_Stub::ListBarrages, false,
            "barrages");
    if (request.method == "POST" && request.path == "/videos/barrages")
        return callBusiness<vod::business::AddBarrageRequest,
                            vod::business::AddBarrageResponse,
                            vod::business::InteractionOperations_Stub>(
            channel, request, account, requestId, response, error,
            "InteractionOperations.AddBarrage",
            &vod::business::InteractionOperations_Stub::AddBarrage, false,
            nullptr);
    if (request.method == "POST" && request.path == "/transcode/jobs")
        return callBusiness<vod::business::SubmitJobRequest,
                            vod::business::SubmitJobResponse,
                            vod::business::TranscodeOperations_Stub>(
            channel, request, account, requestId, response, error,
            "TranscodeOperations.SubmitJob",
            &vod::business::TranscodeOperations_Stub::SubmitJob, false,
            nullptr);
    if (request.method == "GET" && request.path == "/transcode/jobs")
        return callBusiness<vod::business::GetJobRequest,
                            vod::business::GetJobResponse,
                            vod::business::TranscodeOperations_Stub>(
            channel, request, account, requestId, response, error,
            "TranscodeOperations.GetJob",
            &vod::business::TranscodeOperations_Stub::GetJob, false, nullptr);
    if (request.method == "POST" && request.path == "/transcode/jobs/retry")
        return callBusiness<vod::business::RetryJobRequest,
                            vod::business::RetryJobResponse,
                            vod::business::TranscodeOperations_Stub>(
            channel, request, account, requestId, response, error,
            "TranscodeOperations.RetryJob",
            &vod::business::TranscodeOperations_Stub::RetryJob, false, nullptr);
    error = "unknown business operation";
    return false;
}
} // namespace biterpc
