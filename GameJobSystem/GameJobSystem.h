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
		std::string id;
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

			~JobPool() {
				for (uint32_t i = 0; i < jobLists.size(); i++) {
					delete jobLists[i];
				}
			};
		};

	private:
		std::vector<JobPool*> m_jobPools;						//a list with pointers to the job pools

		static JobMemory *m_pJobMemory;							//pointer to singleton
		JobMemory() {											//private constructor
			m_jobPools.reserve(10);
			m_jobPools.emplace_back( new JobPool );
		};

		~JobMemory() {
			for (uint32_t i = 0; i < m_jobPools.size(); i++) {
				delete m_jobPools[i];
			}
		}
	
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
			if (poolNumber > m_jobPools.size() - 1) {		//if pool does not exist yet
				resetPool( poolNumber );					//create it
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
		Job* allocateJob( Job *pParent = nullptr, uint32_t poolNumber = 0, std::string id = ""  ) {
			Job *pJob;
			do {
				pJob = getNextJob(poolNumber);
			} while ( !pJob->m_available );
			pJob->m_available = false;
			pJob->m_poolNumber = poolNumber;
			pJob->m_onFinishedJob = nullptr;
			pJob->id = id;

			if( pParent != nullptr ) pJob->setParentJob(pParent);
			return pJob;
		};

		//---------------------------------------------------------------------------
		//reset index for new frame, start with 0 again
		void resetPool( uint32_t poolNumber = 0 ) { 
			if (poolNumber > m_jobPools.size() - 1) {
				static std::mutex mutex;
				std::lock_guard<std::mutex> lock(mutex);

				if (poolNumber > m_jobPools.size() - 1) {							//could be beaten here by other thread
					for (uint32_t i = m_jobPools.size(); i <= poolNumber; i++) {	//so check again
						m_jobPools.push_back(new JobPool);
					}
				}
			}
			m_jobPools[poolNumber]->jobIndex = 0;
		};
	};


	//---------------------------------------------------------------------------
	class JobQueue {
		std::mutex m_mutex;
		std::queue<Job*> m_queue;

	public:
		JobQueue() {};
		void push( Job * pJob) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push(pJob);
		};
		Job * pop() {
			if (m_queue.size() == 0) return nullptr;
			std::lock_guard<std::mutex> lock(m_mutex);
			Job* pJob = m_queue.front();
			m_queue.pop();
			return pJob;
		};
		Job *steal() {
			if (m_queue.size() == 0) return nullptr;
			std::lock_guard<std::mutex> lock(m_mutex);
			Job* pJob = m_queue.front();
			m_queue.pop();
			return pJob;
		};
	};


	//---------------------------------------------------------------------------
	class ThreadPool {
		friend Job;

		using Ids = std::vector<std::thread::id>;

	private:
		std::vector<std::thread> m_threads;
		std::vector<Job*> m_jobPointers;
		std::map<std::thread::id, uint32_t> m_threadIndexMap;
		std::atomic<bool> m_terminate;

		std::vector<JobQueue*> m_jobQueues;
		std::condition_variable m_jobsAvailable;


	public:

		//---------------------------------------------------------------------------
		//instance and private constructor
		static ThreadPool *pInstance;
		ThreadPool(std::size_t threadCount = 0) : m_terminate(false) {
			pInstance = this;

			if (threadCount == 0) {
				threadCount = std::thread::hardware_concurrency();		//main thread is also running
			}

			m_threads.reserve(threadCount);	
			m_jobQueues.resize(threadCount);
			m_jobPointers.resize(threadCount);
			for (uint32_t i = 0; i < threadCount; i++) {
				m_threads.push_back( std::thread( &ThreadPool::threadTask, this ) );
			}
		};


		//---------------------------------------------------------------------------
		static ThreadPool * getInstance() {
			if (pInstance == nullptr) pInstance = new ThreadPool();
			return pInstance;
		};

		ThreadPool(const ThreadPool&) = delete;				// non-copyable,
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = default;					// but movable
		ThreadPool& operator=(ThreadPool&&) = default;
		~ThreadPool() {
			m_threads.clear();
		};

		//---------------------------------------------------------------------------
		//will also let the main task exit the threadTask() function
		void terminate() {
			m_terminate = true;	
		}

		//---------------------------------------------------------------------------
		//should be called by the main task
		void wait() {
			for (uint32_t i = 0; i < m_threads.size(); i++ ) {
				m_threads[i].join();
			}
		}

		//---------------------------------------------------------------------------
		// function each thread performs
		void threadTask() {
			static std::atomic<uint32_t> threadIndexCounter = 0;

			uint32_t threadIndex = threadIndexCounter.fetch_add(1);
			m_jobQueues[threadIndex] = new JobQueue();				//work stealing queue
			m_jobPointers[threadIndex] = nullptr;					//pointer to current Job structure
			m_threadIndexMap[std::thread::id()] = threadIndex;		//use only the map to determine how many threads are in the pool

			while (true) {

				if (m_terminate) break;

				Job * pJob = m_jobQueues[threadIndex]->pop();

				if (m_terminate) break;

				uint32_t max = 5;
				while (pJob == nullptr && m_threadIndexMap.size()>1) {
					uint32_t idx = std::rand() % m_threadIndexMap.size();
					if (idx != threadIndex) pJob = m_jobQueues[idx]->steal();
					if (!--max) break;
				}

				if (m_terminate) break;

				if (pJob != nullptr) {
					m_jobPointers[threadIndex] = pJob;	//make pointer to the Job structure accessible!
					(*pJob)();							//run the job
				}
				else {
					std::this_thread::sleep_for(std::chrono::microseconds(100));
				};
			}
		};

		//---------------------------------------------------------------------------
		// returns number of threads being used
		std::size_t getThreadCount() const { return m_threadIndexMap.size(); };

		//---------------------------------------------------------------------------
		//get index of the thread that is calling this function
		uint32_t getThreadNumber() {
			return m_threadIndexMap[std::thread::id()];
		};

		//---------------------------------------------------------------------------
		//get a pointer to the job of the current task
		Job *getJobPointer() {
			return m_jobPointers[m_threadIndexMap[std::thread::id()]];
		}

		void addJob( Job *pJob ) {
			m_jobQueues[m_threadIndexMap[std::thread::id()]]->push(pJob);
		}

		//---------------------------------------------------------------------------
		//create a new job in a job pool
		void addJob(std::function<void()> func, uint32_t poolNumber = 0, std::string id = "") {
			Job *pCurrentJob = getJobPointer();
			if (pCurrentJob == nullptr) {
				pCurrentJob = JobMemory::getInstance()->allocateJob(nullptr, poolNumber, id);
				pCurrentJob->setFunction(std::make_shared<Function>(func));
				m_jobPointers[m_threadIndexMap[std::thread::id()]] = pCurrentJob;
				addJob(pCurrentJob);
				return;
			}

			Job *pParentJob = pCurrentJob->m_poolNumber == poolNumber ? pCurrentJob : nullptr;	//either current job or nullptr if called from the main thread
			Job *pJob = JobMemory::getInstance()->allocateJob( pParentJob, poolNumber, id );
			pJob->setFunction(std::make_shared<Function>(func));
			addJob(pJob);
		};

		//---------------------------------------------------------------------------
		//create a successor job for tlhis job, will be added to the queue after the current job finished -> wait
		void onFinishedJob(std::function<void()> func, std::string id ="") {
			Job *pCurrentJob = getJobPointer();
			Job *pParentJob = pCurrentJob != nullptr ? pCurrentJob->m_parentJob : nullptr;
			uint32_t poolNumber = pCurrentJob != nullptr ? pCurrentJob->m_poolNumber.load() : 0;
			Job *pNewJob = JobMemory::getInstance()->allocateJob(pParentJob, poolNumber, id ); //new job has the same parent as current job
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

