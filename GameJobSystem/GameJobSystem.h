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
		Job *					m_pFirstChild;					//pointer to first child, needed for playback
		Job *					m_pLastChild;					//pointer to last child created, needed for playback
		Job *					m_pNextSibling;					//pointer to next sibling, needed for playback
	
		//---------------------------------------------------------------------------
		//set pointer to parent job
		void setParentJob(Job *parentJob) {
			m_parentJob = parentJob;				//set the pointer
			parentJob->m_numUnfinishedChildren++;	//tell parent that there is one more child
			if (parentJob->m_pFirstChild != nullptr) {				//there are already children
				parentJob->m_pLastChild->m_pNextSibling = this;		//remember me as sibling
				parentJob->m_pLastChild = this;						//set me as last child
			}
			else {
				parentJob->m_pFirstChild = this;	//no children yet, set first child
				parentJob->m_pLastChild = this;		//is also the last child
			}
		};	

		//---------------------------------------------------------------------------
		//set job to execute when this job has finished
		void setOnFinished(Job *pJob) { 	
			m_onFinishedJob = pJob; 
		};

		//---------------------------------------------------------------------------
		//set the Job's function
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
		void operator()();				//does not know JobMemory yet, so define later

	public:
		Job() : m_poolNumber(0), m_parentJob(nullptr), m_numUnfinishedChildren(0), m_onFinishedJob(nullptr),
				m_pFirstChild(nullptr), m_pNextSibling(nullptr) {};
		~Job() {};

#ifdef _DEBUG
		std::string id;
#endif
	};


	//---------------------------------------------------------------------------
	class JobMemory {
		friend ThreadPool;
		friend Job;

		//transient jobs, will be deleted for each new frame
		const static std::uint32_t	m_listLength = 4096;		//length of a segment

		using JobList = std::vector<Job>;
		struct JobPool {
			std::atomic<uint32_t> jobIndex = 0;			//index of next job to allocate, or number of jobs to playback
			std::vector<JobList*> jobLists;				//list of Job structures
			std::mutex lmutex;							//only lock if appending the job list
			std::atomic<uint32_t> numJobsLeftToPlay;	//number of jobs left to play back
			std::atomic<bool> isPlayedBack = false;		//if true then the pool is currently played back
			Job * pOnPlaybackFinishedJob = nullptr;		//Run this job once playback is finished

			JobPool() : jobIndex(0) {
				jobLists.reserve(100);
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
			m_jobPools.reserve(50);								//reserve enough mem so that vector is never reallocated
			m_jobPools.emplace_back( new JobPool );				//start with 1 job pool
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
				m_pJobMemory = new JobMemory();		//create the singleton
			}
			return m_pJobMemory;
		};

		//---------------------------------------------------------------------------
		//get a new empty job from the job memory - if necessary add another job list
		Job * getNextJob( uint32_t poolNumber ) {
			if (poolNumber > m_jobPools.size() - 1) {		//if pool does not exist yet
				resetPool( poolNumber );					//create it
			}

			JobPool *pPool = m_jobPools[poolNumber];						//pointer to the pool
			const uint32_t index = pPool->jobIndex.fetch_add(1);			//increase counter by 1
			if (index > pPool->jobLists.size() * m_listLength - 1) {		//do we need a new segment?
				std::lock_guard<std::mutex> lock(pPool->lmutex);			//lock the pool

				if (index > pPool->jobLists.size() * m_listLength - 1)		//might be beaten here by other thread so check again
					pPool->jobLists.emplace_back(new JobList(m_listLength));	//create a new segment
			}

			return &(*pPool->jobLists[index / m_listLength])[index % m_listLength];		//get modulus of number of last job list
		}

		//---------------------------------------------------------------------------
		//get the first job that is available
		Job* allocateJob( Job *pParent = nullptr, uint32_t poolNumber = 0 ) {
			Job *pJob;
			do {
				pJob = getNextJob(poolNumber);		//get the next Job in the pool
			} while ( !pJob->m_available );			//check whether it is available
			pJob->m_poolNumber = poolNumber;		//make sure the job knows its own pool number
			pJob->m_onFinishedJob = nullptr;		//no successor Job yet
			pJob->m_pFirstChild = nullptr;			//no children yet
			pJob->m_pLastChild = nullptr;			//no children yet
			pJob->m_pNextSibling = nullptr;			//no sibling yet
			if( pParent != nullptr ) pJob->setParentJob(pParent);	//set parent Job and notify parent
			return pJob;
		};

		//---------------------------------------------------------------------------
		//reset index for new frame, start with 0 again
		void resetPool( uint32_t poolNumber = 0 ) { 
			if (poolNumber > m_jobPools.size() - 1) {		//if pool does not yet exist
				static std::mutex mutex;					//lock this function
				std::lock_guard<std::mutex> lock(mutex);

				if (poolNumber > m_jobPools.size() - 1) {							//could be beaten here by other thread so check again
					for (uint32_t i = (uint32_t)m_jobPools.size(); i <= poolNumber; i++) {	//create missing pools
						m_jobPools.push_back(new JobPool);							//enough memory should be reserved -> no reallocate
					}
				}
			}
			m_jobPools[poolNumber]->jobIndex = 0;
		};
	};


	//---------------------------------------------------------------------------
	//queue class
	//will be changed for lock free queues
	class JobQueue {
		std::mutex m_mutex;
		std::queue<Job*> m_queue;	//conventional queue by now

	public:
		//---------------------------------------------------------------------------
		JobQueue() {};

		//---------------------------------------------------------------------------
		void push( Job * pJob) {
			m_mutex.lock();
			m_queue.push(pJob);
			m_mutex.unlock();
		};

		//---------------------------------------------------------------------------
		Job * pop() {
			m_mutex.lock();
			if (m_queue.size() == 0) {
				m_mutex.unlock();
				return nullptr;
			};
			Job* pJob = m_queue.front();
			m_queue.pop();
			m_mutex.unlock();
			return pJob;
		};

		//---------------------------------------------------------------------------
		Job *steal() {
			m_mutex.lock();
			if (m_queue.size() == 0) {
				m_mutex.unlock();
				return nullptr;
			};
			Job* pJob = m_queue.front();
			m_queue.pop();
			m_mutex.unlock();
			return pJob;
		};
	};


	//---------------------------------------------------------------------------
	class ThreadPool {
		friend Job;

	private:
		std::vector<std::thread>			m_threads;			//array of thread structures
		std::vector<Job*>					m_jobPointers;		//each thread has a current Job that it may run, pointers point to them
		std::map<std::thread::id, uint32_t> m_threadIndexMap;	//Each thread has an index number 0...Num Threads
		std::atomic<bool>					m_terminate;		//Flag for terminating the pool
		std::vector<JobQueue*>				m_jobQueues;		//Each thread has its own Job queue
		std::atomic<uint32_t>				m_uniqueID;			//a unique ID that is counted up

	public:

		//---------------------------------------------------------------------------
		//instance and private constructor
		static ThreadPool *pInstance;
		ThreadPool(std::size_t threadCount = 0, uint32_t numPools = 1) : m_terminate(false), m_uniqueID(0) {
			pInstance = this;

			if (threadCount == 0) {
				threadCount = std::thread::hardware_concurrency();		//main thread is also running
			}

			m_threads.reserve(threadCount);								//reserve mem for the threads
			m_jobQueues.resize(threadCount);							//reserve mem for job queue pointers
			m_jobPointers.resize(threadCount);							//rerve mem for Job pointers
			for (uint32_t i = 0; i < threadCount; i++) {
				m_threads.push_back( std::thread( &ThreadPool::threadTask, this ) );	//spawn the pool threads
			}

			JobMemory::getInstance()->resetPool(numPools-1);	//pre-allocate job pools
		};


		//---------------------------------------------------------------------------
		//singleton access through class
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
			m_threadIndexMap[std::this_thread::get_id()] = threadIndex;		//use only the map to determine how many threads are in the pool

			while (true) {

				if (m_terminate) break;

				Job * pJob = m_jobQueues[threadIndex]->pop();

				if (m_terminate) break;

				uint32_t max = 5;
				while (pJob == nullptr && m_threads.size()>1) {
					uint32_t idx = std::rand() % m_threads.size();
					if (idx != threadIndex) pJob = m_jobQueues[idx]->steal();
					if (!--max) break;
				}

				if (m_terminate) break;

				if (pJob != nullptr) {

#ifdef _DEBUG
					printDebug("Thread " + std::to_string(threadIndex) + " runs " + pJob->id + "\n");
#endif
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
			return m_threadIndexMap[std::this_thread::get_id()];
		};

		//---------------------------------------------------------------------------
		//get a pointer to the job of the current task
		Job *getJobPointer() {
			return m_jobPointers[getThreadNumber()];
		}

		//---------------------------------------------------------------------------
		//this replays all jobs recorded into a pool
		void playBackPool( uint32_t playPoolNumber, Function onFinishedFunction ) {
			Job *pCurrentJob = getJobPointer();			//can be nullptr if called from main thread
			Job *pParentJob = pCurrentJob != nullptr ? pCurrentJob->m_parentJob : nullptr;			//inherit parent to onFinish Job
			uint32_t poolNumber = pCurrentJob != nullptr ? pCurrentJob->m_poolNumber.load() : 0;	//stay in the same pool
			Job *ponFinishedJob = JobMemory::getInstance()->allocateJob(pParentJob, poolNumber);			//new job has the same parent as current job
			ponFinishedJob->setFunction(std::make_shared<Function>(onFinishedFunction));

			JobMemory::JobPool *pPool = JobMemory::getInstance()->m_jobPools[playPoolNumber];
			if (pPool->jobIndex == 0) {
				addJob(ponFinishedJob);			//the pool is empty, so run the finished job immediately
				return;
			}
			pPool->isPlayedBack = true;
			pPool->numJobsLeftToPlay = pPool->jobIndex.load();
			pPool->pOnPlaybackFinishedJob = ponFinishedJob;
				
			addJob(&(*pPool->jobLists[0])[0]);		//start the playback
		}

		//---------------------------------------------------------------------------
		//add a task to a random queue
		void addJob( Job *pJob ) {
			m_jobQueues[std::rand() % m_threads.size()]->push(pJob);
		}

		//---------------------------------------------------------------------------
		//create a new job in a job pool
		void addJob(Function func, uint32_t poolNumber = 0) {
			addJob(func, poolNumber, "");
		}

		void addJob(Function func, uint32_t poolNumber, std::string id ) {
			Job *pCurrentJob = getJobPointer();
			if (pCurrentJob == nullptr) {		//called from main thread -> need a Job 
				pCurrentJob = JobMemory::getInstance()->allocateJob(nullptr, poolNumber );	
				pCurrentJob->setFunction(std::make_shared<Function>(func));
#ifdef _DEBUG
				pCurrentJob->id = id;
#endif
				addJob(pCurrentJob);
				return;
			}

			Job *pJob = JobMemory::getInstance()->allocateJob( nullptr, poolNumber );	//no parent, so do not wait for its completion
			pJob->setFunction(std::make_shared<Function>(func));
#ifdef _DEBUG
			pJob->id = id;
#endif
			addJob(pJob);
		};

		//---------------------------------------------------------------------------
		//create a new child job in a job pool
		void addChildJob(Function func ) {
			addChildJob(func, getJobPointer()->m_poolNumber, "");
		}

		void addChildJob( Function func, std::string id) {
			addChildJob(func, getJobPointer()->m_poolNumber, id);
		}

		void addChildJob(Function func, uint32_t poolNumber, std::string id ) {
			if (JobMemory::getInstance()->m_jobPools[poolNumber]->isPlayedBack) return;
			Job *pJob = JobMemory::getInstance()->allocateJob(getJobPointer(), poolNumber );
#ifdef _DEBUG			
			pJob->id = id;							//copy Job id, can be removed in production code
#endif
			pJob->setFunction(std::make_shared<Function>(func));
			addJob(pJob);
		};

		//---------------------------------------------------------------------------
		//create a successor job for tlhis job, will be added to the queue after the current job finished -> wait
		void onFinishedAddJob(Function func, std::string id ) {
			Job *pCurrentJob = getJobPointer();			//can be nullptr if called from main thread
			if (JobMemory::getInstance()->m_jobPools[pCurrentJob->m_poolNumber]->isPlayedBack) return;

			Job *pParentJob = pCurrentJob != nullptr ? pCurrentJob->m_parentJob : nullptr;			//inherit parent to onFinish Job
			uint32_t poolNumber = pCurrentJob != nullptr ? pCurrentJob->m_poolNumber.load() : 0;	//stay in the same pool
			Job *pNewJob = JobMemory::getInstance()->allocateJob(pParentJob, poolNumber);			//new job has the same parent as current job
#ifdef _DEBUG			
			pNewJob->id = id;
#endif
			pNewJob->setFunction(std::make_shared<Function>(func));
			pCurrentJob->setOnFinished(pNewJob);
		};

		//---------------------------------------------------------------------------
		//wait for all children to finish and then terminate the pool
		void onFinishedTerminatePool() {
			onFinishedAddJob( std::bind(&ThreadPool::terminate, this), "terminate");
		};

		//---------------------------------------------------------------------------
		//Print deubg information, this is synchronized so that text is not confused on the console
		void printDebug(std::string s) {
			static std::mutex lmutex;
			std::lock_guard<std::mutex> lock(lmutex);

			std::cout << s;
		};

	};


}


#ifdef IMPLEMENT_GAMEJOBSYSTEM

namespace gjs {

	JobMemory * JobMemory::m_pJobMemory = nullptr;
	ThreadPool * ThreadPool::pInstance = nullptr;

	//---------------------------------------------------------------------------
	//This is run if the job is executed
	void Job::operator()() {
		if (m_function == nullptr) return;
		m_numUnfinishedChildren = 1;					//number of children includes itself
		(*m_function)();								//call the function

		JobMemory::JobPool *pPool = JobMemory::getInstance()->m_jobPools[m_poolNumber];
		if( pPool->isPlayedBack) {
			Job *pChild = m_pFirstChild;					//if pool is played back
			while (pChild != nullptr) {
				ThreadPool::getInstance()->addJob(pChild);	//run all children
				pChild = pChild->m_pNextSibling;
			}

			uint32_t numLeft = pPool->numJobsLeftToPlay.fetch_sub(1);
			if (numLeft == 1) {
				pPool->isPlayedBack = false;										//playback stops
				ThreadPool::getInstance()->addJob(pPool->pOnPlaybackFinishedJob);	//schedule onPlayBackFinished Job
			}
			return;
		}

		uint32_t numLeft = m_numUnfinishedChildren.fetch_add(-1);
		if (numLeft == 1) onFinished();					//this was the last child
	};


	//---------------------------------------------------------------------------
	//This call back is called once a Job and all its children are finished
	void Job::onFinished() {
		if (m_parentJob != nullptr) {		//if there is parent then inform it
			m_parentJob->childFinished();	//if this is the last child job then the parent will also finish
		}
		if (m_onFinishedJob != nullptr) {	//is there a successor Job?
			ThreadPool::getInstance()->addJob(m_onFinishedJob);	//schedule it for running
		}
		m_available = true;		//job is available again (after a pool reset)
	}
}

#elif

extern JobMemory * JobMemory::m_pJobMemory = nullptr;
extern ThreadPool * ThreadPool::pInstance = nullptr;


#endif

