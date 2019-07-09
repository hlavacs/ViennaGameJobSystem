#include "ThreadPool.h"

#include <iterator>
#include <algorithm>

ThreadPool::ThreadPool(std::size_t threadCount)
    : threadsWaiting(0),
    terminate(false),
    paused(false)
{
    if (threadCount==0)
        threadCount = std::thread::hardware_concurrency();
    // prevent potential reallocation, thereby screwing up all our hopes and dreams
    threads.reserve(threadCount);
    std::generate_n(std::back_inserter(threads), threadCount, [this]() { return std::thread{ threadTask, this }; });
	for (uint32_t i = 0; i < threads.size(); i++) {
		threadNum[threads[i].get_id()] = i;
	}
}
ThreadPool::~ThreadPool()
{
    clear();
    // tell threads to stop when they can
    terminate = true;
    jobsAvailable.notify_all();
    // wait for all threads to finish
    for (auto& t : threads)
    {
        if (t.joinable())
            t.join();
    }
}
std::size_t ThreadPool::threadCount() const
{
    return threads.size();
}
std::size_t ThreadPool::waitingJobs() const
{
    std::lock_guard<std::mutex> jobLock(jobsMutex);
    return jobs.size();
}
ThreadPool::Ids ThreadPool::ids() const
{
    Ids ret(threads.size());
    std::transform(threads.begin(), threads.end(), ret.begin(), [](auto& t) { return t.get_id(); });
    return ret;
}
void ThreadPool::clear()
{
    std::lock_guard<std::mutex> lock{ jobsMutex };
    while (!jobs.empty())
        jobs.pop();
}
void ThreadPool::pause(bool state)
{
    paused = state;
    if (!paused)
        jobsAvailable.notify_all();
}
void ThreadPool::wait()
{
    // we're done waiting once all threads are waiting
    while (threadsWaiting != threads.size());
}
void ThreadPool::threadTask(ThreadPool* pool)
{
    // loop until we break (to keep thread alive)
    while (true)
    {
        // if we need to finish, let's do it before we get into
        // all the expensive synchronization stuff
        if (pool->terminate)
            break;
        std::unique_lock<std::mutex> jobsLock{ pool->jobsMutex };
        // if there are no more jobs, or we're paused, go into waiting mode
        if (pool->jobs.empty() || pool->paused)
        {
            ++pool->threadsWaiting;
            pool->jobsAvailable.wait(jobsLock, [&]()
            {
                return pool->terminate || !(pool->jobs.empty() || pool->paused);
            });
            --pool->threadsWaiting;
        }
        // check once more before grabbing a job, since we want to stop ASAP
        if (pool->terminate)
            break;
        // take next job
        auto job = std::move(pool->jobs.front());
        pool->jobs.pop();
        jobsLock.unlock();
		job();
    }
}
