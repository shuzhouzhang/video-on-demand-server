#include "../../server/common/object_storage.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    std::cout << "[PASS] " << message << '\n';
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "vod-object-storage-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    auto storage = bitestorage::makeLocalObjectStorage(root.string());
    bitestorage::StoredObject stored;
    std::string error;
    const std::string payload("video\0payload", 13);
    ok &= expect(storage->put("source", "../unsafe name.mp4", payload,
                              stored, error),
                 "store binary object under managed root");
    ok &= expect(stored.storageGroup == "local" &&
                     stored.sizeBytes == payload.size() &&
                     stored.locator.rfind("source/", 0) == 0,
                 "return stable local object metadata");

    std::string downloaded;
    ok &= expect(storage->get(stored.locator, downloaded, error) &&
                     downloaded == payload,
                 "download exact binary object");
    ok &= expect(!storage->get("../outside", downloaded, error),
                 "reject object traversal outside managed root");
    ok &= expect(storage->remove(stored.locator, error),
                 "remove stored object");
    ok &= expect(!storage->get(stored.locator, downloaded, error),
                 "removed object is no longer readable");

    std::filesystem::remove_all(root, ec);
    return ok ? 0 : 1;
}
