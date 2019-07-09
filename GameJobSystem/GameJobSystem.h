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
#include <iterator>
#include <algorithm>


namespace gjs {

	class JobMemory;
	class Job;
	class ThreadPool;


	//-------------------------------------------------------------------------------
	class Job {
		friend JobMemory;

	private:
		std::atomic<bool>		m_permanent;					//if true, do not reuse this job

		using PackedTask = std::packaged_task<void()>;
		std::shared_ptr<PackedTask>	m_packagedTask;				//the packaged task to carry out
		Job *					m_parentJob = nullptr;			//parent job, called if this job finishes
		std::atomic<uint32_t>	m_numUnfinishedChildren = 0;	//number of unfinished jobs
		Job *					m_onFinishedJob;				//job to schedule once this job finshes
		
	public:
		Job() : m_permanent(false), m_onFinishedJob(nullptr) {};
		~Job() {};

		//---------------------------------------------------------------------------
		//reset a fresh job taken from the JobMemory
		void reset() {						
			m_permanent = false;
			m_parentJob = nullptr;
			m_numUnfinishedChildren = 0;
			m_onFinishedJob = nullptr;
		};

		//---------------------------------------------------------------------------
		//set pointer to parent job
		void setParentJob(Job *pJob) { 		
			if (pJob == nullptr) return;
			m_parentJob = pJob; 
			pJob->m_numUnfinishedChildren++;
		};	

		//---------------------------------------------------------------------------
		//set job to execute when this job has finished
		void setOnFinished(Job *pJob) { 	
			m_onFinishedJob = pJob; 
		};

		//---------------------------------------------------------------------------
		// create a new job - do not schedule it yet
		template<typename Func, typename... Args>
		auto bindTask(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type> {
			using PackedTask = std::packaged_task<typename std::result_of<Func(Args...)>::type()>;
			m_packagedTask = std::make_shared<PackedTask>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
			auto ret = m_packagedTask->get_future();
			return ret;
		}

		//---------------------------------------------------------------------------
		//notify parent, or schedule the finished job, define later since do not know ThreadPool yet
		void onFinished();	

		//---------------------------------------------------------------------------
		void childFinished() {
			uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

		//---------------------------------------------------------------------------
		//run the packaged task
		void operator()() {									
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
			m_jobLists.reserve(1000);							//reserve enough that it will never be expanded
			m_jobLists.emplace_back(new JobList(m_listLength));	//init with 1 segment
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
		//get a new empty job from the job memory - if necessary add another job list
		Job * getNextJob() {
			const uint32_t index = m_jobIndex.fetch_add(1);			
			if (index > m_jobLists.size() * m_listLength - 1) {
				static std::mutex lmutex;								//only lock if appending the job list
				std::lock_guard<std::mutex> lock(lmutex);

				if (index > m_jobLists.size() * m_listLength - 1)		//might be beaten here by other thread so check again
					m_jobLists.emplace_back(new JobList(m_listLength));
			}

			return &(*m_jobLists[index / m_listLength])[index % m_listLength];		//get modulus of number of last job list
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
		//reset index for new frame, start with 0 again
		void reset() { m_jobIndex = 0; };				
	};


	class JobQueue {
	public:
		JobQueue() {};
		void push( Job * pJob) {
		};
		Job * pop() {
			return nullptr;
		};
		Job *steal() {
			return nullptr;
		};
	};


	//---------------------------------------------------------------------------
	class ThreadPool {

		using Ids = std::vector<std::thread::id>;

	private:
		std::vector<std::thread> threads;
		std::map<std::thread::id, uint32_t> threadIndexMap;
		std::atomic<bool> terminate;

		std::vector<JobQueue*> jobQueues;
		std::condition_variable jobsAvailable;
	


	public:

		//---------------------------------------------------------------------------
		//instance and private constructor
		static ThreadPool *pInstance;
		ThreadPool(std::size_t threadCount = 0) : terminate(false) {
			pInstance = this;

			if (threadCount == 0) {
				threadCount = std::thread::hardware_concurrency() - 1;	//main thread is also running
			}

			threads.reserve(threadCount + 1);
			jobQueues.resize(threadCount + 1);
			for (uint32_t i = 0; i < threadCount; i++) {
				threads.push_back( std::thread( &ThreadPool::threadTask, this ) );
			}
		};

		//---------------------------------------------------------------------------
		static ThreadPool * getInstance(std::size_t threadCount = 0) {
			if (pInstance == nullptr) pInstance = new ThreadPool(threadCount);
			return pInstance;
		};

		ThreadPool(const ThreadPool&) = delete;				// non-copyable,
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = default;					// but movable
		ThreadPool& operator=(ThreadPool&&) = default;
		~ThreadPool() {};

		//---------------------------------------------------------------------------
		// function each thread performs
		void threadTask() {
			static std::atomic<uint32_t> threadIndexCounter = 0;

			uint32_t threadIndex = threadIndexCounter.fetch_add(1);
			jobQueues[threadIndex] = new JobQueue();
			threadIndexMap[std::thread::id()] = threadIndex;

			while (true) {

				if (terminate) break;

				Job * pJob = jobQueues[threadIndex]->pop();

				if (terminate) break;

				if (pJob == nullptr && threads.size()>1) {
					uint32_t idx = std::rand() % threads.size();
					if (idx != threadIndex) pJob = jobQueues[idx]->steal();
				}

				if (terminate) break;

				if (pJob != nullptr) {
					(*pJob)();
				}
				else {
					std::this_thread::sleep_for(std::chrono::microseconds(100));
				};
			}
		};

		//---------------------------------------------------------------------------
		// returns number of threads being used
		std::size_t getThreadCount() const { return threads.size(); };	

		//---------------------------------------------------------------------------
		//return number of this thread
		uint32_t getThreadNumber() {						
			for (uint32_t i = 0; i < threads.size(); i++) {
				if (std::thread::id() == threads[i].get_id()) return i;
			}
			return 0;
		};

		//---------------------------------------------------------------------------
		//get index of the thread that is calling this function
		uint32_t getThreadIndex() {
			return threadIndexMap[std::thread::id()];
		};

		//---------------------------------------------------------------------------
		//add a job to the thread pool
		void addJob( Job* pJob) {

		};

	};


}


#ifdef IMPLEMENT_GAMEJOBSYSTEM

namespace gjs {

	JobMemory * JobMemory::m_pJobMemory = nullptr;
	ThreadPool * ThreadPool::pInstance = nullptr;


	void Job::onFinished() {
		m_permanent = false;					//job is finished so it can be reused
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

