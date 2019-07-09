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

	class JobMemory;
	class Job;
	class ThreadPool;


	//-------------------------------------------------------------------------------
	class Job {
		friend JobMemory;

	private:
		bool					m_permanent;					//if true, do not reuse this job

		using PackedTask = std::packaged_task<void()>;
		std::shared_ptr<PackedTask>	m_packagedTask;				//the packaged task to carry out
		Job *					m_parentJob = nullptr;			//parent job, called if this job finishes
		std::atomic<uint32_t>	m_numUnfinishedChildren = 0;	//number of unfinished jobs
		Job *					m_onFinishedJob;				//job to schedule once this job finshes
		
	public:
		Job() : m_permanent(false), m_onFinishedJob(nullptr) {};
		~Job() {};

		void reset() {						//reset a fresh job taken from the JobMemory
			m_permanent = false;
			m_parentJob = nullptr;
			m_numUnfinishedChildren = 0;
			m_onFinishedJob = nullptr;
		};

		void setParentJob(Job *pJob) { 		//set pointer to parent job
			if (pJob == nullptr) return;
			m_parentJob = pJob; 
			pJob->m_numUnfinishedChildren++;
		};	

		void setOnFinished(Job *pJob) { 	//set job to execute when this job has finished
			m_onFinishedJob = pJob; 
		};

		// create a new job - do not schedule it yet
		template<typename Func, typename... Args>
		auto bindTask(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type> {
			using PackedTask = std::packaged_task<typename std::result_of<Func(Args...)>::type()>;
			m_packagedTask = std::make_shared<PackedTask>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
			auto ret = m_packagedTask->get_future();
			return ret;
		}

		void onFinished();	//notify parent, or schedule the finished job, define later since do not know ThreadPool yet

		void childFinished() {
			uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

		void operator()() {									//run the packaged task
			if (m_packagedTask == nullptr) return;
			m_numUnfinishedChildren = 1;					//number of children includes itself
			(*m_packagedTask)();
			uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

	};


	//---------------------------------------------------------------------------
	class JobMemory {
		friend ThreadPool;

	private:
		//transient jobs, will be deleted for each new frame
		const static std::uint32_t	m_listLength = 4096;		//length of a segment
		std::atomic<uint32_t>		m_jobIndex = 0;				//current index (number of jobs so far)
		using JobList = std::vector<Job>;
		std::vector<JobList*>		m_jobLists;					//list of transient segments

		static JobMemory *m_pJobMemory;							//pointer to singleton
		JobMemory() : m_jobIndex(0) {							//private constructor
			m_jobLists.reserve(1000);
			m_jobLists.emplace_back(new JobList(m_listLength));		//init with 1 segment
		};
	
	public:
		//---------------------------------------------------------------------------
		//get instance of singleton
		static JobMemory *getInstance() {						
			if (m_pJobMemory == nullptr) {
				m_pJobMemory = new JobMemory();					//create the singleton
			}
			return m_pJobMemory;
		};

		//---------------------------------------------------------------------------
		//get current index, then incr by 1
		Job * getNextJob() {
			const uint32_t index = m_jobIndex.fetch_add(1);			
			if (index > m_jobLists.size() * m_listLength - 1) {
				static std::mutex lmutex;								//only lock if appending the job list
				std::lock_guard<std::mutex> lock(lmutex);

				if (index > m_jobLists.size() * m_listLength - 1) //might be beaten here by other thread so check again
					m_jobLists.emplace_back(new JobList(m_listLength));
			}

			return &(*m_jobLists.back())[index % m_listLength];		//get modulus of number of last job list
		}

		//---------------------------------------------------------------------------
		//get the first job that is available
		Job* allocateTransientJob( Job *pParent = nullptr ) {
			Job *pJob;
			do {
				pJob = getNextJob();
			} while ( pJob->m_permanent );
			pJob->reset();
			if( pParent != nullptr ) pJob->setParentJob(pParent);
			return pJob;
		};


		//---------------------------------------------------------------------------
		//allocate a job that will not go out of context after the next reset
		Job* allocatePermanentJob(Job *pParent = nullptr) {
			Job *pJob = allocateTransientJob( pParent );
			pJob->m_permanent = true;
			return pJob;
		};

		//---------------------------------------------------------------------------
		//allocate a job that will not go out of context
		void deallocatePermanentJob( Job *pJob) {
			pJob->m_permanent = false;
		}

		void reset() { m_jobIndex = 0; };				//reset index for new frame, start with 0 again
	};



	//---------------------------------------------------------------------------
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


	void Job::onFinished() {
		if (m_parentJob != nullptr) {
			m_parentJob->childFinished();
		}
		if (m_onFinishedJob != nullptr) {
			ThreadPool::getInstance()->addJob(m_onFinishedJob);
		}
	}
}

#elif

extern JobMemory * JobMemory::m_pJobMemory = nullptr;
extern ThreadPool * ThreadPool::pInstance = nullptr;


#endif

