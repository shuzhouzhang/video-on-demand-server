#include "svc_data.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

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
    const std::string prefix =
        std::to_string(std::time(nullptr)) + "-" + safeSegment(originalName);
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
