#include "object_storage.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <utility>

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
extern "C" {
#include <fastdfs/fdfs_client.h>
#include <fastdfs/storage_client1.h>
#include <fastdfs/tracker_client.h>
#include <fastcommon/logger.h>
}

#include <cerrno>
#include <cstdlib>
#include <cstring>
#endif

namespace bitestorage {
namespace {

namespace fs = std::filesystem;

std::string safeSegment(std::string value) {
    for (char& character : value) {
        const unsigned char codeUnit = static_cast<unsigned char>(character);
        if (!std::isalnum(codeUnit) && character != '-' && character != '_' &&
            character != '.') {
            character = '_';
        }
    }
    return value.empty() ? "misc" : value;
}

bool inside(const fs::path& child, const fs::path& parent) {
    auto childPart = child.begin();
    for (auto parentPart = parent.begin(); parentPart != parent.end();
         ++parentPart, ++childPart) {
        if (childPart == child.end() || *childPart != *parentPart) return false;
    }
    return true;
}

class LocalObjectStorage final : public IObjectStorage {
public:
    explicit LocalObjectStorage(std::string root) : root_(std::move(root)) {}

    bool put(const std::string& directory,
             const std::string& originalName,
             const std::string& content,
             StoredObject& stored,
             std::string& error) override {
        const std::string filename = safeSegment(
            fs::path(originalName).filename().string());
        const std::string unique = std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        const fs::path relative = fs::path(safeSegment(directory)) /
            (unique + "-" + filename);
        const fs::path destination = fs::path(root_) / relative;
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            error = "cannot create local object directory: " + ec.message();
            return false;
        }
        std::ofstream stream(destination, std::ios::binary);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) {
            error = "cannot write local object";
            return false;
        }
        stored.locator = relative.generic_string();
        stored.storageGroup = "local";
        stored.remoteName = stored.locator;
        stored.sizeBytes = content.size();
        return true;
    }

    bool get(const std::string& locator, std::string& content,
             std::string& error) override {
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(fs::absolute(root_), ec);
        const fs::path path = fs::weakly_canonical(
            fs::absolute(fs::path(root_) / locator), ec);
        if (ec || !inside(path, root) || !fs::is_regular_file(path, ec)) {
            error = "local object not found";
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        content.assign(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
        if (!stream.good() && !stream.eof()) {
            error = "cannot read local object";
            return false;
        }
        return true;
    }

    bool remove(const std::string& locator, std::string& error) override {
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(fs::absolute(root_), ec);
        const fs::path path = fs::weakly_canonical(
            fs::absolute(fs::path(root_) / locator), ec);
        if (ec || !inside(path, root)) {
            error = "local object path is outside storage root";
            return false;
        }
        if (!fs::remove(path, ec) && ec) {
            error = "cannot remove local object: " + ec.message();
            return false;
        }
        return true;
    }

private:
    std::string root_;
};

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

std::mutex g_fastDfsMutex;

class FastDfsSession {
public:
    explicit FastDfsSession(const std::string& config) {
        log_init();
        g_log_context.log_level = LOG_ERR;
        ignore_signal_pipe();
        result_ = fdfs_client_init(config.c_str());
        if (result_ == 0) tracker_ = tracker_get_connection();
        if (result_ == 0 && !tracker_) {
            result_ = errno == 0 ? ECONNREFUSED : errno;
        }
    }

    ~FastDfsSession() {
        if (tracker_) tracker_close_connection_ex(tracker_, true);
        fdfs_client_destroy();
    }

    int result() const noexcept { return result_; }
    ConnectionInfo* tracker() const noexcept { return tracker_; }

private:
    int result_ = 0;
    ConnectionInfo* tracker_ = nullptr;
};

std::string extensionOf(const std::string& name) {
    std::string extension = fs::path(name).extension().string();
    if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
    if (extension.size() > FDFS_FILE_EXT_NAME_MAX_LEN) extension.clear();
    return extension;
}

class FastDfsObjectStorage final : public IObjectStorage {
public:
    explicit FastDfsObjectStorage(biteconfig::FastDfsSettings settings)
        : settings_(std::move(settings)) {}

    bool put(const std::string&,
             const std::string& originalName,
             const std::string& content,
             StoredObject& stored,
             std::string& error) override {
        std::lock_guard<std::mutex> lock(g_fastDfsMutex);
        FastDfsSession session(settings_.clientConfig);
        if (session.result() != 0) return fail("initialize", session.result(), error);
        const std::string extension = extensionOf(originalName);
        ConnectionInfo storageServer;
        char groupName[FDFS_GROUP_NAME_MAX_LEN + 1] = {0};
        int storePathIndex = -1;
        int result = tracker_query_storage_store(
            session.tracker(), &storageServer, groupName, &storePathIndex);
        if (result != 0) return fail("locate storage", result, error);
        char fileId[256] = {0};
        result = storage_upload_by_filebuff1(
            session.tracker(), &storageServer, storePathIndex, content.data(),
            static_cast<std::int64_t>(content.size()),
            extension.empty() ? nullptr : extension.c_str(), nullptr, 0,
            groupName, fileId);
        if (result != 0) return fail("upload", result, error);
        stored.locator = fileId;
        const std::size_t slash = stored.locator.find('/');
        if (slash == std::string::npos) {
            error = "FastDFS returned an invalid file id";
            return false;
        }
        stored.storageGroup = stored.locator.substr(0, slash);
        stored.remoteName = stored.locator.substr(slash + 1);
        stored.sizeBytes = content.size();
        return true;
    }

    bool get(const std::string& locator, std::string& content,
             std::string& error) override {
        std::lock_guard<std::mutex> lock(g_fastDfsMutex);
        FastDfsSession session(settings_.clientConfig);
        if (session.result() != 0) return fail("initialize", session.result(), error);
        char* buffer = nullptr;
        std::int64_t size = 0;
        const int result = storage_download_file1(
            session.tracker(), nullptr, locator.c_str(), &buffer, &size);
        if (result != 0) return fail("download", result, error);
        content.assign(buffer, static_cast<std::size_t>(size));
        std::free(buffer);
        return true;
    }

    bool remove(const std::string& locator, std::string& error) override {
        std::lock_guard<std::mutex> lock(g_fastDfsMutex);
        FastDfsSession session(settings_.clientConfig);
        if (session.result() != 0) return fail("initialize", session.result(), error);
        const int result = storage_delete_file1(
            session.tracker(), nullptr, locator.c_str());
        return result == 0 || fail("delete", result, error);
    }

private:
    static bool fail(const char* operation, int result, std::string& error) {
        error = std::string("FastDFS ") + operation + " failed: " +
            std::strerror(result) + " (" + std::to_string(result) + ")";
        return false;
    }

    biteconfig::FastDfsSettings settings_;
};

#endif

}  // namespace

std::unique_ptr<IObjectStorage> makeLocalObjectStorage(
    const std::string& uploadRoot) {
    return std::make_unique<LocalObjectStorage>(uploadRoot);
}

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
std::unique_ptr<IObjectStorage> makeFastDfsObjectStorage(
    const biteconfig::FastDfsSettings& settings) {
    return std::make_unique<FastDfsObjectStorage>(settings);
}
#endif

}  // namespace bitestorage
