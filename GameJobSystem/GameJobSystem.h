#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <vector>
#include <functional>
#include <condition_variable>
#include <queue>
#include <map>
#include <set>


namespace gjs {

	class Job {
	private:
		using PackedTask = std::packaged_task<void()>;

		std::shared_ptr<PackedTask>	m_packagedTask;
		Job *					m_parentJob = nullptr;
		std::atomic<uint32_t>	m_numUnfinishedChildren = 0;
		Job *					m_onFinished;

	public:
		Job() {};
		~Job() {};

		void setParentJob(Job *pJob) { m_parentJob = pJob; };
		void setOnFinished(Job *pJob) { m_onFinished = pJob; };

		// create a new job - do not schedule it yet
		template<typename Func, typename... Args>
		auto bindTask(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type> {
			using PackedTask = std::packaged_task<typename std::result_of<Func(Args...)>::type()>;
			m_packagedTask = std::make_shared<PackedTask>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
			auto ret = m_packagedTask->get_future();
			return ret;
		}

		void onFinished();

		void childFinished() {
			uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

		void operator()() {
			if (m_packagedTask == nullptr) return;
			(*m_packagedTask)();
		};

	};


	class JobMemory {
	private:
		using JobList = std::vector<Job>;

		std::atomic<uint32_t> m_jobIndex = 0;
		std::vector<JobList*> m_jobLists;
		const static std::uint32_t m_listLength = 4096;

		static JobMemory *m_pJobMemory;
		JobMemory() : m_jobIndex(0) {
			m_jobLists.push_back(new JobList(m_listLength));
		};

	public:

		static JobMemory *getInstance() { 
			if (m_pJobMemory == nullptr) {
				m_pJobMemory = new JobMemory();
			}
			return m_pJobMemory;
		};

		Job* allocateJob( ) {
			const uint32_t index = m_jobIndex.fetch_add(1);
			if (index > m_jobLists.size() * m_listLength - 1) {
				m_jobLists.push_back(new JobList(m_listLength));
			}

			return &(*m_jobLists.back())[index % m_listLength];		//get modulus of number of last job list
		};

		void reset() { m_jobIndex = 0; };
	};


	class ThreadPool {

		using Ids = std::vector<std::thread::id>;

	private:
		std::vector<std::thread> threads;
		std::map<std::thread::id, uint32_t> threadNum;
		std::queue<Job> jobs;
		std::condition_variable jobsAvailable;
	
		// function each thread performs
		static void threadTask(ThreadPool* pool) {

		};

		static ThreadPool *pInstance;
		ThreadPool(std::size_t threadCount = 0) {

		};

	public:
		static ThreadPool * getInstance(std::size_t threadCount = 0) {
			if (pInstance == nullptr) pInstance = new ThreadPool(threadCount);
			return pInstance;
		};

		ThreadPool(const ThreadPool&) = delete;				// non-copyable,
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = default;					// but movable
		ThreadPool& operator=(ThreadPool&&) = default;
		
		// clears job queue, then blocks until all threads are finished executing their current job
		~ThreadPool() {	
		};

		std::size_t getThreadCount() const { return threads.size(); };	// returns number of threads being used

		uint32_t getThreadNumber() {						//return number of this thread
			for (uint32_t i = 0; i < threads.size(); i++) {
				if (std::thread::id() == threads[i].get_id()) return i;
			}
			return 0;
		};

		void addJob( Job* pJob) {

		};

		std::size_t waitingJobs() const;		// returns the number of jobs waiting to be executed#
		void clear();							// clears currently queued jobs (jobs which are not currently running)
		void pause(bool state);					// pause and resume job execution. Does not affect currently running jobs
		void wait();							// blocks calling thread until job queue is empty


	};


}


#ifdef IMPLEMENT_GAMEJOBSYSTEM

namespace gjs {

	JobMemory * JobMemory::m_pJobMemory = nullptr;
	ThreadPool * ThreadPool::pInstance = nullptr;

	void gjs::Job::onFinished() {
		if (m_parentJob != nullptr) {
			m_parentJob->childFinished();
		}
		if (m_onFinished != nullptr) {
			ThreadPool::getInstance()->addJob(m_onFinished);
		}
	}
}

#elif

extern JobMemory * JobMemory::m_pJobMemory = nullptr;
extern ThreadPool * ThreadPool::pInstance = nullptr;


#endif

