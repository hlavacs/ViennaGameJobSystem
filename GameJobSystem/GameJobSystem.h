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


// find first bit that equals to 1 (only designed for 32 and 64 bit integers) that works on all machines and compilers
template<class Int>
unsigned int countTrailing0M(Int v) {
	if (sizeof(Int) < 8) {
		// smaller and possibly faster version for 32 and les bit numbers
		static const char multiplyDeBruijnBitPosition[32] = {
			0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
			31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
		};
		return multiplyDeBruijnBitPosition[((uint32_t)((v & -(int32_t)v) * 0x077CB531U)) >> 27];
	}
	else {
		static const char multiplyDeBruijnBitPosition[64] = {
			0, 1, 2, 56, 3, 32, 57, 46, 29, 4, 20, 33, 7, 58, 11, 47,
			62, 30, 18, 5, 16, 21, 34, 23, 53, 8, 59, 36, 25, 12, 48, 39,
			63, 55, 31, 45, 28, 19, 6, 10, 61, 17, 15, 22, 52, 35, 24, 38,
			54, 44, 27, 9, 60, 14, 51, 37, 43, 26, 13, 50, 42, 49, 41, 40
		};
		return multiplyDeBruijnBitPosition[((uint64_t)((v & -(int64_t)v) * 0x26752B916FC7B0DULL)) >> 58];
	};
}


namespace gjs {

	class JobListPermanent;
	class JobMemory;
	class Job;
	class ThreadPool;


	//-------------------------------------------------------------------------------
	struct JobHandle {
		JobListPermanent *	m_pJobList;
		uint32_t			m_listIndex;

		JobHandle() : m_pJobList(nullptr) {};

		JobHandle(JobListPermanent * pJobList, uint32_t listIndex) {
			m_pJobList = pJobList;
			m_listIndex = listIndex;
		};

		Job * getPointer();	//do not know JobListPermanent yet, so define this function later
	};


	//-------------------------------------------------------------------------------
	class Job {
		friend JobMemory;
		friend JobListPermanent;

	private:
		JobHandle				m_permHandle = {nullptr, 0};	//only used for permanent memory

		using PackedTask = std::packaged_task<void()>;
		std::shared_ptr<PackedTask>	m_packagedTask;				//the packages task to carry out
		Job *					m_parentJob = nullptr;			//parent job, called if this job finishes
		std::atomic<uint32_t>	m_numUnfinishedChildren = 0;	//number of unfinished jobs
		Job *					m_onFinishedJob;				//job to schedule once this job finshes

	public:
		Job() {};
		Job(Job&& other) :
			m_permHandle(std::move(other.m_permHandle)), 
			m_packagedTask(std::move(other.m_packagedTask)), 
			m_parentJob(std::move(other.m_parentJob)),
			m_numUnfinishedChildren(m_numUnfinishedChildren._My_val),
			m_onFinishedJob(std::move(other.m_onFinishedJob))
			{}
		Job& operator=(const Job& other) = default;
		~Job() {};

		void setParentJob(Job *pJob) { m_parentJob = pJob; };		//set pointer to parent job
		void setOnFinished(Job *pJob) { m_onFinishedJob = pJob; };	//set job to execute when this job has finished

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
			(*m_packagedTask)();
		};

	};

	//---------------------------------------------------------------------------
	class JobListPermanent {
		friend JobMemory;
		friend Job;
		friend JobHandle;

	private:
		uint32_t m_occupancy;
		std::vector<Job> m_jobList;
		std::mutex m_mutex;

	public:
		JobListPermanent() : m_occupancy(0xFFFFFFFF) { m_jobList.resize(32);  };
		JobListPermanent(JobListPermanent&& other) :
			m_occupancy(std::move(other.m_occupancy)), 
			m_jobList(std::move(other.m_jobList)), 
			m_mutex()
		{}
		JobListPermanent& operator=(const JobListPermanent& other) = default;

		//-----------------------------------------------------------------------
		JobHandle allocatePermanentJob() {
			std::lock_guard<std::mutex> lock(m_mutex);			//lock this list

			if (!m_occupancy) return { nullptr, 0 };			//are there still free slots?
			uint32_t r = countTrailing0M(m_occupancy);			//get first bit that is set to 1
			m_occupancy ^= 1 << r;								//set it to zero
			
			m_jobList[r].m_permHandle = {this, r};
			return m_jobList[r].m_permHandle;					//return the handle
		};

		//-----------------------------------------------------------------------
		void deallocatePermanentJob(Job *pJob) {
			std::lock_guard<std::mutex> lock(m_mutex);			//lock this list

			m_occupancy |= 1 << pJob->m_permHandle.m_listIndex;				//reset the bit representing this slot
			pJob->m_permHandle = { nullptr, 0 };	//clear the handle
		};
	};



	//---------------------------------------------------------------------------
	class JobMemory {
		friend ThreadPool;

	private:
		//permanent jobs
		std::vector<JobListPermanent>	m_jobListsPermanent;	//permanent jobs

		//transient jobs, will be deleted for each new frame
		const static std::uint32_t	m_listLength = 4096;		//length of a segment
		std::atomic<uint32_t>		m_jobIndex = 0;				//current index (number of jobs so far)
		using JobList = std::vector<Job>;
		std::vector<JobList*>		m_jobListsTrans;			//list of transient segments

		static JobMemory *m_pJobMemory;							//pointer to singleton
		JobMemory() : m_jobIndex(0) {							//private constructor
			m_jobListsTrans.push_back(new JobList(m_listLength));	//init with 1 segment
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
		Job* allocateTransientJob( ) {
			const uint32_t index = m_jobIndex.fetch_add(1);			
			if (index > m_jobListsTrans.size() * m_listLength - 1) {
				m_jobListsTrans.push_back(new JobList(m_listLength));
			}

			return &(*m_jobListsTrans.back())[index % m_listLength];		//get modulus of number of last job list
		};

		//---------------------------------------------------------------------------
		//allocate a job that will not go out of context
		Job* allocatePermanentJob() {

			uint32_t size = m_jobListsPermanent.size();
			for (uint32_t i = 0; i < size; i++) {
				if (m_jobListsPermanent[i].m_occupancy != 0) {
					JobHandle handle = m_jobListsPermanent[i].allocatePermanentJob();	//might fail due to other thread
					if (handle.m_pJobList) {
						return handle.getPointer();
					}
				}
			}
			
			static std::mutex lmutex;
			std::lock_guard<std::mutex> lock(lmutex);
			if (size < m_jobListsPermanent.size()) {
				JobHandle handle = m_jobListsPermanent.back().allocatePermanentJob();	//might fail due to other thread
				if (handle.m_pJobList) {
					return handle.getPointer();
				}
			}

			m_jobListsPermanent.emplace_back(JobListPermanent());
			JobHandle handle = m_jobListsPermanent.back().allocatePermanentJob();	//might fail due to other thread
			return handle.getPointer();
		};

		//---------------------------------------------------------------------------
		//allocate a job that will not go out of context
		void deallocatePermanentJob( Job *pJob) {
			pJob->m_permHandle.m_pJobList->deallocatePermanentJob(pJob);
		}

		void reset() { m_jobIndex = 0; };							//reset index for new frame, start with 0 again
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


	Job * JobHandle::getPointer() { 
		return &(m_pJobList->m_jobList[m_listIndex]); 
	};


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

