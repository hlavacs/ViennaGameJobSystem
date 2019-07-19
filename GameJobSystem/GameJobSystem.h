#pragma once

/*
The Vienna Game Job System (VGJS)
Designed and implemented by Prof. Helmut Hlavacs, Faculty of Computer Science, University of Vienna
See documentation on how to use it at https://github.com/hlavacs/GameJobSystem
The library is a single include file, and can be used under MIT license.
*/

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


namespace vgjs {

	class JobMemory;
	class Job;
	class JobSystem;
	class JobQueueFIFO;
	class JobQueueLockFree;


	using Function = std::function<void()>;

	//-------------------------------------------------------------------------------
	class Job {
		friend JobMemory;
		friend JobSystem;
		friend JobQueueFIFO;
		friend JobQueueLockFree;

	private:
		Job *					m_nextInQueue;					//next in the current queue

		Job *					m_parentJob;					//parent job, called if this job finishes
		Job *					m_onFinishedJob;				//job to schedule once this job finshes
		Job *					m_pFirstChild;					//pointer to first child, needed for recording/playback
		Job *					m_pLastChild;					//pointer to last child created, needed for recording
		Job *					m_pNextSibling;					//pointer to next sibling, needed for playback
		std::atomic<uint32_t>	m_poolNumber;					//number of pool this job comes from
		std::shared_ptr<Function> m_function;					//the function to carry out
		std::atomic<uint32_t>	m_numUnfinishedChildren;		//number of unfinished jobs
		std::atomic<bool>		m_available;					//is this job available after a pool reset?
		bool					m_repeatJob;					//if true then the job will be rescheduled
		bool					m_endPlayback;					//true then end pool playback after this job is finished
	
		//---------------------------------------------------------------------------
		//set pointer to parent job
		void setParentJob(Job *parentJob, bool addToChildren ) {
			m_parentJob = parentJob;				//set the pointer
			if (parentJob == nullptr) return;
			parentJob->m_numUnfinishedChildren++;	//tell parent that there is one more child
			if (addToChildren) {
				if (parentJob->m_pFirstChild != nullptr) {				//there are already children
					parentJob->m_pLastChild->m_pNextSibling = this;		//remember me as sibling
					parentJob->m_pLastChild = this;						//set me as last child
				}
				else {
					parentJob->m_pFirstChild = this;	//no children yet, set first child
					parentJob->m_pLastChild = this;		//is also the last child
				}
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
		//notify parent, or schedule the finished job, define later since do not know JobSystem yet
		void onFinished();	

		//---------------------------------------------------------------------------
		void childFinished() {
			uint32_t numLeft = m_numUnfinishedChildren.fetch_sub(1);
			if (numLeft == 1) onFinished();					//this was the last child
		};

		//---------------------------------------------------------------------------
		//run the packaged task
		void operator()();				//does not know JobMemory yet, so define later

	public:

#ifndef _DEBUG
		uint32_t				m_padding[2];					//pad to 128 bytes
#else

		std::string				id;											//info for debugging
#endif

		Job() : m_nextInQueue(nullptr), m_poolNumber(0), m_parentJob(nullptr), m_numUnfinishedChildren(0), m_onFinishedJob(nullptr),
				m_pFirstChild(nullptr), m_pNextSibling(nullptr), m_repeatJob(false), m_available(true) {};
		~Job() {};
	};


	//---------------------------------------------------------------------------
	class JobMemory {
		friend JobSystem;
		friend Job;

		//transient jobs, will be deleted for each new frame
		const static std::uint32_t	m_listLength = 4096;		//length of a segment

		using JobList = std::vector<Job>;
		struct JobPool {
			std::atomic<uint32_t> jobIndex;				//index of next job to allocate, or number of jobs to playback
			std::vector<JobList*> jobLists;				//list of Job structures
			std::mutex lmutex;							//only lock if appending the job list
			std::atomic<bool> isPlayedBack = false;		//if true then the pool is currently played back

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
		static JobMemory *pInstance;							//pointer to singleton
		static JobMemory *getInstance() {						
			if (pInstance == nullptr) {
				pInstance = new JobMemory();		//create the singleton
			}
			return pInstance;
		};

		//---------------------------------------------------------------------------
		//get a new empty job from the job memory - if necessary add another job list
		Job * JobMemory::getNextJob(uint32_t poolNumber) {
			if (poolNumber > m_jobPools.size() - 1) {		//if pool does not exist yet
				resetPool(poolNumber);					//create it
			}

			JobPool *pPool = m_jobPools[poolNumber];						//pointer to the pool
			const uint32_t index = pPool->jobIndex.fetch_add(1);			//increase counter by 1
			if (index > pPool->jobLists.size() * m_listLength - 1) {		//do we need a new segment?
				std::lock_guard<std::mutex> lock(pPool->lmutex);			//lock the pool

				if (index > pPool->jobLists.size() * m_listLength - 1)		//might be beaten here by other thread so check again
					pPool->jobLists.emplace_back(new JobList(m_listLength));	//create a new segment
			}

			return &(*pPool->jobLists[index / m_listLength])[index % m_listLength];		//get modulus of number of last job list
		};

		//---------------------------------------------------------------------------
		//get the first job that is available
		Job* allocateJob( uint32_t poolNumber = 0 ) {
			Job *pJob;
			do {
				pJob = getNextJob(poolNumber);		//get the next Job in the pool
			} while ( !pJob->m_available );			//check whether it is available
			pJob->m_nextInQueue = nullptr;

			pJob->m_poolNumber = poolNumber;		//make sure the job knows its own pool number
			pJob->m_onFinishedJob = nullptr;		//no successor Job yet
			pJob->m_endPlayback = false;			//should not end playback
			pJob->m_pFirstChild = nullptr;			//no children yet
			pJob->m_pLastChild = nullptr;			//no children yet
			pJob->m_pNextSibling = nullptr;			//no sibling yet
			pJob->m_parentJob = nullptr;			//default is no parent
			pJob->m_repeatJob = false;				//default is no repeat
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
			m_jobPools[poolNumber]->jobIndex.store(0);
		};

		JobPool *getPoolPointer( uint32_t poolNumber) {
			if (poolNumber > m_jobPools.size() - 1) {		//if pool does not yet exist
				resetPool(poolNumber);
			}
			return m_jobPools[poolNumber];
		};
	};


	//---------------------------------------------------------------------------
	//queue class
	//will be changed for lock free queues
	class JobQueue {

	public:
		JobQueue() {};
		virtual void push(Job * pJob) = 0;
		virtual Job * pop() = 0;
		virtual Job *steal() = 0;
	};


	//---------------------------------------------------------------------------
	//queue class
	//will be changed for lock free queues
	class JobQueueSTL : public JobQueue {
		std::mutex m_mutex;
		std::queue<Job*> m_queue;	//conventional queue by now

	public:
		//---------------------------------------------------------------------------
		JobQueueSTL() {};

		//---------------------------------------------------------------------------
		void push(Job * pJob) {
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
			return pop();
		};
	};


	//---------------------------------------------------------------------------
	//queue class
	//lock free queue
	class JobQueueFIFO : public JobQueue {

		Job * m_pHead = nullptr;
		Job * m_pTail = nullptr;
		std::mutex m_mutex;

	public:
		//---------------------------------------------------------------------------
		JobQueueFIFO() {};

		//---------------------------------------------------------------------------
		void push(Job * pJob) {
			m_mutex.lock();
			pJob->m_nextInQueue = nullptr;
			if (m_pHead == nullptr) m_pHead = pJob;
			if( m_pTail != nullptr ) m_pTail->m_nextInQueue = pJob;
			m_pTail = pJob;			
			m_mutex.unlock();
		};

		//---------------------------------------------------------------------------
		Job * pop() {
			m_mutex.lock();

			if (m_pHead == nullptr) {
				m_mutex.unlock();
				return nullptr;
			}
			Job *pJob = m_pHead;
			if (pJob == m_pTail) {
				m_pTail = nullptr;
			}
			m_pHead = m_pHead->m_nextInQueue;

			m_mutex.unlock();
			return pJob;
		};

		//---------------------------------------------------------------------------
		Job *steal() {
			return pop();
		};
	};


	//---------------------------------------------------------------------------
	//queue class
	//lock free queue
	class JobQueueLockFree : public JobQueue {

		std::atomic<Job *> m_pHead = nullptr;

	public:
		//---------------------------------------------------------------------------
		JobQueueLockFree() {};

		//---------------------------------------------------------------------------
		void push(Job * pJob) {
			pJob->m_nextInQueue = m_pHead.load(std::memory_order_relaxed);

			while (! std::atomic_compare_exchange_strong_explicit(
				&m_pHead, &pJob->m_nextInQueue, pJob, 
				std::memory_order_release, std::memory_order_relaxed) ) {};
		};

		//---------------------------------------------------------------------------
		Job * pop() {
			Job * head = m_pHead.load(std::memory_order_relaxed);
			if (head == nullptr) return nullptr;
			while (head != nullptr && !std::atomic_compare_exchange_weak(&m_pHead, &head, head->m_nextInQueue)) {};
			return head;
		};

		//---------------------------------------------------------------------------
		Job *steal() {
			return pop();
		};
	};



	//---------------------------------------------------------------------------
	class JobSystem {
		friend Job;

	private:
		std::vector<std::thread>			m_threads;			//array of thread structures
		std::vector<Job*>					m_jobPointers;		//each thread has a current Job that it may run, pointers point to them
		std::map<std::thread::id, uint32_t> m_threadIndexMap;	//Each thread has an index number 0...Num Threads
		std::atomic<bool>					m_terminate;		//Flag for terminating the pool
		std::vector<JobQueue*>				m_jobQueues;		//Each thread has its own Job queue
		std::atomic<uint32_t>				m_numJobs;			//total number of jobs in the system
		std::mutex							m_mainThreadMutex;	//used for syncing with main thread
		std::condition_variable				m_mainThreadCondVar;//used for waking up main tread

		//---------------------------------------------------------------------------
		// function each thread performs
		void threadTask() {
			static std::atomic<uint32_t> threadIndexCounter = 0;
			uint32_t threadIndex = threadIndexCounter.fetch_add(1);
			m_threadIndexMap[std::this_thread::get_id()] = threadIndex;		//use only the map to determine how many threads are in the pool

			while(threadIndexCounter < m_threads.size() )
				std::this_thread::sleep_for(std::chrono::nanoseconds(10));

			while (true) {

				if (m_terminate) break;

				Job * pJob = m_jobQueues[threadIndex]->pop();

				if (m_terminate) break;

				uint32_t tsize = m_threads.size();
				if (pJob == nullptr && tsize > 1) {
					uint32_t idx = std::rand() % tsize;
					uint32_t max = 2*tsize;

					while (pJob == nullptr) {
						if (idx != threadIndex) pJob = m_jobQueues[idx]->steal();
						idx = (idx+1) % tsize;
						max--;
						if (max == 0) break;
						if (m_terminate) break;
					}
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
					std::this_thread::sleep_for(std::chrono::nanoseconds(1));
				};
			}
			m_numJobs = 0;
			m_mainThreadCondVar.notify_all();		//make sure to wake up a waiting main thread
		};

	public:

		//---------------------------------------------------------------------------
		//class constructor
		//threadCount Number of threads to start. If 0 then the number of hardware threads is used.
		//numPools Number of job pools to create upfront
		//
		static JobSystem *	pInstance;			//pointer to singleton
		JobSystem(std::size_t threadCount = 0, uint32_t numPools = 1) : m_terminate(false), m_numJobs(0) {
			pInstance = this;
			JobMemory::getInstance();			//create the job memory

			if (threadCount == 0) {
				threadCount = std::thread::hardware_concurrency();		//main thread is also running
			}

			m_jobQueues.resize(threadCount);							//reserve mem for job queue pointers
			m_jobPointers.resize(threadCount);							//rerve mem for Job pointers
			for (uint32_t i = 0; i < threadCount; i++) {
				m_jobQueues[i] = new JobQueueLockFree();						//job queue
				m_jobPointers[i] = nullptr;								//pointer to current Job structure
			}

			m_threads.reserve(threadCount);								//reserve mem for the threads
			for (uint32_t i = 0; i < threadCount; i++) {
				m_threads.push_back(std::thread(&JobSystem::threadTask, this));	//spawn the pool threads
			}

			JobMemory::pInstance->resetPool(numPools - 1);	//pre-allocate job pools
		};

		//---------------------------------------------------------------------------
		//singleton access through class
		//returns a pointer to the JobSystem instance
		//
		static JobSystem * getInstance() {
			if (pInstance == nullptr) pInstance = new JobSystem();
			return pInstance;
		};

		JobSystem(const JobSystem&) = delete;				// non-copyable,
		JobSystem& operator=(const JobSystem&) = delete;
		JobSystem(JobSystem&&) = default;					// but movable
		JobSystem& operator=(JobSystem&&) = default;
		~JobSystem() {
			m_threads.clear();
		};

		//---------------------------------------------------------------------------
		//sets a flag to terminate all running threads
		//
		void terminate() {
			m_terminate = true;					//let threads terminate
		};

		//---------------------------------------------------------------------------
		//returns total number of jobs in the system
		//
		uint32_t getNumberJobs() {
			return m_numJobs;
		};

		//---------------------------------------------------------------------------
		//can be called by the main thread to wait for the completion of all jobs in the system
		//returns as soon as there are no more jobs in the job queues
		//
		void wait() {
			while (getNumberJobs() > 0) {
				std::unique_lock<std::mutex> lock(m_mainThreadMutex);
				m_mainThreadCondVar.wait(lock);							//wait to be awakened
			}
		};

		//---------------------------------------------------------------------------
		//can be called by the main thread to wait for all threads to terminate
		//returns as soon as all threads have exited
		//
		void waitForTermination() {
			for (uint32_t i = 0; i < m_threads.size(); i++ ) {
				m_threads[i].join();
			}
		};

		//---------------------------------------------------------------------------
		// returns number of threads in the thread pool
		//
		std::size_t getThreadCount() const { return m_threads.size(); };

		//---------------------------------------------------------------------------
		//each thread has a unique index between 0 and numThreads - 1
		//returns index of the thread that is calling this function
		//can e.g. be used for allocating command buffers from command buffer pools in Vulkan
		//
		int32_t getThreadNumber() {
			if (m_threadIndexMap.count(std::this_thread::get_id()) == 0) return -1;

			return m_threadIndexMap[std::this_thread::get_id()];
		};

		//---------------------------------------------------------------------------
		//wrapper for resetting a job pool in the job memory
		//poolNumber Number of the pool to reset
		//
		void resetPool( uint32_t poolNumber ) {
			JobMemory::pInstance->resetPool(poolNumber);
		};
		
		//---------------------------------------------------------------------------
		//returns a pointer to the job of the current task
		//
		Job *getJobPointer() {
			int32_t threadNumber = getThreadNumber();
			if (threadNumber < 0) return nullptr;
			return m_jobPointers[threadNumber];
		};

		//---------------------------------------------------------------------------
		//this replays all jobs recorded into a pool
		//playPoolNumber Number of the job pool that should be replayed
		//
		void playBackPool( uint32_t poolNumber) {
			JobMemory::JobPool *pPool = JobMemory::pInstance->getPoolPointer(poolNumber);
			if (pPool->jobIndex == 0) return;						//if empty simply return
			pPool->isPlayedBack = true;								//set flag to indicate that the pool is in playback mode
			
			Job * pJob = &(*pPool->jobLists[0])[0];					//get pointer to the first job in the pool
			pJob->setParentJob(getJobPointer(), true);					//parent's childFinished() will be called when die playback ended
			pJob->m_endPlayback = true;								//this is the last job that will finish, so end playback for the pool
			addJob( pJob );											//start the playback
		};

		//---------------------------------------------------------------------------
		//returns whether a pool is currently played back
		//poolNumber Number of pool to query - if -1, then take the pool of the current job
		//
		bool isPlayedBack(int32_t poolNumber = -1 ) {
			if (poolNumber < 0) {
				Job * pJob = getJobPointer();
				if (pJob == nullptr) return false;
				poolNumber = pJob->m_poolNumber;
			}
			JobMemory::JobPool *pPool = JobMemory::pInstance->getPoolPointer(poolNumber);
			return pPool->isPlayedBack;
		}

		//---------------------------------------------------------------------------
		//add a job to a queue
		//pJob Pointer to the job to schedule
		//
		void addJob( Job *pJob ) {
			m_numJobs++;	//keep track of the number of jobs in the system to sync with main thread

			uint32_t tsize = m_threads.size();
			int32_t threadNumber = getThreadNumber();
			threadNumber = threadNumber < 0 ? std::rand() % tsize : threadNumber;

			if (m_numJobs < 2 * tsize) {
				threadNumber = std::rand() % tsize;
			}

			m_jobQueues[threadNumber]->push(pJob);			//keep jobs local
		};

		//---------------------------------------------------------------------------
		//create a new job in a job pool
		//func The function to schedule
		//poolNumber Optional number of the pool, or 0
		//
		void addJob(Function&& func, uint32_t poolNumber = 0) {
			addJob(std::move(func), poolNumber, std::string(""));
		};

		//func The function to schedule
		//id A name for the job for debugging
		void addJob(Function&& func, std::string&& id) {
			addJob(std::move(func), 0, std::move(id));
		};

		//func The function to schedule
		//poolNumber Optional number of the pool, or 0
		//id A name for the job for debugging
		void addJob(Function&& func, uint32_t poolNumber, std::string&& id ) {
			Job *pCurrentJob = getJobPointer();
			if (pCurrentJob == nullptr) {		//called from main thread -> need a Job 
				pCurrentJob = JobMemory::pInstance->allocateJob( poolNumber );
				pCurrentJob->setFunction(std::make_shared<Function>(func));
#ifdef _DEBUG
				pCurrentJob->id = id;
#endif
				addJob(pCurrentJob);
				return;
			}

			Job *pJob = JobMemory::pInstance->allocateJob( poolNumber );	//no parent, so do not wait for its completion
			pJob->setFunction(std::make_shared<Function>(func));
#ifdef _DEBUG
			pJob->id = id;
#endif
			addJob(pJob);
		};


		//---------------------------------------------------------------------------
		//create a new child job in a job pool
		//func The function to schedule
		void addChildJob(Function&& func) {
			addChildJob(std::move(func), getJobPointer()->m_poolNumber, std::move(std::string("")));
		};

		//func The function to schedule
		//id A name for the job for debugging
		void addChildJob( Function func, std::string&& id) {
			addChildJob(std::move(func), getJobPointer()->m_poolNumber, std::move(id) );
		};


		//func The function to schedule
		//id A name for the job for debugging
		void addChildJob(Function func, uint32_t poolNumber) {
			addChildJob(std::move(func), poolNumber, std::move(std::string("")));
		};

		//func The function to schedule
		//poolNumber Number of the pool
		//id A name for the job for debugging
		void addChildJob(Function&& func, uint32_t poolNumber, std::string&& id ) {
			if (JobMemory::pInstance->getPoolPointer(poolNumber)->isPlayedBack) return;			//in playback no children are created
			Job *pJob = JobMemory::pInstance->allocateJob( poolNumber );
			pJob->setParentJob(getJobPointer(), true);			//set parent Job and notify parent

#ifdef _DEBUG
			pJob->id = id;										//copy Job id, can be removed in production code
#endif
			pJob->setFunction(std::make_shared<Function>(func));
			addJob(pJob);
		};

		//---------------------------------------------------------------------------
		//create a successor job for this job, will be added to the queue after 
		//the current job finished (i.e. all children have finished)
		//func The function to schedule
		//id A name for the job for debugging
		//
		void onFinishedAddJob(Function func, std::string &&id) {
			Job *pCurrentJob = getJobPointer();			//should never be called by meain thread
			if (pCurrentJob == nullptr) return;			//is null if called by main thread
			if (JobMemory::pInstance->getPoolPointer(pCurrentJob->m_poolNumber)->isPlayedBack) return; //in playback mode no sucessors are recorded
			if (pCurrentJob->m_repeatJob) return;		//you cannot do both repeat and add job after finishing
			uint32_t poolNumber = pCurrentJob != nullptr ? pCurrentJob->m_poolNumber.load() : 0;	//stay in the same pool
			Job *pNewJob = JobMemory::pInstance->allocateJob( poolNumber);			//new job has the same parent as current job
#ifdef _DEBUG	
			pNewJob->id = id;
#endif
			pNewJob->setFunction(std::make_shared<Function>(func));
			pCurrentJob->setOnFinished(pNewJob);
		};

		//---------------------------------------------------------------------------
		//Once the Job finished, it will be rescheduled  and repeated
		//This also includes all children, which will be recreated and run
		//
		void onFinishedRepeatJob() {
			Job *pCurrentJob = getJobPointer();						//can be nullptr if called from main thread
			if (pCurrentJob == nullptr) return;						//is null if called by main thread
			if (pCurrentJob->m_onFinishedJob != nullptr) return;	//you cannot do both repeat and add job after finishing	
			pCurrentJob->m_repeatJob = true;
		}

		//---------------------------------------------------------------------------
		//wait for all children to finish and then terminate the pool
		void onFinishedTerminatePool() {
			onFinishedAddJob( std::bind(&JobSystem::terminate, this), "terminate");
		};

		//---------------------------------------------------------------------------
		//Print deubg information, this is synchronized so that text is not confused on the console
		//s The string to print to the console
		void printDebug(std::string s) {
			static std::mutex lmutex;
			std::lock_guard<std::mutex> lock(lmutex);

			std::cout << s;
		};
	};
}


#ifdef IMPLEMENT_GAMEJOBSYSTEM

namespace vgjs {

	JobMemory * JobMemory::pInstance = nullptr;
	JobSystem * JobSystem::pInstance = nullptr;

	//---------------------------------------------------------------------------
	//This is run if the job is executed
	void Job::operator()() {
		if (m_function == nullptr) return;
		m_numUnfinishedChildren = 1;					//number of children includes itself
		(*m_function)();								//call the function

		//addChildJob() would have started the children already, so if playback do this immediately
		//you cannot wait for children to finish, since in playback there are no children yet
		//if a job is repeated - so are its children!
		JobMemory::JobPool *pPool = JobMemory::pInstance->getPoolPointer(m_poolNumber);
		if( pPool->isPlayedBack) {
			Job *pChild = m_pFirstChild;					//if pool is played back
			while (pChild != nullptr) {
				m_numUnfinishedChildren++;
				JobSystem::pInstance->addJob(pChild);	//run all children
				pChild = pChild->m_pNextSibling;
			}
		}

		uint32_t numLeft = m_numUnfinishedChildren.fetch_sub(1);	//reduce number of running children by 1 (includes itself)
		if (numLeft == 1) onFinished();								//this was the last child
	};


	//---------------------------------------------------------------------------
	//This call back is called once a Job and all its children are finished
	void Job::onFinished() {
#ifdef _DEBUG
		JobSystem::pInstance->printDebug( "Job " + id + " finishes\n" );
#endif

		if (m_repeatJob) {
			m_repeatJob = false;								//only repeat if job executes so
			JobSystem::pInstance->m_numJobs--;					//addJob will increase this again
			JobSystem::pInstance->addJob( this );				//rescheduled this
			return;
		}

		if (m_onFinishedJob != nullptr) {						//is there a successor Job?
			if (m_parentJob != nullptr) 
				m_onFinishedJob->setParentJob(  m_parentJob, false );
			JobSystem::pInstance->addJob(m_onFinishedJob);	//schedule it for running
		}

		if (m_parentJob != nullptr) {		//if there is parent then inform it	
			m_parentJob->childFinished();	//if this is the last child job then the parent will also finish
		}

		//synchronize with the main thread
		uint32_t numLeft = JobSystem::pInstance->m_numJobs.fetch_sub(1); //one less job in the system
		if (numLeft == 1) {							//if this was the last job in ths system 
			JobSystem::pInstance->m_mainThreadCondVar.notify_all();	//notify main thread that might be waiting
		}
		//if pool is played back, and this is the last job to finish, then end the playback
		if (m_endPlayback) {						//on playback the first job is the last to finish
			m_endPlayback = false;
			JobMemory::pInstance->getPoolPointer(m_poolNumber)->isPlayedBack = false;
		}
		m_available = true;					//job is available again (after a pool reset)
	};


}

#elif

extern JobMemory * JobMemory::pInstance = nullptr;
extern JobSystem * JobSystem::pInstance = nullptr;


#endif

