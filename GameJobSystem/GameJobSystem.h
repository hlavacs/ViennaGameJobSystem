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
#include <assert.h>


namespace gjs {

	class JobMemory;
	class Job;
	class ThreadPool;


	using Function = std::function<void()>;

	//-------------------------------------------------------------------------------
	class Job {
		friend JobMemory;
		friend ThreadPool;

	private:
		std::atomic<bool>		m_available;					//is this job available after a pool reset?
		std::atomic<uint32_t>	m_poolNumber;					//number of pool this job comes from

		std::shared_ptr<Function> m_function;					//the function to carry out
		Job *					m_parentJob;					//parent job, called if this job finishes
		std::atomic<uint32_t>	m_numUnfinishedChildren;		//number of unfinished jobs
		Job *					m_onFinishedJob;				//job to schedule once this job finshes
		
		//---------------------------------------------------------------------------
		//reset a fresh job taken from the JobMemory
		void reset( uint32_t poolNumber ) {				
			m_poolNumber = poolNumber;
			m_available = false;
			m_parentJob = nullptr;
			m_numUnfinishedChildren = 0;
			m_onFinishedJob = nullptr;
		};

		//---------------------------------------------------------------------------
		//set pointer to parent job
		void setParentJob(Job *parentJob) {
			if (parentJob == nullptr) return;
			m_parentJob = parentJob;
			parentJob->m_numUnfinishedChildren++;
		};	

		//---------------------------------------------------------------------------
		//set job to execute when this job has finished
		void setOnFinished(Job *pJob) { 	
			m_onFinishedJob = pJob; 
		};

		void setFunction(std::shared_ptr<Function> func) {
			m_function = func;
		};

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
			if (m_function == nullptr) return;
			m_numUnfinishedChildren = 1;					//number of children includes itself
			(*m_function)();								//call the function
			uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

	public:
		Job() : m_poolNumber(0), m_parentJob(nullptr), m_numUnfinishedChildren(0), m_onFinishedJob(nullptr) {};
		~Job() {};

	};


	//---------------------------------------------------------------------------
	class JobMemory {
		friend ThreadPool;

		//transient jobs, will be deleted for each new frame
		const static std::uint32_t	m_listLength = 4096;		//length of a segment

		using JobList = std::vector<Job>;
		struct JobPool {
			std::atomic<uint32_t> jobIndex = 0;
			std::vector<JobList*> jobLists;
			std::mutex lmutex;							//only lock if appending the job list

			JobPool() : jobIndex(0) {
				jobLists.reserve(500);
				jobLists.emplace_back(new JobList(m_listLength));
			};
		};

	private:
		std::vector<JobPool*> m_jobPools;						//a list with pointers to the job pools

		static JobMemory *m_pJobMemory;							//pointer to singleton
		JobMemory() {											//private constructor
			m_jobPools.reserve(10);
			m_jobPools.emplace_back( new JobPool );
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
		Job * getNextJob( uint32_t poolNumber ) {
			if (poolNumber > m_jobPools.size() - 1) {								//if pool does not exist yet
				static std::mutex mutex;
				std::lock_guard<std::mutex> lock(mutex);

				if (poolNumber > m_jobPools.size() - 1) {							//could be beaten here by other thread
					for (uint32_t i = m_jobPools.size(); i <= poolNumber; i++) {	//so check again
						m_jobPools.push_back(new JobPool);
					}
				}
			}

			JobPool *pPool = m_jobPools[poolNumber];
			const uint32_t index = pPool->jobIndex.fetch_add(1);
			if (index > pPool->jobLists.size() * m_listLength - 1) {
				std::lock_guard<std::mutex> lock(pPool->lmutex);

				if (index > pPool->jobLists.size() * m_listLength - 1)		//might be beaten here by other thread so check again
					pPool->jobLists.emplace_back(new JobList(m_listLength));
			}

			return &(*pPool->jobLists[index / m_listLength])[index % m_listLength];		//get modulus of number of last job list
		}

		//---------------------------------------------------------------------------
		//get the first job that is available
		Job* allocateJob( Job *pParent = nullptr, uint32_t poolNumber = 0  ) {
			Job *pJob;
			do {
				pJob = getNextJob(poolNumber);
			} while ( !pJob->m_available );
			pJob->reset( poolNumber );
			if( pParent != nullptr ) pJob->setParentJob(pParent);
			return pJob;
		};

		//---------------------------------------------------------------------------
		//reset index for new frame, start with 0 again
		void reset( uint32_t poolNumber ) { 
			if (poolNumber > m_jobPools.size() - 1) return;
			m_jobPools[poolNumber]->jobIndex = 0;
		};
	};


	//---------------------------------------------------------------------------
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
		std::vector<Job*> jobPointers;
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
			jobPointers.resize(threadCount + 1);
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
			jobPointers[threadIndex] = nullptr;

			while (true) {

				if (terminate) break;

				Job * pJob = jobQueues[threadIndex]->pop();

				if (terminate) break;

				uint32_t max = 5;
				while (pJob == nullptr && threads.size()>1) {
					uint32_t idx = std::rand() % threads.size();
					if (idx != threadIndex) pJob = jobQueues[idx]->steal();
					if (!--max) break;
				}

				if (terminate) break;

				if (pJob != nullptr) {
					jobPointers[threadIndex] = pJob;	//make pointer to the Job structure accessible
					(*pJob)();							//run the job
				}
				else {
					std::this_thread::sleep_for(std::chrono::microseconds(100));
				};
			}
		};

		//---------------------------------------------------------------------------
		// returns number of threads being used
		std::size_t getThreadCount() const { return threadIndexMap.size(); };

		//---------------------------------------------------------------------------
		//get index of the thread that is calling this function
		uint32_t getThreadNumber() {
			return threadIndexMap[std::thread::id()];
		};

		//---------------------------------------------------------------------------
		//get a pointer to the job of the current task
		Job *getJobPointer() {
			return jobPointers[threadIndexMap[std::thread::id()]];
		}

		//---------------------------------------------------------------------------
		//add the new job to the thread's queue
		void addJob(Job* pJob) {
			(*pJob)();
		};

		//---------------------------------------------------------------------------
		//create a transient job
		void addJob(std::function<void()> func, uint32_t poolNumber = 0) {
			Job *pCurrentJob = getJobPointer();
			Job *pParentJob = pCurrentJob!=nullptr && pCurrentJob->m_poolNumber == poolNumber ? pCurrentJob : nullptr;
			Job *pJob = JobMemory::getInstance()->allocateJob( pParentJob, poolNumber );
			pJob->setFunction(std::make_shared<Function>(func));
			addJob(pJob);
		};

		//---------------------------------------------------------------------------
		//create a successor job for tlhis job, will be added to the queue after the current job finished -> wait
		void onFinishedJob(std::function<void()> func) {
			Job *pCurrentJob = getJobPointer();
			Job *pNewJob = JobMemory::getInstance()->allocateJob(pCurrentJob->m_parentJob, pCurrentJob->m_poolNumber ); //new job has the same parent as current job
			pNewJob->setFunction(std::make_shared<Function>(func));
			pCurrentJob->setOnFinished(pNewJob);
		};

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
			ThreadPool::getInstance()->addJob(m_onFinishedJob);	//Job does not know ThreadPool when it is declared
		}
		m_available = true;		//job is available again (after a pool reset)
	}
}

#elif

extern JobMemory * JobMemory::m_pJobMemory = nullptr;
extern ThreadPool * ThreadPool::pInstance = nullptr;


#endif

