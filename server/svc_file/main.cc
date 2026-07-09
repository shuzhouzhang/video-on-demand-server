#include "../common/bitelog.h"
#include "../common/config.h"
#include "../common/util.h"

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

std::string pathFileName(const std::string& filename) {
    return std::filesystem::path(filename).filename().string();
}

std::string safeSegment(std::string value) {
    for (char& ch : value) {
        const bool safe = std::isalnum(static_cast<unsigned char>(ch)) ||
            ch == '-' || ch == '_';
        if (!safe) {
            ch = '_';
        }
    }
    return value.empty() ? "misc" : value;
}

bool writeBinaryFile(const std::filesystem::path& path,
                     const std::string& content,
                     std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "创建目录失败: " + ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "打开文件失败: " + path.string();
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        error = "写入文件失败: " + path.string();
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string configPath =
        argc > 1 ? argv[1] : "conf/file_service.local.json";

    std::string error;
    const auto settings = biteconfig::Config::load(configPath, error);
    if (!settings) {
        std::cerr << "file_service 启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    httplib::Server server;
    std::error_code ignored;
    std::filesystem::create_directories("uploads", ignored);
    server.set_mount_point("/uploads", "uploads");

    server.Get("/health", [](const httplib::Request&,
                             httplib::Response& response) {
        Json::Value body;
        body["code"] = 0;
        body["message"] = "ok";
        body["data"]["status"] = "UP";
        setJsonResponse(response, 200, body);
    });
    server.Get("/healthz", [](const httplib::Request&,
                              httplib::Response& response) {
        Json::Value body;
        body["success"] = true;
        body["service"] = "file_service";
        body["status"] = "ok";
        setJsonResponse(response, 200, body);
    });
    server.Post("/files/upload", [](const httplib::Request& request,
                                    httplib::Response& response) {
        Json::Value body;
        constexpr std::size_t MAX_FILE_BYTES = 200 * 1024 * 1024;
        if (!request.is_multipart_form_data() || !request.has_file("file")) {
            body["success"] = false;
            body["message"] = "上传格式必须包含 file 字段";
            setJsonResponse(response, 200, body);
            return;
        }

        const auto file = request.get_file_value("file");
        const std::string originalName = pathFileName(file.filename);
        if (originalName.empty() || file.content.empty() ||
            file.content.size() > MAX_FILE_BYTES) {
            body["success"] = false;
            body["message"] = "文件不能为空或超过大小限制";
            setJsonResponse(response, 200, body);
            return;
        }

        std::string directory = "misc";
        if (request.has_file("directory")) {
            directory = safeSegment(request.get_file_value("directory").content);
        }
        const std::string prefix =
            std::to_string(std::time(nullptr)) + "-" + safeSegment(originalName);
        const std::filesystem::path storedPath =
            std::filesystem::path("uploads") / directory / prefix;

        std::string error;
        if (!writeBinaryFile(storedPath, file.content, error)) {
            body["success"] = false;
            body["message"] = "文件保存失败";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["message"] = "文件上传成功";
        body["storedPath"] = storedPath.generic_string();
        body["publicUrl"] = "/" + storedPath.generic_string();
        body["originalName"] = originalName;
        setJsonResponse(response, 200, body);
    });

    INF("file_service listening on 0.0.0.0:{}", settings->server.port);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("file_service failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}
