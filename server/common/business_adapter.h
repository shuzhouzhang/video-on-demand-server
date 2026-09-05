#pragma once
#include "auth.h"
#include "util.h"
#include "business.pb.h"
#include <brpc/closure_guard.h>
#include <google/protobuf/util/json_util.h>

namespace biterpc {
inline Json::Value messageJson(const google::protobuf::Message &message) {
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    const auto status =
        google::protobuf::util::MessageToJsonString(message, &json, options);
    if (!status.ok())
        throw std::runtime_error("cannot serialize business DTO");
    return biteutil::JSON::unserialize(json).value_or(
        Json::Value(Json::objectValue));
}
inline bool fillMessage(const Json::Value &json,
                        google::protobuf::Message &message) {
    return google::protobuf::util::JsonStringToMessage(
               biteutil::JSON::serialize(json).value_or("{}"), &message)
        .ok();
}
// RPC 请求只携带领域字段。此处适配旧处理器的内存 DTO，不启动
// HTTP、不发回环请求。
template <class Request, class Response>
void invokeBusiness(const Request &input, Response &output,
                    const httplib::Server::Handler &handler, bool query,
                    bool multipart) {
    output.mutable_status()->set_request_id(input.context().request_id());
    if (!handler) {
        output.mutable_status()->set_code(503);
        return;
    }
    httplib::Request request;
    const auto payload = messageJson(input.payload());
    request.body = biteutil::JSON::serialize(payload).value_or("{}");
    if (query)
        for (const auto &key : payload.getMemberNames())
            request.params.emplace(key, payload[key].asString());
    biteauth::applyGatewayIdentity(
        request.headers, input.context().authenticated_account().empty()
                             ? std::nullopt
                             : std::optional<std::string>(
                                   input.context().authenticated_account()));
    request.headers.emplace("X-Request-Id", input.context().request_id());
    if (!input.context().session_token().empty())
        request.headers.emplace("Authorization",
                                "Bearer " + input.context().session_token());
    if (multipart) {
        request.headers.emplace("Content-Type",
                                "multipart/form-data; boundary=business-dto");
        httplib::MultipartFormData metadata;
        metadata.name = "metadata";
        metadata.content = request.body;
        request.files.emplace("metadata", metadata);
        if (payload.isMember("account")) {
            httplib::MultipartFormData account;
            account.name = "account";
            account.content = payload["account"].asString();
            request.files.emplace("account", account);
        }
        const auto *descriptor = input.GetDescriptor();
        const auto *reflection = input.GetReflection();
        for (int i = 2; i < descriptor->field_count(); ++i) {
            const auto *field = descriptor->field(i);
            if (!reflection->HasField(input, field))
                continue;
            const auto &part = reflection->GetMessage(input, field);
            const auto *pr = part.GetReflection();
            const auto *pd = part.GetDescriptor();
            httplib::MultipartFormData file;
            file.name = field->name();
            file.filename =
                pr->GetString(part, pd->FindFieldByName("filename"));
            file.content = pr->GetString(part, pd->FindFieldByName("content"));
            file.content_type =
                pr->GetString(part, pd->FindFieldByName("contentType"));
            request.files.emplace(file.name, std::move(file));
        }
    }
    httplib::Response response;
    handler(request, response);
    auto result = biteutil::JSON::unserialize(response.body);
    if (result && result->isArray()) {
        Json::Value list;
        list["success"] = true;
        list["videos"] = *result;
        result = list;
    }
    if (!result || !fillMessage(*result, *output.mutable_result())) {
        output.mutable_result()->Clear();
        output.mutable_result()->set_success(false);
        output.mutable_result()->set_message(
            "business response schema mismatch");
        output.mutable_status()->set_code(500);
    } else
        output.mutable_status()->set_code(
            response.status < 0 ? 200 : response.status);
}
} // namespace biterpc
