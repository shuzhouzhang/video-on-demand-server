#include "svc_data.h"
#include "../../common/session_token.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

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
    std::FILE* out = std::fopen(path.string().c_str(), "wbx");
    if (!out) {
        error = "打开文件失败: " + std::string(std::strerror(errno));
        return false;
    }
    const std::size_t written =
        std::fwrite(content.data(), 1, content.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != content.size() || !closed) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        error = "写入文件失败: " + path.string();
        return false;
    }
    return true;
}

}  // namespace

namespace svc_file {

bool FileDataFacade::storeUploadedFile(const std::string& directory,
                                       const std::string& filename,
                                       const std::string& content,
                                       StoredFile& stored,
                                       std::string& error) const {
    const std::string originalName = pathFileName(filename);
    if (originalName.empty() || content.empty() ||
        content.size() > maxFileBytes()) {
        error = "文件不能为空或超过大小限制";
        return false;
    }

    const std::string safeDirectory = safeSegment(directory);
    const std::filesystem::path originalPath(originalName);
    const std::string extension = originalPath.extension().string();
    const std::string safeName = safeSegment(originalPath.stem().string()) +
        extension;
    std::string uploadToken;
    if (!bitesession::generateSessionToken(uploadToken, error)) return false;
    const std::string prefix = uploadToken.substr(4, 32) + "-" + safeName;
    const std::filesystem::path storedPath =
        std::filesystem::path("uploads") / safeDirectory / prefix;

    if (!writeBinaryFile(storedPath, content, error)) {
        return false;
    }

    stored.originalName = originalName;
    stored.storedPath = storedPath.generic_string();
    stored.publicUrl = "/" + stored.storedPath;
    return true;
}

}  // namespace svc_file
