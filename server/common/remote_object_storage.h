#pragma once
#include "object_storage.h"
#include "etcd_registry.h"
namespace bitestorage {
// 文件 ID 由 FileService 分配；调用方不连接 FastDFS，也不依赖 FileService
// 的磁盘目录。
class RemoteObjectStorage final : public IObjectStorage {
  public:
    RemoteObjectStorage(biteconfig::RegistrySettings settings, int timeoutMs);
    bool start(std::string &error);
    bool put(const std::string &, const std::string &, const std::string &,
             StoredObject &, std::string &) override;
    bool get(const std::string &, std::string &, std::string &) override;
    bool remove(const std::string &, std::string &) override;

  private:
    bitesvc::ServiceRegistry registry_;
    bitesvc::EtcdServiceWatcher watcher_;
    int timeoutMs_;
};
} // namespace bitestorage
