#pragma once


/**
*
* \file
* \brief The Vienna Game Job System (VGJS)
*
* Designed and implemented by Prof. Helmut Hlavacs, Faculty of Computer Science, University of Vienna
* See documentation on how to use it at https://github.com/hlavacs/GameJobSystem
* The library is a single include file, and can be used under MIT license.
*
*/


#include <iostream>
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



#if defined(VE_ENABLE_MULTITHREADING) || defined(DOXYGEN)
	/**
	* \defgroup VGJS_Macros
	* \brief Main job system macros encapsulating interactions for multithreading.
	* @{
	*/
	
	/**
	* \brief Get index of the thread running this job
	*
	* \returns the index of the thread running this job
	*
	*/
	#define JIDX vgjs::JobSystem::getInstance()->getThreadIndex()

	/**
	* \brief Add a function as a job to the jobsystem.
	*
	* In multithreaded operations, this macro adds a function as job to the job system.
	* In singlethreaded mode, this macro simply calls the function.
	*
	* \param[in] f The function to be added or called.
	*
	*/
	#define JADD( f )	vgjs::JobSystem::getInstance()->addJob( [=](){ f; } )

	/**
	* \brief Add a dependent job to the jobsystem. The job will run only after all previous jobs have ended.
	*
	* In multithreaded operations, this macro adds a function as job to the job system after all other child jobs have finished.
	* In singlethreaded mode, this macro simply calls the function.
	*
	* \param[in] f The function to be added or called.
	*
	*/
	#define JDEP( f )	vgjs::JobSystem::getInstance()->onFinishedAddJob( [=](){ f; } )

	/**
	* \brief Add a job to the jobsystem, schedule to a particular thread.
	*
	* In multithreaded operations, this macro adds a function as job to the job system and schedule it to the given thread.
	* In singlethreaded mode, this macro simply calls the function.
	*
	* \param[in] f The function to be added or called.
	* \param[in] t The thread that the job should go to.
	*
	*/
	#define JADDT( f, t )	vgjs::JobSystem::getInstance()->addJob( [=](){ f; }, t )

	/**
	* \brief Add a dependent job to the jobsystem, schedule to a particular thread. The job will run only after all previous jobs have ended.
	*
	* In multithreaded operations, this macro adds a function as job to the job system and schedule it to the given thread.
	* The job will run after all other child jobs have finished.
	* In singlethreaded mode, this macro simply calls the function.
	*
	* \param[in] f The function to be added or called.
	* \param[in] t The thread that the job should go to.
	*
	*/
	#define JDEPT( f, t )	vgjs::JobSystem::getInstance()->onFinishedAddJob( [=](){ f; }, t )

	/**
	* \brief After the job has finished, reschedule it.
	*/
	#define JREP vgjs::JobSystem::getInstance()->onFinishedRepeatJob()

	/**
	* \brief Wait for all jobs in the job system to have finished and that there are no more jobs.
	*/
	#define JWAIT vgjs::JobSystem::getInstance()->wait()

	/**
	* \brief Reset the memory for allocating jobs. Should be done once every game loop.
	*/
	#define JRESET vgjs::JobSystem::getInstance()->resetPool()

	/**
	* \brief Tell all threads to end. The main thread will return to the point where it entered the job system.
	*/
	#define JTERM vgjs::JobSystem::getInstance()->terminate()

	/**
	* \brief Wait for all threads to have terminated.
	*/
	#define JWAITTERM vgjs::JobSystem::getInstance()->waitForTermination()

	/**
	* \brief A wrapper over return, is empty in singlethreaded use
	*/
	#define JRET return;

	///@}
#endif

#if !defined(VE_ENABLE_MULTITHREADING) || defined(DOXYGEN)
	/**
	* \defgroup VGJS_Macros
	* @{
	*/
	#define JIDX 0
	#define JADD( f ) {f;}
	#define JDEP( f ) {f;}
	#define JADDT( f, t ) {f;}
	#define JDEPT( f, t ) {f;}
	#define JREP
	#define JWAIT 
	#define JRESET
	#define JTERM
	#define JWAITTERM
	#define JRET
	///@}
#endif



namespace vgjs {



	class JobMemory;
	class Job;
	class JobSystem;
	class JobQueueFIFO;
	class JobQueueLockFree;

	using Function = std::function<void()>;	///< Standard function that can be put into a job

	/*typedef uint32_t VgjsThreadIndex;					///< Use for index into job arrays
	constexpr VgjsThreadIndex VGJS_NULL_THREAD_IDX = std::numeric_limits<VgjsThreadIndex>::max();	///< No index given
	typedef uint32_t VgjsThreadLabel;					///< Use for index into job arrays
	constexpr VgjsThreadLabel VGJS_NULL_THREAD_LABEL =	std::numeric_limits<VgjsThreadLabel>::max(); ///< No label given
	typedef uint64_t VgjsThreadID;					///< An id contains an index and a label
	constexpr VgjsThreadID VGJS_NULL_THREAD_ID =	std::numeric_limits<VgjsThreadID>::max();	///< No ID given
	*/

	enum class VgjsThreadIndex : uint32_t {};
	constexpr VgjsThreadIndex VGJS_NULL_THREAD_IDX = VgjsThreadIndex(std::numeric_limits<uint32_t>::max());	///< No index given

	enum class VgjsThreadLabel : uint32_t {};				///< Use for index into job arrays
	constexpr VgjsThreadLabel VGJS_NULL_THREAD_LABEL = VgjsThreadLabel(std::numeric_limits<uint32_t>::max());	///< No index given

	enum class VgjsThreadID : uint64_t {};				///< An id contains an index and a label
	constexpr VgjsThreadID VGJS_NULL_THREAD_ID = VgjsThreadID(std::numeric_limits<uint64_t>::max());	///< No ID given


	/**
	*
	* \brief Break down an ID into its components and return the thread index
	*
	* \param[in] thread_id ID of the thread
	*
	*/
	inline VgjsThreadIndex getThreadIndexFromID(VgjsThreadID thread_id) {
		return (VgjsThreadIndex)((uint64_t)thread_id & (uint64_t)VGJS_NULL_THREAD_IDX);
	}

	/**
	*
	* \brief Break down an ID into its components and return the thread label
	*
	* \param[in] thread_id ID of the thread
	*
	*/
	inline VgjsThreadLabel getThreadLabelFromID(VgjsThreadID thread_id) {
		return (VgjsThreadLabel)((uint64_t)thread_id >> 32);
	}

	/**
	*
	* \brief Construct an ID from index and label
	*
	* \param[in] thread_idx Index of the thread
	* \param[in] label Label of the thread
	*
	*/
	inline VgjsThreadID TID(VgjsThreadIndex thread_idx, VgjsThreadLabel label = VGJS_NULL_THREAD_LABEL) {
		return (VgjsThreadID)((uint64_t)label << 32 | (uint64_t)thread_idx);
	}


	/**
	* \brief This class holds a single job, including a function object and other info.
	*/
	class Job {
		friend JobMemory;
		friend JobSystem;
		friend JobQueueFIFO;
		friend JobQueueLockFree;

	private:
		Job *					m_nextInQueue;					///< next in the current queue
		Job *					m_parentJob;					///< parent job, called if this job finishes
		Job *					m_onFinishedJob;				///< job to schedule once this job finshes
		VgjsThreadIndex			m_thread_idx;					///< the id of the thread this job must be scheduled, or -1 if any thread
		VgjsThreadLabel			m_thread_label;					///< label for performance evaluation
		Function				m_function;						///< the function to carry out
		std::atomic<uint32_t>	m_numUnfinishedChildren;		///< number of unfinished jobs
		std::chrono::high_resolution_clock::time_point t1, t2;	///< execution start and end
		VgjsThreadIndex			m_exec_thread;					///< thread that this job actually ran at
		bool					m_available;					///< is this job available after a pool reset?
		bool					m_repeatJob;					///< if true then the job will be rescheduled
	
		/**
		*
		* \brief Set a pointer to the parent of a job
		*
		* \param[in] parentJob Pointer to the parent.
		*
		*/
		void setParentJob(Job *parentJob ) {
			m_parentJob = parentJob;				//set the pointer
			if (parentJob == nullptr) return;
			parentJob->m_numUnfinishedChildren++;	//tell parent that there is one more child
		};	

		/**
		*
		* \brief Set job to execute when this job has finished
		*
		* \param[in] pJob Pointer to the job that will be scheduled after this job ends.
		*
		*/
		void setOnFinished(Job *pJob) { 	
			m_onFinishedJob = pJob; 
		};

		/**
		*
		* \brief Set fixed thread id.
		*
		* \param[in] thread_id The id contains the fixed thread index and a label
		*
		*/
		void setThreadId(VgjsThreadID thread_id) {
			m_thread_idx = getThreadIndexFromID(thread_id);
			m_thread_label = getThreadLabelFromID(thread_id);
		}

		/**
		*
		* \brief Set the Job's function
		*
		* \param[in] func The function object containing the job function
		*
		*/
		void setFunction(Function& func) {
			m_function = func;
		};

		/**
		*
		* \brief Set the Job's function
		*
		* \param[in] func The function object containing the job function
		*
		*/
		void setFunction(Function&& func) {
			m_function = std::move(func);
		};

		/**
		* \brief Notify parent, or schedule the finished job, define later since do not know JobSystem yet
		*/
		void onFinished();	

		/**
		* \brief Called by a child job that has finished.
		*
		* Once a child finished, it calls this parent function. The parent decreases the
		* child counter. Once the child counter reaches 1, the parent iself can finish.
		*
		*/
		void childFinished() {
			uint32_t numLeft = m_numUnfinishedChildren.fetch_sub(1);
			if (numLeft == 1) onFinished();					///< this was the last child
		};

		/**
		* \brief Run the packages task.
		*/
		void operator()();		///< does not know JobMemory yet, so define later

	public:

		Job() : m_nextInQueue(nullptr), m_parentJob(nullptr), m_onFinishedJob(nullptr), 
			m_thread_idx(VGJS_NULL_THREAD_IDX), m_thread_label(VGJS_NULL_THREAD_LABEL),
			t1(), t2(), m_exec_thread(VGJS_NULL_THREAD_IDX),
			m_numUnfinishedChildren(0), m_repeatJob(false), m_available(true) {}; ///< Job class constructor
		~Job() {};	///<Job class desctructor
	};


	/**
	* \brief Hold memory for the Job class instances
	*
	* The JobMemory class manages memory for storing allocated jobs.
	* Allocation is done linearly, using a counter. No need for synchronization because
	* each thread has its own job memory and allocates only locally.
	* Since the memory grows continuously, it must be reset periodically.
	* Typically this is done once every game loop. Resetting can be done during
	* normal operation, since jobs still in use are protected by a boolean.
	* 
	*/
	class JobMemory {
		friend JobSystem;
		friend Job;

		constexpr static std::uint32_t	m_listLength = 2048;	///< Default length of a segment
		constexpr static std::uint32_t  m_listmask = m_listLength - 1;

		using JobList = std::unique_ptr<std::vector<Job>>;
		uint32_t				jobIndex;		///< index of next job to allocate, or number of jobs to playback
		std::vector<JobList>	jobLists;		///< list of Job structures
		vve::VeClock m_clock;					///< A clock for measuring execution times

	public:
		JobMemory() : m_clock("JobMemory", 200), jobIndex(0), jobLists() {	///<JobMemory class constructor
			jobLists.emplace_back(std::make_unique<std::vector<Job>>(m_listLength));
		};
		~JobMemory() {};
	
		/**
		* \brief Get a new empty job from the job memory - if necessary add another job list
		*/
		Job * getNextJob() {
			uint32_t index = jobIndex;	///< Use this index
			jobIndex++;					///< increase counter by 1

			if (index > jobLists.size() * m_listLength - 1) {		///< do we need a new segment?
				jobLists.emplace_back(std::make_unique<std::vector<Job>>(m_listLength));	///< create a new segment
			}

			return &(*jobLists[index / m_listLength])[index & m_listmask];	///< get modulus of number of last job list
		};

		/**
		* \brief Get the first job that is available
		*/
		Job* allocateJob( ) {
			Job *pJob;						///< Pointer to the new job
			
			//m_clock.start();
			do {
				pJob = getNextJob();			///< get the next Job in the pool
			} while ( !pJob->m_available );		///< check whether it is available

			pJob->m_available = false;			///< Mark this job as unavailable
			pJob->m_nextInQueue = nullptr;		///< For storing in a queue
			pJob->m_onFinishedJob = nullptr;				///< no successor Job yet
			pJob->m_parentJob = nullptr;					///< default is no parent
			pJob->m_repeatJob = false;						///< default is no repeat
			pJob->m_thread_idx = VGJS_NULL_THREAD_IDX;		///< can run oon any thread
			pJob->m_thread_label = VGJS_NULL_THREAD_LABEL;
			pJob->m_exec_thread = VGJS_NULL_THREAD_IDX;
			//m_clock.stop();
			return pJob;
		};

		/**
		* \brief Reset index for new frame, start with 0 again
		*/
		void resetPool() { 
			jobIndex = 0;
		};
	};


	/**
	* \brief Base class for job queues, provides a single interface
	*/
	class JobQueue {
	protected:
		vve::VeClock m_clock;	///<Clock for measuring time

	public:
		JobQueue() : m_clock("Job Queue", 200) {};	///<JobQueue class constructor
		virtual void push(Job * pJob) = 0;	///<Pushes a new job onto the queue
		virtual Job * pop() = 0;			///<Pop the next job from the queue
		virtual Job *steal() = 0;			///<Steal a job by another thread
	};


	/**
	*
	* \brief A simple FIFO queue for thread local operation.
	*
	* This queue is not synchronized, so it can only run thread local. 
	* I.e., it belongs to a thread and only this thread is allowed to access it.
	*
	*/
	class JobQueueFIFO : public JobQueue {
		std::queue<Job*> m_queue;	///< Conventional STL queue by now

	public:
		//---------------------------------------------------------------------------
		JobQueueFIFO() {};	///< JobQueueFIFO class constructor

		/**
		*
		* \brief Pushes a job onto the queue
		*
		* \param[in] pJob The job to be pushed into the queue
		*
		*/
		void push(Job * pJob) {
			m_queue.push(pJob);
		};

		/**
		*
		* \brief Pops a job from the queue
		*
		* \returns a job or nullptr
		*
		*/
		Job * pop() {
			if (m_queue.size() == 0) {		///< if no jobs available
				return nullptr;				///< return nullptr
			};
			Job* pJob = m_queue.front();	///< get next job from front
			m_queue.pop();					///< delete it from the queue
			return pJob;					///< return it
		};

		/**
		*
		* \brief Another thread wants to steal a job (should never happen with this queue)
		*
		* \returns a job or nullptr
		*
		*/
		Job *steal() {
			return pop();
		};
	};


	/**
	* \brief A lockfree LIFO stack
	*
	* This queue can be accessed by any thread, it is synchronized by STL CAS operations.
	* However it is only a LIFO stack, not a FIFO queue.
	*
	*/
	class JobQueueLockFree : public JobQueue {

		std::atomic<Job *> m_pHead = nullptr;	///< Head of the stack

	public:
		JobQueueLockFree() {};	///<JobQueueLockFree class constructor

		/**
		*
		* \brief Pushes a job onto the queue
		*
		* \param[in] pJob The job to be pushed into the queue
		*
		*/
		void push(Job * pJob) {
			pJob->m_nextInQueue = m_pHead.load(std::memory_order_relaxed);
			while (! std::atomic_compare_exchange_weak(&m_pHead, &pJob->m_nextInQueue, pJob) ) {};
		};

		/**
		*
		* \brief Pops a job from the queue
		*
		* \returns a job or nullptr
		*
		*/
		Job * pop() {
			Job * head = m_pHead.load(std::memory_order_relaxed);
			if (head == nullptr) return nullptr;
			while (head != nullptr && !std::atomic_compare_exchange_weak(&m_pHead, &head, head->m_nextInQueue)) {};
			return head;
		};

		/**
		*
		* \brief Another thread wants to steal a job 
		*
		* \returns a job or nullptr
		*
		*/
		Job *steal() {
			return pop();
		};
	};



	/**
	*
	* \brief The main JobSystem class manages the whole VGJS job system
	*
	* The JobSystem starts N threads and provides them with data structures.
	* It can add new jobs, and wait until they are done.
	*
	*/
	class JobSystem {
		friend Job;

	private:
		std::vector<std::unique_ptr<std::thread>>	m_threads;				///< array of thread structures
		uint32_t									m_threadCount;			///< number of threads in the pool
		uint32_t									m_start_idx = 0;		///< idx of first thread that is created
		static thread_local VgjsThreadIndex			m_thread_index;			///< each thread has its own number
		std::vector<std::unique_ptr<JobMemory>>		m_job_memory;			///< each thread has its own job memory
		std::vector<Job*>							m_jobPointers;			///< each thread has a current Job that it may run, pointers point to them
		uint32_t									m_memory_reset_counter = 0; ///< signals each thread when to reset its memory
		std::atomic<bool>							m_terminate;			///< Flag for terminating the pool
		std::vector<std::unique_ptr<JobQueue>>		m_jobQueues;			///< Each thread has its own Job queue
		std::vector<std::unique_ptr<JobQueue>>		m_jobQueuesLocal;		///< a secondary low priority FIFO queue for each thread, where polling jobs are parked
		std::vector<std::unique_ptr<JobQueue>>		m_jobQueuesLocalFIFO;	///< a secondary low priority FIFO queue for each thread, where polling jobs are parked
		std::atomic<uint32_t>						m_numJobs;				///< total number of jobs in the system
		std::vector<uint64_t>						m_numLoops;				///< number of loops the task has done so far
		std::vector<uint64_t>						m_numMisses;			///< number of times the task did not get a job
		std::mutex									m_mainThreadMutex;		///< used for syncing with main thread
		std::condition_variable						m_mainThreadCondVar;	///< used for waking up main tread

	public:
		vve::VeClock m_clock;				///< Clock for time measurements

		/**
		*
		* \brief every thread runs in this function
		*
		* \param[in] threadIndex Number of this thread
		*
		*/
		void threadTask( VgjsThreadIndex threadIndex = VgjsThreadIndex(0) ) {
			thread_local uint32_t memory_reset_counter = 0;			//Used for resetting the pool
			static std::atomic<uint32_t> threadIndexCounter = 0;	//Counted up when started

			m_thread_index = threadIndex;	//Remember your own thread number

			threadIndexCounter++;			//count up at start
			while(threadIndexCounter.load() < m_threadCount )	//Continuous only if all threads are running
				std::this_thread::sleep_for(std::chrono::nanoseconds(10));

			while (!m_terminate) {			//Run until the job system is terminated
				//m_numLoops[threadIndex]++;	//Count up the number of loops run

				if (m_memory_reset_counter > memory_reset_counter) { //Possibly reset your job memory
					memory_reset_counter = m_memory_reset_counter;
					m_job_memory[(uint32_t)m_thread_index]->resetPool();
				}

				if (m_terminate) break;	//Test for termination

				Job * pJob = m_jobQueuesLocal[(uint32_t)threadIndex]->pop();	//Try to get a job from local queue

				if (pJob == nullptr)
					pJob = m_jobQueues[(uint32_t)threadIndex]->pop(); //Try to get a job from the general job queue

				if (pJob == nullptr && m_threadCount > 1) {	//try to steal one from another thread
					uint32_t idx = (uint32_t)threadIndex + 1;		//std::rand() % tsize;
					uint32_t max = m_threadCount - 1;		//Max number of tries to steal one

					while (pJob == nullptr && max > 0) {				//loop until found or retry own 
						if (idx >= m_threadCount)
							idx = 0;
						pJob = m_jobQueues[idx]->steal();	//Try to steal one
						++idx;
						max--;
					}
				}

				if (pJob == nullptr)			//If no job found
					pJob = m_jobQueuesLocalFIFO[(uint32_t)threadIndex]->pop(); //Try to get a job from local FIFO queue

				if (pJob != nullptr) {	//f found a job
					//std::cout << "start job on thread idx " << threadIndex << " with label " << pJob->m_thread_label << std::endl;

					m_jobPointers[(uint32_t)threadIndex] = pJob;						//make pointer to the Job structure accessible!
					//pJob->t1 = std::chrono::high_resolution_clock::now();	//time of execution
					(*pJob)();												//run the job
					//pJob->t2 = std::chrono::high_resolution_clock::now();	//time of finishing
					pJob->m_exec_thread = threadIndex;						//thread idx this job was executed on
				}
				else {
					//m_numMisses[threadIndex]++;	//Increase miss counter, possibly sleep or wait for a signal
					//std::this_thread::sleep_for(std::chrono::nanoseconds(1));
					//std::this_thread::yield();
				}
			}
			m_numJobs = 0;
			m_mainThreadCondVar.notify_all();		//make sure to wake up a waiting main thread
		};

		static std::unique_ptr<JobSystem> pInstance;			//pointer to singleton

		/**
		*
		* \brief JobSystem class constructor
		*
		* \param[in] threadIndex Number of this thread
		* \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0 
		*
		*/
		JobSystem(uint32_t threadCount = 0, uint32_t start_idx = 0) : 
			m_terminate(false), m_numJobs(0), m_clock("Job System", 100) {

			m_start_idx = start_idx;
			m_threadCount = threadCount;
			if (m_threadCount == 0) {
				m_threadCount = std::thread::hardware_concurrency();		///< main thread is also running
			}

			m_job_memory.resize(m_threadCount);
			m_jobQueues.resize(m_threadCount);								//reserve mem for job queue pointers
			m_jobQueuesLocal.resize(m_threadCount);							//reserve mem for local job queue pointers
			m_jobQueuesLocalFIFO.resize(m_threadCount);						//reserve mem for polling job queue pointers
			m_jobPointers.resize(m_threadCount);							//rerve mem for Job pointers
			m_numMisses.resize(m_threadCount);								//each threads counts its own misses
			m_numLoops.resize(m_threadCount);								//each tread counts its own loops
			for (uint32_t i = 0; i < m_threadCount; i++) {
				m_job_memory[i]			= std::make_unique<JobMemory>();			//Each thread has its own job memory
				m_jobQueues[i]			= std::make_unique<JobQueueLockFree>();		//job queue with work stealing
				m_jobQueuesLocal[i]		= std::make_unique<JobQueueLockFree>();		//job queue for local work
				m_jobQueuesLocalFIFO[i] = std::make_unique<JobQueueFIFO>();			//job queue for local polling work
				m_jobPointers[i]		= nullptr;							//pointer to current Job structure
				m_numLoops[i]			= 0;								//for accounting per thread statistics
				m_numMisses[i]			= 0;
			}

			for (uint32_t i = start_idx; i < m_threadCount; i++) {
				m_threads.push_back(std::make_unique<std::thread>( &JobSystem::threadTask, this, VgjsThreadIndex(i) ));	//spawn the pool threads
			}
		};

		/**
		*
		* \brief Singleton access through class
		*
		* \param[in] threadIndex Number of threads in the system
		* \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
		* \returns a pointer to the JobSystem instance
		*
		*/
		static std::unique_ptr<JobSystem>& getInstance(uint32_t threadCount = 0, uint32_t start_idx = 0 ) {
			static std::once_flag once;
			std::call_once(once, [&]() { pInstance = std::make_unique<JobSystem>(threadCount, start_idx); });
			return pInstance;
		};

		/**
		*
		* \brief Test whether the job system has been started yet
		*
		* \returns true if the instance exists, else false
		*
		*/
		static bool isInstanceCreated() {
			if (pInstance) return true;
			return false;
		};

		JobSystem(const JobSystem&) = delete;				// non-copyable,
		JobSystem& operator=(const JobSystem&) = delete;
		JobSystem(JobSystem&&) = default;					// but movable
		JobSystem& operator=(JobSystem&&) = default;

		/**
		* \brief JobSystem class destructor
		*/
		~JobSystem() {
			
		};

		/**
		* \brief Terminate the job system
		*/
		void terminate() {
			m_terminate = true;					//let threads terminate
		};

		/**
		* \brief Get the number of jobs in the system
		*
		* \returns total number of jobs in the system
		*/
		uint32_t getNumberJobs() {
			return m_numJobs;
		};

		/**
		*
		* \brief Wait for completion of all jobs
		*
		* Can be called by the main thread to wait for the completion of all jobs in the system.
		* Returns as soon as there are no more jobs in the job queues.
		*
		*/
		void wait() {
			while (getNumberJobs() > 0) {
				std::unique_lock<std::mutex> lock(m_mainThreadMutex);
				m_mainThreadCondVar.wait_for(lock, std::chrono::microseconds(500));		//wait to be awakened
			}
		};

		/**
		*
		* \brief Wait for termination of all jobs
		*
		* Can be called by the main thread to wait for all threads to terminate.
		* Returns as soon as all threads have exited.
		*
		*/
		void waitForTermination() {
			for (std::unique_ptr<std::thread>& pThread : m_threads ) {
				pThread->join();
			}
		};

		/**
		*
		* \brief Get the number of jobs in the job system
		*
		* \returns number of threads in the job system
		*
		*/
		std::size_t getThreadCount() const { 
			return m_threadCount; 
		};

		/**
		*
		* \brief Get the number of jobs in the job system
		*
		* Each thread has a unique index between 0 and numThreads - 1.
		* Returns index of the thread that is calling this function.
		* Can e.g. be used for allocating command buffers from command buffer pools in Vulkan.
		*
		* \returns the index of this thread
		*
		*/
		VgjsThreadIndex getThreadIndex() {
			return m_thread_index;
		};

		/**
		*
		* \brief Reset all pool memory counters to zero
		*
		* Wrapper for resetting a job pool in the job memory.
		* poolNumber Number of the pool to reset.
		*
		*/
		void resetPool() {
			m_memory_reset_counter++;
		};
		
		/**
		*
		* \brief Get the job instance this job runs in.
		*
		* Every job runs in a job instance. This function provides a pointer to the instance.
		* When starting a new child job, the child job must know its parent instance.
		* So this function provides this information.
		* 
		* \returns a pointer to the job instance of the current task
		*
		*/
		Job *getJobPointer() {
			int32_t threadNumber = (uint32_t)getThreadIndex();
			if (threadNumber < 0) 
				return nullptr;
			return m_jobPointers[threadNumber];
		};

		/**
		*
		* \brief Add a job to a queue.
		*
		* \param[in] pJob Pointer to the new job to add to a queue
		* \returns a pointer to the job instance of the current task
		*
		*/
		void addJob( Job *pJob ) {
			thread_local static uint32_t next_thread = 0;	///< counter for distributing jjobs to threads evenly

			assert(pJob != nullptr);
			++m_numJobs;	//keep track of the number of jobs in the system to sync with main thread

			if (pJob->m_thread_idx != VGJS_NULL_THREAD_IDX)  {
				uint32_t threadNumber = (uint32_t)getThreadIndex();

				uint32_t thread_idx = (uint32_t)pJob->m_thread_idx % m_threadCount;

				if(thread_idx == threadNumber )
					m_jobQueuesLocalFIFO[thread_idx]->push(pJob);	//put into thread local FIFO queue
				else
					m_jobQueuesLocal[thread_idx]->push(pJob);		//put into thread local LIFO queue
				return;
			}

			++next_thread;
			if (next_thread >= m_threadCount)
				next_thread = next_thread % m_threadCount;

			m_jobQueues[next_thread]->push(pJob);	//put into random LIFO queue

		};

		/**
		*
		* \brief Create a new child job
		*
		* \param[in] func The function to schedule, as rvalue reference
		* \param[in] thread_id The ID of the job, consist of the thread index and a label
		*
		*/
		void addJob( Function&& func, VgjsThreadID thread_id = VGJS_NULL_THREAD_ID ) {
			//std::cout << "addJob index " << (uint32_t)getThreadIndexFromID(thread_id) << " label " << (uint32_t)getThreadLabelFromID(thread_id) << std::endl;
			Job* pJob = m_job_memory[(uint32_t)m_thread_index]->allocateJob();
			//m_clock.start();
			pJob->setParentJob(getJobPointer());	//set parent Job to notify on finished, or nullptr if main thread
			pJob->setFunction(std::forward<Function>(func));
			pJob->setThreadId(thread_id);
			addJob(pJob);
			//m_clock.stop();
		};

		/**
		*
		* \brief Create a successor job for the current job
		*
		* Create a successor job for this job, will be added to the queue after 
		* the current job finished (i.e. all children have finished).
		*
		* \param[in] func The function to schedule, as rvalue reference
		* \param[in] thread_id The ID of the job, consist of the thread index and a label
		*
		*/
		void onFinishedAddJob(Function &&func, VgjsThreadID thread_id = VGJS_NULL_THREAD_ID ) {
			Job *pCurrentJob = getJobPointer();			//should never be called by meain thread
			if (pCurrentJob == nullptr) return;			//is null if called by main thread
			assert(!pCurrentJob->m_repeatJob);			//you cannot do both repeat and add job after finishing
			Job* pNewJob = m_job_memory[(uint32_t)m_thread_index]->allocateJob();
			pNewJob->setFunction(func);
			pNewJob->setThreadId(thread_id);
			pCurrentJob->setOnFinished(pNewJob);
		};

		/**
		*
		* \brief Repeat this job after it has finished
		*
		* Once the Job finished, it will be reschedule and repeated.
		* This also includes all children, which will be recreated and run.
		*
		*/
		void onFinishedRepeatJob() {
			Job *pCurrentJob = getJobPointer();						//can be nullptr if called from main thread
			if (pCurrentJob == nullptr) return;						//is null if called by main thread
			assert(pCurrentJob->m_onFinishedJob == nullptr);		//you cannot do both repeat and add job after finishing	
			pCurrentJob->m_repeatJob = true;						//flag that in onFinished() the job will be rescheduled
		}

		/**
		*
		* \brief Print debug information, this is synchronized so that text is not confused on the console
		*
		* \param[in] s The string to print to the console
		*
		*/
		void printDebug(std::string s) {
			static std::mutex lmutex;
			std::lock_guard<std::mutex> lock(lmutex);
			std::cout << s;
		};
	};


}



#if (defined(VE_ENABLE_MULTITHREADING) && defined(VE_IMPLEMENT_GAMEJOBSYSTEM)) || defined(DOXYGEN)

namespace vgjs {

	std::unique_ptr<JobSystem> JobSystem::pInstance;			//pointer to singleton
	thread_local VgjsThreadIndex JobSystem::m_thread_index = VgjsThreadIndex(0);		///< Thread local index of the thread


	/**
	* \brief The call operator of a job, calling the stored function object
	*/
	void Job::operator()() {
		m_numUnfinishedChildren = 1;					//number of children includes itself

		//std::cout << "call job with label " << m_thread_label << " children left " << m_numUnfinishedChildren << std::endl;

		m_function();									//call the function

		uint32_t numLeft = m_numUnfinishedChildren.fetch_sub(1);	//reduce number of running children by 1 (includes itself)

		//std::cout << "stop job with label " << m_thread_label << " children left " << numLeft << std::endl;

		if (numLeft == 1) 
			onFinished();								//this was the last child
	};


	/**
	* \brief This call back is called once a Job and all its children are finished
	*/
	void Job::onFinished() {

		//std::cout << "finished job with label " << m_thread_label << std::endl;

		if (m_repeatJob) {							//job is repeated for polling
			m_repeatJob = false;					//only repeat if job executes and so
			JobSystem::pInstance->m_numJobs--;		//addJob() will increase this again
			JobSystem::pInstance->addJob( this);	//rescheduled this to the polling FIFO queue
			return;
		}

		if (m_onFinishedJob != nullptr) {						//is there a successor Job?
			if (m_parentJob != nullptr) 
				m_onFinishedJob->setParentJob(  m_parentJob );  //increases child num of parent by 1
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
		m_available = true;					//job is available again (after a pool reset)
	};


}

#endif

