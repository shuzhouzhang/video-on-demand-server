#include "svc_worker.h"

#include <utility>

namespace svc_transcode {

SvcWorker::SvcWorker(std::size_t threadCount) {
    const std::size_t safeThreadCount = threadCount == 0 ? 1 : threadCount;
    threads_.reserve(safeThreadCount);
    for (std::size_t i = 0; i < safeThreadCount; ++i) {
        threads_.emplace_back(&SvcWorker::threadEntry, this);
    }
}

SvcWorker::~SvcWorker() {
    stop();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

bool SvcWorker::addTask(Task task, std::string& error) {
    if (!task) {
        error = "转码任务不能为空";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            error = "转码 worker 已停止";
            return false;
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void SvcWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

std::size_t SvcWorker::pendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void SvcWorker::threadEntry() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopped_ || !tasks_.empty(); });
            if (stopped_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

}  // namespace svc_transcode
