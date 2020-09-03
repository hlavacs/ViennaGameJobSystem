


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
    * \brief Base class of coro task promises and jobs
    */
    class Job_base {
    public:
        Job_base*           m_next = nullptr;           //next job in the queue
        std::atomic<int>    m_children = 1;             //includes the job itself
        Job_base*           m_parent = nullptr;         //parent job that created this job
        int32_t             m_thread_index = -1;        //thread that the job should run on

        virtual bool resume() = 0;                      //this is the actual work to be done
        virtual void operator() () noexcept {           //wrapper as function operator
            resume();
        }
        virtual void child_finished() = 0;
        virtual bool recycle() { return false; }        //default is do not recycle, e.g. tasks
    };


    /**
    * \brief Job class calls normal C++ functions and can have continuations
    */
    class Job : public Job_base {
    public:
        Job*                        m_continuation = nullptr;   //continuation follows this job
        std::function<void(void)>   m_function = []() {};       //empty function

        Job(std::function<void(void)> && f) : m_function(std::move(f)) {};

        void reset() {                  //call only if you want to wipe out the Job data
            m_next = nullptr;           //e.g. when recycling from a used Jobs queue
            m_children = 1;
            m_parent = nullptr;
            m_thread_index = -1;
            m_continuation = nullptr;
            m_function = []() {};
        }

        virtual bool resume() {                 //work is to call the function
            m_children = 1;
            m_function();
            if (m_children.fetch_sub(1) == 1) { //reduce number of children by one
                on_finished();                  //if no more children, then finish
            }
            return true;
        }

        void on_finished() noexcept;            //called when the job finishes, i.e. all children have finished
        void child_finished() noexcept;         //child calls parent to notify that it has finished
        virtual bool recycle() { return true; } //recycle in recycle queue so its not destroyed
    };



    /**
    * \brief A lockfree queue.
    *
    * This queue can be accessed by any thread, it is synchronized by STL CAS operations.
    * If there is only ONE consumer, then FIFO can be set true, and the queue is a FIFO queue.
    * If FIFO is false the queue is a LIFO stack and there can be multiple consumers if need be.
    */
    template<typename JOB = Job_base, bool FIFO = false>
    class JobQueue {

        std::atomic<JOB*> m_head = nullptr;	///< Head of the stack

    public:

        JobQueue() {};	///<JobQueue class constructor

        /**
        * \brief Pushes a job onto the queue
        * \param[in] pJob The job to be pushed into the queue
        */
        void push(JOB* pJob) {
            pJob->m_next = m_head.load(std::memory_order_relaxed);
            while (!std::atomic_compare_exchange_weak(&m_head, (JOB**)&pJob->m_next, pJob)) {};
        };

        /**
        * \brief Pops a job from the queue
        * \returns a job or nullptr
        */
        JOB* pop() {
            JOB* head = m_head.load(std::memory_order_relaxed);
            if (head == nullptr) return nullptr;

            if constexpr (FIFO) {                   //if this is a FIFO queue there can only be one consumer!
                while (head->m_next) {              //as long as there are more jobs in the list
                    JOB* last = head;               //remember job before that
                    head = head->m_next;            //goto next job
                    if (!head->m_next) {            //if this is the last job
                        last->m_next = nullptr;     //dequeue it
                        return head;                //return it
                    }
                }
            }
            //if LIFO or there is only one Job then deqeue it from the start
            //this might collide with other producers, so use CAS
            //in rare cases FIFO might be violated
            while (head != nullptr && !std::atomic_compare_exchange_weak(&m_head, &head, (JOB*)head->m_next)) {};
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
        std::pmr::memory_resource*                  m_mr;                   ///<use to allocate/deallocate Jobs
        static inline std::unique_ptr<JobSystem>    m_instance;	            ///<pointer to singleton
        std::vector<std::unique_ptr<std::thread>>	m_threads;	            ///< array of thread structures
        uint32_t						            m_thread_count = 0;     ///< number of threads in the pool
        uint32_t									m_start_idx = 0;        ///< idx of first thread that is created
        static inline thread_local uint32_t		    m_thread_index;			///< each thread has its own number
        std::atomic<bool>							m_terminate = false;	///< Flag for terminating the pool
        static inline thread_local Job_base* m_current_job = nullptr;
        std::vector<std::unique_ptr<JobQueue<Job_base,true>>>   m_local_queues;	    ///< Each thread has its own Job queue, multiple produce, single consume
        std::unique_ptr<JobQueue<Job_base,false>>               m_central_queue;    ///<Main central job queue is multiple produce multiple consume
        std::unique_ptr<JobQueue<Job,false>>                    m_recycle;          ///<save old jobs for recycling

    public:

        /**
        * \brief JobSystem class constructor
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        */
        JobSystem(uint32_t threadCount = 0, uint32_t start_idx = 0, std::pmr::memory_resource *mr = std::pmr::get_default_resource() ) 
            : m_mr(mr) {

            m_start_idx = start_idx;
            m_thread_count = threadCount;
            if (m_thread_count == 0) {
                m_thread_count = std::thread::hardware_concurrency();		///< main thread is also running
            }

            m_central_queue = std::make_unique<JobQueue<Job_base,false>>();
            m_recycle = std::make_unique<JobQueue<Job, false>>();

            for (uint32_t i = 0; i < m_thread_count; i++) {
                m_local_queues.push_back(std::make_unique<JobQueue<Job_base,true>>());	//local job queue
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
            constexpr uint32_t NOOP = 20;                                   //number of empty loops until threads sleeps
            m_thread_index = threadIndex;	                                //Remember your own thread index number
            static std::atomic<uint32_t> thread_counter = m_thread_count;	//Counted down when started

            thread_counter--;			                                    //count down
            while (thread_counter.load() > 0)	                            //Continue only if all threads are running
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));

           thread_local uint32_t noop = NOOP;                               //number of empty loops until threads sleeps
           while (!m_terminate) {			                                //Run until the job system is terminated
                m_current_job = m_local_queues[m_thread_index]->pop();      //try get a job from the local queue
                if (!m_current_job) {
                    m_current_job = m_central_queue->pop();                 //if none found try the central queue
                }
                if (m_current_job) {
                    (*m_current_job)();                                     //if any job found execute it
                    if (m_current_job->recycle()) {
                        m_recycle->push((Job*)m_current_job);
                    }
                }
                else if (--noop == 0 && m_thread_index > 0) {               //if none found too longs let thread sleep
                    noop = NOOP;
                    std::this_thread::sleep_for(std::chrono::microseconds(5));
                }
            };
        };

        /**
        * \brief Terminate the job system
        */
        void terminate() {
            m_terminate = true;
        }

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
        Job_base* current_job() {
            return m_current_job;
        }

        /**
        * \brief Schedule a job into the job system
        * \param[in] job A pointer to the job to schedule
        */
        void schedule(Job_base* job) {
            if (job->m_thread_index >= 0 && job->m_thread_index < (int)m_thread_count) {
                m_local_queues[job->m_thread_index]->push(job);
                return;
            }
            m_central_queue->push(job);
        };


        /**
        * \brief Schedule a function into the job system
        * \param[in] f The function to schedule
        * \param[in] thread_index The thread to run the function
        */
        void schedule( std::function<void(void)> && f, int32_t thread_index = -1 ) {
            Job* job = m_recycle->pop();
            if (!job) {
                std::pmr::polymorphic_allocator<Job> allocator(m_mr);
                job = allocator.allocate(1);                                    //allocate the object
                new (job) Job(std::forward< std::function<void(void)>>(f));      //call constructor
            }
            else {
                job->reset();
                job->m_function = std::move(f);
            }
            job->m_thread_index = thread_index;
            schedule(job);
        }

    };

    /**
    * \brief A job and all its children have finished
    *
    * This is called when a job and its children has finished.
    * If there is a continuation stored in the job, then the continuation
    * gets scheduled. Also the job's parent is notified of this new child.
    * Then, if there is a parent, the parent's child_finished() function is called.
    */
    inline void Job::on_finished() noexcept {

        if (m_continuation != nullptr) {						//is there a successor Job?
            if (m_parent != nullptr) {                          //is there a parent?
                m_parent->m_children++;
                m_continuation->m_parent = m_parent;            //add successor as child to the parent
            }
            JobSystem::instance()->schedule(m_continuation);    //schedule the successor 
        }

        if (m_parent != nullptr) {		//if there is parent then inform it	
            m_parent->child_finished();	//if this is the last child job then the parent will also finish
        }
    }

    /**
    * \brief Child tells its parent that it has finished
    * 
    * A child that finished calls this function of its parent, thus decreasing
    * the number of left children by one. If the last one finishes (including the 
    * parent itself) then the parent also finishes (and may call its own parent).
    * Note that a Job is also its own child, so it must have also finished before
    * on_finished() is called.
    */
    inline void Job::child_finished() noexcept {
        if (m_children.fetch_sub(1) == 1) {
            on_finished();
        }
    }

    /**
    * \brief Schedule a task promise into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    * \param[in] thread_index Optional thread index to run the task
    */
    template<typename T>
    requires (std::is_base_of<Job, T>::value)
    inline void schedule(T* job) noexcept {
        JobSystem::instance()->schedule(job);
    };

    inline void schedule(std::function<void(void)>&& f, int32_t thd = -1 ) noexcept {
        JobSystem::instance()->schedule(std::forward<std::function<void(void)>>(f), thd );
    };


    /**
    * \brief Terminate the job system
    */
    inline void terminate() {
        JobSystem::instance()->terminate();
    }

    /**
    * \brief Wait for the job system to terminate
    */
    inline void wait_for_termination() {
        JobSystem::instance()->wait_for_termination();
    }

}




