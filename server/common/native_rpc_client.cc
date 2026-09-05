#include "native_rpc_client.h"
#include "user.pb.h"
#include "video.pb.h"
#include "util.h"
#include "../data/video.h"
#include <brpc/channel.h>
#include <brpc/controller.h>

namespace biterpc {
namespace {
Json::Value videoJson(const vod::api::VideoInfo& input) {
    bitevideo::Video video;
    video.id=input.video_id(); video.title=input.title(); video.userName=input.uploader();
    video.date=input.published_on(); video.durationSeconds=input.duration_seconds();
    video.playCount=input.play_count(); video.likeCount=input.like_count();
    video.category=input.category(); video.description=input.description();
    video.tags.assign(input.tags().begin(),input.tags().end());
    return bitevideo::toJson(video);
}
void finish(ForwardResponse& response, int status, const Json::Value& body,
            const std::string& method, const std::string& requestId) {
    response.status=status;
    response.contentType="application/json; charset=utf-8";
    response.body=biteutil::JSON::serialize(body).value_or("{}");
    response.requestId=requestId;
    response.headers.emplace("X-Vod-Rpc-Method",method);
}
}
bool hasNativeRoute(const httplib::Request& request) {
    return (request.method=="POST" && (request.path=="/login" || request.path=="/login/password")) ||
        (request.method=="GET" && (request.path=="/users/profile" || request.path=="/videos" ||
          request.path=="/videos/detail" || request.path=="/videos/search"));
}
bool forwardNative(const std::string& endpoint, int timeoutMs,
    const httplib::Request& request, const std::optional<std::string>& account,
    const std::string& requestId, ForwardResponse& response, std::string& error) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol="baidu_std";
    options.timeout_ms=timeoutMs; options.connect_timeout_ms=timeoutMs;
    options.max_retry=0;
    std::string address=endpoint;
    if (address.rfind("http://",0)==0) address.erase(0,7);
    if (channel.Init(address.c_str(),&options)!=0) { error="cannot initialize native RPC channel"; return false; }
    brpc::Controller controller;
    Json::Value body;
    auto setContext = [&](vod::api::RequestContext* context) {
        context->set_request_id(requestId);
        if (account) context->set_authenticated_account(*account);
    };
    if (request.method=="POST") {
        const auto json=biteutil::JSON::unserialize(request.body);
        if (!json || !json->isObject() ||
            (json->isMember("account") && !(*json)["account"].isString()) ||
            (json->isMember("password") && !(*json)["password"].isString())) {
            body["success"]=false; body["message"]="请求JSON格式错误";
            finish(response,200,body,"UserService.Login",requestId); return true;
        }
        vod::api::LoginRequest input; vod::api::LoginResponse output;
        input.set_account((*json)["account"].asString()); input.set_password((*json)["password"].asString());
        setContext(input.mutable_context());
        vod::api::UserService_Stub stub(&channel);
        stub.Login(&controller,&input,&output,nullptr);
        if (controller.Failed()) { error=controller.ErrorText(); return false; }
        body["success"]=output.success();
        if (output.success()) {
            body["account"]=output.user().account(); body["userName"]=output.user().user_name();
            if (!output.token().empty()) body["token"]=output.token();
        } else body["message"]=output.message();
        finish(response,output.status().code(),body,"UserService.Login",requestId);
    } else if (request.path=="/users/profile") {
        vod::api::GetProfileRequest input; vod::api::GetProfileResponse output;
        input.set_account(request.get_param_value("account")); setContext(input.mutable_context());
        vod::api::UserService_Stub stub(&channel);
        stub.GetProfile(&controller,&input,&output,nullptr);
        if (controller.Failed()) { error=controller.ErrorText(); return false; }
        body["success"]=output.success();
        if (output.success()) {
            auto& user=body["user"]; user["account"]=output.user().account();
            user["userName"]=output.user().user_name(); user["avatarPath"]=output.user().avatar_url();
            user["description"]=output.user().description();
        } else body["message"]=output.message();
        finish(response,output.status().code(),body,"UserService.GetProfile",requestId);
    } else if (request.path=="/videos/detail") {
        vod::api::VideoDetailRequest input; vod::api::VideoDetailResponse output;
        input.set_video_id(request.get_param_value("id")); setContext(input.mutable_context());
        vod::api::VideoService_Stub stub(&channel);
        stub.GetVideoDetail(&controller,&input,&output,nullptr);
        if (controller.Failed()) { error=controller.ErrorText(); return false; }
        body["success"]=output.success();
        if (output.success()) body["video"]=videoJson(output.video()); else body["message"]=output.message();
        finish(response,output.status().code(),body,"VideoService.GetVideoDetail",requestId);
    } else {
        const bool search=request.path=="/videos/search";
        const std::string keyword=search ? request.get_param_value("keyword") : "";
        if (search && keyword.empty()) {
            body["success"]=false; body["message"]="搜索关键词不能为空";
            finish(response,200,body,"VideoService.ListVideos",requestId); return true;
        }
        vod::api::VideoListRequest input; vod::api::VideoListResponse output;
        input.set_keyword(keyword); setContext(input.mutable_context());
        vod::api::VideoService_Stub stub(&channel);
        stub.ListVideos(&controller,&input,&output,nullptr);
        if (controller.Failed()) { error=controller.ErrorText(); return false; }
        if (output.success()) {
            Json::Value videos(Json::arrayValue);
            for (const auto& video:output.videos()) videos.append(videoJson(video));
            if (search) {body["success"]=true;body["videos"]=videos;} else body=videos;
        } else {body["success"]=false;body["message"]=output.message();}
        finish(response,output.status().code(),body,"VideoService.ListVideos",requestId);
    }
    return true;
}
}
