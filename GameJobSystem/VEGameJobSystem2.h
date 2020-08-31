


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
#include <memory_resource>
#include <type_traits>



namespace vgjs {


    /**
    * \brief A lockfree LIFO stack
    *
    * This queue can be accessed by any thread, it is synchronized by STL CAS operations.
    * However it is only a LIFO stack, not a FIFO queue.
    */

    class Job {
    public:
        Job*                m_next = nullptr;
        std::atomic<int>    m_children = 0;
        Job*                m_continuation = nullptr;

        virtual bool resume() = 0;

        void operator() () noexcept {
            resume();
        }

        virtual void continuation() noexcept {};  //can be removed if not needed
    };


    template<bool FIFO = false>
    class JobQueue {

        std::atomic<Job*> m_head = nullptr;	///< Head of the stack

    public:

        JobQueue() {};	///<JobQueue class constructor

        /**
        * \brief Pushes a job onto the queue
        * \param[in] pJob The job to be pushed into the queue
        */
        void push(Job* pJob) {
            pJob->m_next = m_head.load(std::memory_order_relaxed);
            while (!std::atomic_compare_exchange_weak(&m_head, &pJob->m_next, pJob)) {};
        };

        /**
        * \brief Pops a job from the queue
        * \returns a job or nullptr
        */
        Job* pop() {
            Job* head = m_head.load(std::memory_order_relaxed);
            if (head == nullptr) return nullptr;

            if constexpr (FIFO) {
                while (head->m_next) {
                    Job* last = head;
                    head = head->m_next;
                    if (!head->m_next) {
                        last->m_next = nullptr;
                        return head;
                    }
                }
            }

            while (head != nullptr && !std::atomic_compare_exchange_weak(&m_head, &head, head->m_next)) {};  
            return head;
        };
    };


    /**
    * \brief The main JobSystem class manages the whole VGJS job system
    *
    * The JobSystem starts N threads and provides them with data structures.
    * It can add new jobs, and wait until they are done.
    */
    class JobSystem {

    private:
        static inline std::unique_ptr<JobSystem>    m_instance;	            ///<pointer to singleton
        std::vector<std::unique_ptr<std::thread>>	m_threads;	            ///< array of thread structures
        uint32_t						            m_thread_count = 0;     ///< number of threads in the pool
        uint32_t									m_start_idx = 0;        ///< idx of first thread that is created
        static inline thread_local uint32_t		    m_thread_index;			///< each thread has its own number
        std::atomic<bool>							m_terminate = false;	///< Flag for terminating the pool
        static inline thread_local Job*             m_current_job = nullptr;
        std::vector<std::unique_ptr<JobQueue<true>>> m_local_queues;	    ///< Each thread has its own Job queue, multiple produce, single consume
        std::unique_ptr<JobQueue<false>>            m_central_queue;        ///<Main central job queue is multiple produce multiple consume

    public:

        /**
        * \brief JobSystem class constructor
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        */
        JobSystem(uint32_t threadCount = 0, uint32_t start_idx = 0 ) {

            m_start_idx = start_idx;
            m_thread_count = threadCount;
            if (m_thread_count == 0) {
                m_thread_count = std::thread::hardware_concurrency();		///< main thread is also running
            }

            m_central_queue = std::make_unique<JobQueue<false>>();

            for (uint32_t i = 0; i < m_thread_count; i++) {
                m_local_queues.push_back(std::make_unique<JobQueue<true>>());	//local job queue
            }

            for (uint32_t i = start_idx; i < m_thread_count; i++) {
                std::cout << "Starting thread " << i << std::endl;
                m_threads.push_back(std::make_unique<std::thread>(&JobSystem::thread_task, this, i));	//spawn the pool threads
            }
        };

        /**
        * \brief Singleton access through class
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        * \returns a pointer to the JobSystem instance
        */
        static std::unique_ptr<JobSystem>& instance(uint32_t threadCount = 0, uint32_t start_idx = 0) {
            static std::once_flag once;
            std::call_once(once, [&]() { m_instance = std::make_unique<JobSystem>(threadCount, start_idx); });
            return m_instance;
        };

        /**
        * \brief Test whether the job system has been started yet
        * \returns true if the instance exists, else false
        */
        static bool instance_created() {
            return (m_instance != nullptr);
        };

        JobSystem(const JobSystem&) = delete;				// non-copyable,
        JobSystem& operator=(const JobSystem&) = delete;
        JobSystem(JobSystem&&) = default;					// but movable
        JobSystem& operator=(JobSystem&&) = default;

        /**
        * \brief JobSystem class destructor
        * 
        * By default shuts down the system and waits for the threads to terminate
        */
        ~JobSystem() {
            m_terminate = true;
            wait_for_termination();
        };

        /**
        * \brief every thread runs in this function
        * \param[in] threadIndex Number of this thread
        */
        void thread_task(uint32_t threadIndex = 0) {
            constexpr uint32_t NOOP = 20;
            m_thread_index = threadIndex;	                                //Remember your own thread index number
            static std::atomic<uint32_t> thread_counter = m_thread_count;	//Counted down when started

            thread_counter--;			                                    //count down
            while (thread_counter.load() > 0)	                            //Continue only if all threads are running
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));

            while (!m_terminate) {			                                //Run until the job system is terminated
                static uint32_t noop = NOOP;

                m_current_job = m_local_queues[m_thread_index]->pop();
                if (!m_current_job) {
                    m_current_job = m_central_queue->pop();
                }
                if (m_current_job) {
                    (*m_current_job)();
                    m_current_job->continuation();
                }
                else if (--noop == 0 && m_thread_index > 0) {
                    noop = NOOP;
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            };
        };

        /**
        * \brief Wait for termination of all jobs
        *
        * Can be called by the main thread to wait for all threads to terminate.
        * Returns as soon as all threads have exited.
        */
        void wait_for_termination() {
            for (std::unique_ptr<std::thread>& pThread : m_threads) {
                pThread->join();
            }
        };

        /**
        * \brief Get a pointer to the current job
        * \returns a pointer to the current job
        */
        Job* current_job() {
            return m_current_job;
        }

        /**
        * \brief Schedule a job into the job system
        * \param[in] job A pointer to the job to schedule
        * \param[in] thd Number of the thread to schedule the job to
        */
        void schedule(Job* job, int32_t thd = -1) {
            if (thd >= 0 && thd < (int)m_thread_count) {
                m_local_queues[m_thread_index]->push(job);
                return;
            }
            m_central_queue->push(job);
        };
    };
}







