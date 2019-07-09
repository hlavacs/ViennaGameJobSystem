#ifndef DBR_CC_THREAD_POOL_HPP
#define DBR_CC_THREAD_POOL_HPP

#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <vector>
#include <functional>
#include <condition_variable>
#include <queue>
#include <map>

class ThreadPool
{
public:
    using Ids = std::vector<std::thread::id>;
    // starts threadCount threads, waiting for jobs
    // may throw a std::system_error if a thread could not be started
    ThreadPool(std::size_t threadCount = 0);
    // non-copyable,
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    // but movable
    ThreadPool(ThreadPool&&) = default;
    ThreadPool& operator=(ThreadPool&&) = default;
    // clears job queue, then blocks until all threads are finished executing their current job
    ~ThreadPool();
    // add a function to be executed, along with any arguments for it
    template<typename Func, typename... Args>
    auto add(Func&& func, Args&&... args)->std::future<typename std::result_of<Func(Args...)>::type>;
    // returns number of threads being used
    std::size_t threadCount() const;
    // returns the number of jobs waiting to be executed
    std::size_t waitingJobs() const;
    // returns a vector of ids of the threads used by the ThreadPool
    Ids ids() const;
    // clears currently queued jobs (jobs which are not currently running)
    void clear();
    // pause and resume job execution. Does not affect currently running jobs
    void pause(bool state);
    // blocks calling thread until job queue is empty
    void wait();

	std::map<std::thread::id, uint32_t> threadNum;
private:
    using Job = std::function<void()>;
    // function each thread performs
    static void threadTask(ThreadPool* pool);
    std::queue<Job> jobs;
    mutable std::mutex jobsMutex;
    // notification variable for waiting threads
    std::condition_variable jobsAvailable;
    std::vector<std::thread> threads;
    std::atomic<std::size_t> threadsWaiting;
    std::atomic<bool> terminate;
    std::atomic<bool> paused;
};
template<typename Func, typename... Args>
auto ThreadPool::add(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type>
{
    using PackedTask = std::packaged_task<typename std::result_of<Func(Args...)>::type()>;
    auto task = std::make_shared<PackedTask>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    // get the future to return later
    auto ret = task->get_future();
    {
        std::lock_guard<std::mutex> lock{ jobsMutex };
        jobs.emplace([task]() { (*task)(); });
    }
    // let a waiting thread know there is an available job
    jobsAvailable.notify_one();
    return ret;
}

#endif
