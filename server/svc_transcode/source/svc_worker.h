#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace svc_transcode {

class SvcWorker {
public:
    using Task = std::function<void()>;

    explicit SvcWorker(std::size_t threadCount = 1);
    ~SvcWorker();

    SvcWorker(const SvcWorker&) = delete;
    SvcWorker& operator=(const SvcWorker&) = delete;

    bool addTask(Task task, std::string& error);
    void stop();
    std::size_t pendingTasks() const;

private:
    void threadEntry();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
    std::queue<Task> tasks_;
    std::vector<std::thread> threads_;
};

}  // namespace svc_transcode
