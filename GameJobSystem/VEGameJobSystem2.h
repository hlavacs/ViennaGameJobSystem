


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

    #define VGJS_FUNCTION(f) [=](){f;}    //wrapper over a lambda function holding function f and parameters

    //---------------------------------------------------------------------------------------------------


    /**
    * \brief Base class of coro task promises and jobs
    */
    class Job_base {
    public:
        Job_base*           m_next = nullptr;           //next job in the queue
        std::atomic<int>    m_children = 0;             //number of children this job is waiting for
        Job_base*           m_parent = nullptr;         //parent job that created this job
        int32_t             m_thread_index = -1;        //thread that the job should run on
        int32_t             m_type = -1;
        int32_t             m_id = -1;

        virtual bool resume() = 0;                      //this is the actual work to be done
        virtual void operator() () noexcept {           //wrapper as function operator
            resume();
        }
        virtual void child_finished() = 0;              //called by child when it finishes
        virtual bool deallocate() noexcept { return false; };    //called for deallocation
        virtual bool is_job() noexcept { return false; }         //test whether this is a job or e.g. a coro
    };


    /**
    * \brief Job class calls normal C++ functions and can have continuations
    */
    class Job : public Job_base {
    public:
        Job*                        m_continuation = nullptr;   //continuation follows this job
        std::function<void(void)>   m_function = []() {};       //empty function

        Job(std::function<void(void)>&& f) noexcept : m_function(std::move(f)) {}; //job is its own child

        void reset() noexcept {         //call only if you want to wipe out the Job data
            m_next = nullptr;           //e.g. when recycling from a used Jobs queue
            m_parent = nullptr;
            m_thread_index = -1;
            m_continuation = nullptr;
            m_function = []() {};
        }

        virtual bool resume() noexcept {    //work is to call the function
            m_children = 1;                 //job is its own child, so set to 1
            m_function();                   //run the function, can schedule more children here
            child_finished();               //job is its own child
            return true;
        }

        void on_finished() noexcept;            //called when the job finishes, i.e. all children have finished
        void child_finished() noexcept;         //child calls parent to notify that it has finished
        bool deallocate() noexcept { return true; };  //assert this is a job so it has been created by the job system
        bool is_job() noexcept { return true;  };     //assert that this is a job
    };

    class JobSystem;

    /**
    * \brief A lockfree queue.
    *
    * This queue can be accessed by any thread, it is synchronized by STL CAS operations.
    * If there is only ONE consumer, then FIFO can be set true, and the queue is a FIFO queue.
    * If FIFO is false the queue is a LIFO stack and there can be multiple consumers if need be.
    */
    template<typename JOB = Job_base, bool FIFO = false>
    class JobQueue {
        friend JobSystem;
        std::pmr::memory_resource* m_mr;        ///<use to allocate/deallocate Jobs
        std::atomic<JOB*> m_head;	            ///< Head of the stack

    public:

        JobQueue(std::pmr::memory_resource* mr, bool deallocate = true) noexcept : m_mr(mr), m_head(nullptr) {};	///<JobQueue class constructor
        JobQueue(const JobQueue<JOB, FIFO>& queue) noexcept : m_mr(queue.m_mr), m_head(nullptr) {};

        void clear() {
            JOB* job = (JOB*)m_head.load();                         //deallocate jobs that run a function
            while (job != nullptr) {                                //because they were allocated by the JobSystem
                JOB* next = (JOB*)job->m_next;
                if (job->deallocate()) {        //if this is a coro it will destroy itself and return false, a Job returns true
                    std::pmr::polymorphic_allocator<JOB> allocator(m_mr); //construct a polymorphic allocator
                    job->~JOB();                                          //call destructor
                    allocator.deallocate(job, 1);                         //use pma to deallocate the memory
                }
                job = next;
            }
            m_head = nullptr;
        }

        ~JobQueue() {}

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
        std::vector<std::thread>	                m_threads;	            ///<array of thread structures
        std::atomic<uint32_t>   		            m_thread_count = 0;     ///<number of threads in the pool
        uint32_t									m_start_idx = 0;        ///<idx of first thread that is created
        static inline thread_local uint32_t		    m_thread_index;			///<each thread has its own number
        std::atomic<bool>							m_terminate = false;	///<Flag for terminating the pool
        static inline thread_local Job_base*        m_current_job = nullptr;///<Pointer to the current job of this thread
        std::vector<JobQueue<Job_base,true>>        m_local_queues;	        ///<each thread has its own Job queue, multiple produce, single consume
        JobQueue<Job_base,false>                    m_central_queue;        ///<main central job queue is multiple produce multiple consume
        JobQueue<Job,false>                         m_recycle;              ///<save old jobs for recycling

        /**
        * \brief Allocate a job so that it can be scheduled.
        * 
        * If there is a job in the recycle queue we use this. Else a new
        * new Job struct is allocated from the memory resource m_mr
        * 
        * \param[in] f Function that should be executed by the job.
        * \returns a pointer to the job.
        */
        Job* allocate_job(std::function<void(void)>&& f) noexcept {
            Job* job = m_recycle.pop();                                         //try recycle queue
            if (!job) {                                                         //none found
                std::pmr::polymorphic_allocator<Job> allocator(m_mr);           //use this allocator
                job = allocator.allocate(1);                                    //allocate the object
                new (job) Job(std::forward<std::function<void(void)>>(f));      //call constructor
            }
            else {                                  //job found
                job->reset();                       //reset it
                job->m_function = std::move(f);     //move the function
            }
            return job;
        }

    public:

        /**
        * \brief JobSystem class constructor
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        */
        JobSystem(uint32_t threadCount = 0, uint32_t start_idx = 0, std::pmr::memory_resource *mr = std::pmr::get_default_resource() ) noexcept
            : m_mr(mr), m_central_queue(mr, true), m_recycle(mr, false) {

            m_start_idx = start_idx;
            m_thread_count = threadCount;
            if (m_thread_count == 0) {
                m_thread_count = std::thread::hardware_concurrency();		///< main thread is also running
            }

            for (uint32_t i = 0; i < m_thread_count; i++) {
                m_local_queues.push_back(JobQueue<Job_base, true>(mr, false));     //local job queue
            }

            for (uint32_t i = start_idx; i < m_thread_count; i++) {
                std::cout << "Starting thread " << i << std::endl;
                m_threads.push_back(std::thread(&JobSystem::thread_task, this, i));	//spawn the pool threads
                m_threads[i].detach();
            }
        };

        /**
        * \brief Singleton access through class
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        * \returns a pointer to the JobSystem instance
        */
        static std::unique_ptr<JobSystem>& instance(uint32_t threadCount = 0, uint32_t start_idx = 0) noexcept {
            static std::once_flag once;
            std::call_once(once, [&]() { m_instance = std::make_unique<JobSystem>(threadCount, start_idx); });
            return m_instance;
        };

        /**
        * \brief Test whether the job system has been started yet
        * \returns true if the instance exists, else false
        */
        static bool is_instance_created() noexcept {
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
        ~JobSystem() noexcept {
            m_terminate = true;
            wait_for_termination();
        };

        /**
        * \brief every thread runs in this function
        * \param[in] threadIndex Number of this thread
        */
        void thread_task(uint32_t threadIndex = 0) noexcept {
            constexpr uint32_t NOOP = 20;                                   //number of empty loops until threads sleeps
            m_thread_index = threadIndex;	                                //Remember your own thread index number
            static std::atomic<uint32_t> thread_counter = m_thread_count.load();	//Counted down when started

            thread_counter--;			                                    //count down
            while (thread_counter.load() > 0)	                            //Continue only if all threads are running
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));

           thread_local uint32_t noop = NOOP;                               //number of empty loops until threads sleeps
           while (!m_terminate) {			                                //Run until the job system is terminated
                m_current_job = m_local_queues[m_thread_index].pop();      //try get a job from the local queue
                if (!m_current_job) {
                    m_current_job = m_central_queue.pop();                 //if none found try the central queue
                }
                if (m_current_job) {
                    (*m_current_job)();                                     //if any job found execute it
                }
                else if (--noop == 0 && m_thread_index > 0) {               //if none found too longs let thread sleep
                    noop = NOOP;                                            //thread 0 goes on to make the system reactive
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            };

           uint32_t num = m_thread_count.fetch_sub(1);  //last thread clears all queues (or else coros are destructed bevore queues!)
           if (num == 1) {
               m_central_queue.clear();             //clear central queue
               m_recycle.clear();                   //clear recycle queue
               for (auto& q : m_local_queues) {     //clear local queues
                   q.clear();
               }
           }
        };

        void recycle(Job*job) noexcept {
            m_recycle.push(job);               //save it so it can be reused later
        }

        /**
        * \brief Terminate the job system
        */
        void terminate() noexcept {
            m_terminate = true;
        }

        /**
        * \brief Wait for termination of all jobs
        *
        * Can be called by the main thread to wait for all threads to terminate.
        * Returns as soon as all threads have exited.
        */
        void wait_for_termination() noexcept {
            while (m_thread_count > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        };

        /**
        * \brief Get a pointer to the current job
        * \returns a pointer to the current job
        */
        Job_base* current_job() noexcept {
            return m_current_job;
        }

        /**
        * \brief Schedule a job into the job system
        * \param[in] job A pointer to the job to schedule
        */
        void schedule(Job_base* job, uint32_t thd = -1, Job_base* parent = nullptr, int32_t type = -1, int32_t id = -1) noexcept {
            if (job->m_thread_index >= 0 && job->m_thread_index < (int)m_thread_count) {
                m_local_queues[job->m_thread_index].push(job);
                return;
            }
            m_central_queue.push(job);
        };

        /**
        * \brief Schedule a function into the job system
        * \param[in] f The function to schedule
        * \param[in] thread_index The thread to run the function
        */
        void schedule( std::function<void(void)> && f, int32_t thread_index = -1, Job_base* parent = nullptr, int32_t type = -1, int32_t id = -1) noexcept {
            Job* job = allocate_job(std::forward<std::function<void(void)>>(f));
            if (parent == nullptr) {
                parent = current_job();          //set parent
            }
            job->m_parent = parent;
            if (job->m_parent != nullptr) {         //if there is a parent, increase its number of children by one
                job->m_parent->m_children++;
            }
            job->m_type = type;
            job->m_id = id;
            job->m_thread_index = thread_index;
            schedule(job);
        }

        /**
        * \brief Store a continuation for the current Job. Will be scheduled once the current Job finishes
        * \param[in] f The function to schedule
        * \param[in] thread_index The thread to run the function
        */
        void continuation(std::function<void(void)>&& f, int32_t thread_index = -1, int32_t type = -1, int32_t id = -1) noexcept {
            auto current = current_job();
            if (!current->is_job()) {
                return;
            }
            Job* job = allocate_job(std::forward<std::function<void(void)>>(f));
            job->m_type = type;
            job->m_id = id;
            job->m_thread_index = thread_index;
            ((Job*)current)->m_continuation = job;
        }
    };

    //----------------------------------------------------------------------------------------------

    inline void recycle(Job* job) noexcept {
        JobSystem::instance()->recycle(job);               //save it so it can be reused later
    }

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

        recycle(this);  //last command in function so *this is no longer used (race against recycling)
    }

    /**
    * \brief Child tells its parent that it has finished
    * 
    * A child that finished calls this function of its parent, thus decreasing
    * the number of left children by one. If the last one finishes (including the 
    * parent itself) then the parent also finishes (and may call its own parent).
    * Note that a Job is also its own child, so it must have returned from 
    * its function before on_finished() is called.
    */
    inline void Job::child_finished() noexcept {
        uint32_t num = m_children.fetch_sub(1);
        if ( num == 1) {
            on_finished();
        }
    }

    /**
    * \brief Schedule a function into the system.
    * \param[in] f A function to schedule
    * \param[in] thd Thread index to schedule to
    * \param[in] parent Parent job to use
    */
    inline void schedule(std::function<void(void)>&& f, int32_t thd = -1, Job_base* parent = nullptr, int32_t type = -1, int32_t id = -1) noexcept {
        JobSystem::instance()->schedule(std::forward<std::function<void(void)>>(f), thd, parent, type, id );
    };

    /**
    * \brief Schedule functions into the system. T can be a std::function or a task<U>
    * \param[in] functions A vector of functions to schedule
    * \param[in] thd Thread index to schedule to
    */
    template<typename T>
    inline void schedule(std::pmr::vector<T>& functions, int32_t thd = -1, Job_base* parent = nullptr, int32_t type = -1, int32_t id = -1) noexcept {
        int32_t cid = id;
        for (auto&& f : functions) {
            schedule(std::forward<T>(f), thd, parent, type, id);
        }
    };

    /**
    * \brief Store a continuation for the current Job. The continuation will be scheduled once the job finishes.
    * \param[in] f A function to schedule
    * \param[in] thd Thread index to schedule to
    */
    inline void continuation(std::function<void(void)>&& f, int32_t thd = -1, int32_t type = -1, int32_t id = -1) noexcept {
        JobSystem::instance()->continuation(std::forward<std::function<void(void)>>(f), thd, type, id );
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




