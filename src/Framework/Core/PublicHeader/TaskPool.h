#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>


namespace Core {

// 任务句柄（添加任务时返回）
struct TaskHandle {
    uint64_t Id = 0;
    std::shared_future<void> Future;

    bool IsValid() const { return Future.valid(); }
    // 等待任务完成
    void Wait() const { if (Future.valid()) Future.wait(); }
    // 非阻塞检查
    bool IsCompleted() const { return Future.valid() && Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
};

// 简单的线程任务池，支持添加任务返回 TaskHandle，
// 可以等待单个或批量任务完成，也提供等待所有任务完成能力。
class CORE_API TaskPool {
public:
    // 创建一个线程池，threads==0会使用 hardware_concurrency() 或 1。
    explicit TaskPool(size_t threads = 0);
    ~TaskPool();

    // 添加单个任务，返回 TaskHandle （不会抛出）
    TaskHandle AddTask(std::function<void()> task);

    // 添加一批任务，返回对应 TaskHandle 数组
    std::vector<TaskHandle> AddTasks(const std::vector<std::function<void()>>& tasks);

    // 等待某个任务完成（阻塞）
    void Wait(const TaskHandle& handle) const;

    // 等待多个任务完成（阻塞）
    void WaitAll(const std::vector<TaskHandle>& handles) const;

    // 等待池中所有已提交任务完成（包括队列和当前执行的）
    void WaitAll() const;

    // 查询任务是否完成 / 等效于 handle.IsCompleted()
    bool IsCompleted(const TaskHandle& handle) const;

    // 获取当前仍未完成（已提交但未完成/正在执行）的任务数量
    size_t GetPendingTaskCount() const { return m_PendingTasks.load(); }

private:
    struct TaskItem {
        uint64_t Id;
        std::function<void()> Func;
        std::shared_ptr<std::promise<void>> PromisePtr;
    };

    // Worker loop
    void WorkerLoop();

    std::atomic<uint64_t> m_NextId{ 1 };
    std::atomic<size_t> m_PendingTasks{ 0 };

    mutable std::mutex m_QueueMutex;
    mutable std::condition_variable m_QueueCv;
    std::queue<TaskItem> m_Queue;

    std::vector<std::thread> m_Workers;
    std::atomic<bool> m_bShutdown{ false };

    // Used by WaitAll() to wait until PendingTasks == 0
    mutable std::condition_variable m_PendingCv;
};

} // namespace Common