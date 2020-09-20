


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
#include <fstream>
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
#include <chrono>
#include <string>
#include <sstream>

namespace vgjs {

    #define FUNCTION(f) [=](){f;}    //wrapper over a lambda function holding function f and parameters

    class Job_base;
    class JobSystem;

    bool is_logging();
    void log_data(  std::chrono::high_resolution_clock::time_point& t1, std::chrono::high_resolution_clock::time_point& t2,
                    int32_t exec_thread, bool finished, int32_t type, int32_t id);
    void save_log_file();



    struct Function {
        std::function<void(void)>   m_function = []() {};       //empty function
        int32_t                     m_thread_index = -1;        //thread that the f should run on
        int32_t                     m_type = -1;
        int32_t                     m_id = -1;

        Function(std::function<void(void)>&& f, int32_t thread_index = -1, int32_t type = -1, int32_t id = -1 ) 
            : m_function(std::move(f)), m_thread_index(thread_index), m_type(type), m_id(id) {};

        Function(std::function<void(void)>& f, int32_t thread_index = -1, int32_t type = -1, int32_t id = -1 )
            : m_function(f), m_thread_index(thread_index), m_type(type), m_id(id) {};

        Function(const Function& f) 
            : m_function(f.m_function), m_thread_index(f.m_thread_index), m_type(f.m_type), m_id(f.m_id) {};

        Function(Function& f) 
            : m_function(std::move(f.m_function)), m_thread_index(f.m_thread_index), m_type(f.m_type), m_id(f.m_id) {};

        Function& operator= (const Function& f) {
            m_function = f.m_function; m_thread_index = f.m_thread_index; m_type = f.m_type;  m_id = f.m_id;
        };

        Function& operator= (Function&& f) {
            m_function = std::move(f.m_function); m_thread_index = f.m_thread_index; m_type = f.m_type;  m_id = f.m_id;
        };
    };


    //-----------------------------------------------------------------------------------------


    /**
    * \brief Base class of coro task promises and jobs
    */
    class Job_base {
    public:
        Job_base*           m_next = nullptr;           //next job in the queue
        std::atomic<int>    m_children = 0;             //number of children this job is waiting for
        Job_base*           m_parent = nullptr;         //parent job that created this job
        Job_base*           m_continuation = nullptr;   //continuation follows this job (a coro is its own continuation)
        int32_t             m_thread_index = -1;        //thread that the job should run on and ran on
        int32_t             m_type = -1;
        int32_t             m_id = -1;

        virtual bool resume() = 0;                      //this is the actual work to be done
        virtual void operator() () noexcept {           //wrapper as function operator
            resume();
        }
        virtual bool deallocate() noexcept { return false; };    //called for deallocation
        virtual bool is_job() noexcept { return false; }         //test whether this is a job or e.g. a coro
    };


    /**
    * \brief Job class calls normal C++ functions, is allocated and deallocated, and can be reused
    */
    class Job : public Job_base {
    public:
        uint32_t                   m_create_thread = -1;        //thread that created this Job
        std::function<void(void)>  m_function;                  //empty function

        void reset() noexcept {         //call only if you want to wipe out the Job data
            m_next = nullptr;           //e.g. when recycling from a used Jobs queue
            m_children = 0;
            m_parent = nullptr;
            m_continuation = nullptr;
            m_thread_index = -1;
            m_type = -1;
            m_id = -1;
        }

        virtual bool resume() noexcept {    //work is to call the function
            m_children = 1;                 //job is its own child, so set to 1
            m_function();                   //run the function, can schedule more children here
            return true;
        }

        bool deallocate() noexcept { return true; };  //assert this is a job so it has been created by the job system
        bool is_job() noexcept { return true;  };     //assert that this is a job
    };


    struct JobLog {
        std::chrono::high_resolution_clock::time_point m_t1, m_t2;	///< execution start and end
        uint32_t        m_exec_thread;
        bool			m_finished;
        int32_t	        m_type;
        int32_t	        m_id;

        JobLog(std::chrono::high_resolution_clock::time_point& t1, std::chrono::high_resolution_clock::time_point& t2,
            int32_t exec_thread, bool finished, int32_t type, int32_t id)
                : m_t1(t1), m_t2(t2), m_exec_thread(exec_thread), m_finished(finished), m_type(type), m_id(id) {
        };
    };


    /**
    * \brief A lockfree queue.
    *
    * This queue can be accessed by any thread, it is synchronized by STL CAS operations.
    * If there is only ONE consumer, then FIFO can be set true, and the queue is a FIFO queue.
    * If FIFO is false the queue is a LIFO stack and there can be multiple consumers if need be.
    */
    template<typename JOB = Job_base>
    class JobQueue {
        friend JobSystem;
        std::pmr::memory_resource*  m_mr;               ///<use to allocate/deallocate Jobs
        std::atomic_flag            m_lock = ATOMIC_FLAG_INIT;
        JOB*                        m_head = nullptr;	//
        JOB*                        m_tail = nullptr;	//
        int32_t                     m_size = 0;

    public:

        JobQueue(std::pmr::memory_resource* mr) noexcept 
            : m_mr(mr), m_head(nullptr), m_tail(nullptr), m_size(0) {};	///<JobQueue class constructor

        JobQueue(const JobQueue<JOB>& queue) noexcept 
            : m_mr(queue.m_mr), m_head(nullptr), m_tail(nullptr), m_size(0) {};

        void deallocate(JOB* job) {
            if (job == nullptr) return;
            std::pmr::polymorphic_allocator<JOB> allocator(m_mr); //construct a polymorphic allocator
            job->~JOB();                                          //call destructor
            allocator.deallocate(job, 1);                         //use pma to deallocate the memory
        }

        uint32_t clear() {
            uint32_t res = m_size;
            JOB* job = pop();                         //deallocate jobs that run a function
            while (job != nullptr) {                                //because they were allocated by the JobSystem
                if (job->deallocate()) {        //if this is a coro it will destroy itself and return false, a Job returns true
                    deallocate(job);
                }
                job = pop();
            }
            return res;
        }

        ~JobQueue() {
            clear();
        }

        uint32_t size() {
            return m_size;
        }

        /**
        * \brief Pushes a job onto the queue tail
        * \param[in] pJob The job to be pushed into the queue
        */
        void push(JOB* job) {
            while (m_lock.test_and_set(std::memory_order::acquire));  // acquire lock

            job->m_next = nullptr;
            if (m_head == nullptr) {
                m_head = job;
            }
            if (m_tail == nullptr) {
                m_tail = job;
            }
            else {
                m_tail->m_next = job;
                m_tail = job;
            }

            m_size++;
            m_lock.clear(std::memory_order::release);
        };

        /**
        * \brief Pops a job from the queue
        * \returns a job or nullptr
        */
        JOB* pop() {
            if (m_head == nullptr) return nullptr;

            while (m_lock.test_and_set(std::memory_order::acquire));  // acquire lock

            JOB* head = m_head;
            if (head != nullptr) {
                m_head = (JOB*)head->m_next;
                m_size--;
                if (head == m_tail) {
                    m_tail = nullptr;
                }
            }
            m_lock.clear(std::memory_order::release);

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
        const uint32_t                              c_queue_capacity = 100;

    private:
        std::pmr::memory_resource*                  m_mr;                   ///<use to allocate/deallocate Jobs
        static inline std::unique_ptr<JobSystem>    m_instance;	            ///<pointer to singleton
        std::vector<std::thread>	                m_threads;	            ///<array of thread structures
        std::atomic<uint32_t>   		            m_thread_count = 0;     ///<number of threads in the pool
        std::atomic<bool>                           m_terminated = false;   ///
        uint32_t									m_start_idx = 0;        ///<idx of first thread that is created
        static inline thread_local  int32_t		    m_thread_index = -1;    ///<each thread has its own number
        std::atomic<bool>							m_terminate = false;	///<Flag for terminating the pool
        static inline thread_local Job_base*        m_current_job = nullptr;///<Pointer to the current job of this thread
        std::vector<JobQueue<Job_base>>             m_global_queues;	        ///<each thread has its own Job queue, multiple produce, single consume
        std::vector<JobQueue<Job_base>>             m_local_queues;	        ///<each thread has its own Job queue, multiple produce, single consume
        std::pmr::vector<JobQueue<Job>>             m_recycle;              ///<save old jobs for recycling
        std::pmr::vector<JobQueue<Job>>             m_delete;               ///<save old jobs for recycling
        std::pmr::vector<std::pmr::vector<JobLog>>	m_logs;				    ///< log the start and stop times of jobs
        bool                                        m_logging = false;      ///< if true then jobs will be logged
        std::map<int32_t, std::string>              m_types;                ///<map types to a string for logging
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time = std::chrono::high_resolution_clock::now();	//time when program started

        std::atomic<uint32_t> m_created_jobs = 0;
        std::atomic<uint32_t> m_scheduled_jobs = 0;
        std::atomic<uint32_t> m_run_jobs = 0;
        std::atomic<uint32_t> m_finished_jobs = 0;
        std::atomic<uint32_t> m_recycled_jobs = 0;
        std::atomic<uint32_t> m_deleted_jobs = 0;

        /**
        * \brief Allocate a job so that it can be scheduled.
        * 
        * If there is a job in the recycle queue we use this. Else a new
        * new Job struct is allocated from the memory resource m_mr
        * 
        * \param[in] f Function that should be executed by the job.
        * \returns a pointer to the job.
        */
        Job* allocate_job() noexcept {
            Job* job = nullptr;
            if (m_thread_index >= 0) {
                job = m_recycle[m_thread_index].pop();                                //try recycle queue
            }
            if (job == nullptr ) {                                                         //none found
                m_created_jobs++;
                std::pmr::polymorphic_allocator<Job> allocator(m_mr);           //use this allocator
                job = allocator.allocate(1);                                    //allocate the object
                new (job) Job();                                                //call constructor
                job->m_create_thread = m_thread_index >= 0 ? m_thread_index : 0;
            }
            else {                                  //job found
                job->reset();                       //reset it
            }
            return job;
        }

        Job* allocate_job( Function&& f) noexcept {
            Job* job            = allocate_job();
            job->m_function     = std::move(f.m_function);    //move the job
            job->m_thread_index = f.m_thread_index;
            job->m_type         = f.m_type;
            job->m_id           = f.m_id;
            return job;
        }

    public:

        /**
        * \brief JobSystem class constructor
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        */
        JobSystem(uint32_t threadCount = 0, uint32_t start_idx = 0, std::pmr::memory_resource *mr = std::pmr::new_delete_resource() ) noexcept
            : m_mr(mr), m_recycle(mr), m_delete(mr) { 

            m_start_idx = start_idx;
            m_thread_count = threadCount;
            if (m_thread_count == 0) {
                m_thread_count = std::thread::hardware_concurrency();		///< main thread is also running
            }

            for (uint32_t i = 0; i < m_thread_count; i++) {
                m_global_queues.push_back(JobQueue<Job_base>(mr));     //global job queue
                m_local_queues.push_back(JobQueue<Job_base>(mr));     //local job queue
                m_recycle.push_back(JobQueue<Job>(mr));
                m_delete.push_back( JobQueue<Job>(mr));
            }

            for (uint32_t i = start_idx; i < m_thread_count; i++) {
                std::cout << "Starting thread " << i << std::endl;
                m_threads.push_back(std::thread(&JobSystem::thread_task, this, i));	//spawn the pool threads
                m_threads[i].detach();
            }

            m_logs.resize(m_thread_count, std::pmr::vector<JobLog>{mr});    //make room for the log files
        };

        /**
        * \brief Singleton access through class
        * \param[in] threadCount Number of threads in the system
        * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
        * \returns a pointer to the JobSystem instance
        */
        static std::unique_ptr<JobSystem>& instance(uint32_t threadCount = 0, uint32_t start_idx = 0, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) noexcept {
            static std::once_flag once;
            std::call_once(once, [&]() { m_instance = std::make_unique<JobSystem>(threadCount, start_idx, mr); });
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

        void on_finished(Job_base* job) noexcept;            //called when the job finishes, i.e. all children have finished

        /**
        * \brief Child tells its parent that it has finished
        *
        * A child that finished calls this function of its parent, thus decreasing
        * the number of left children by one. If the last one finishes (including the
        * parent itself) then the parent also finishes (and may call its own parent).
        * Note that a Job is also its own child, so it must have returned from
        * its function before on_finished() is called.
        */
        inline bool child_finished(Job_base* job) noexcept {
            uint32_t num = job->m_children.fetch_sub(1);
            if (num <= 1) {
                on_finished(job);
                return true;
            }
            return false;
        }

        /**
        * \brief every thread runs in this function
        * \param[in] threadIndex Number of this thread
        */
        void thread_task(int32_t threadIndex = 0) noexcept {
            constexpr uint32_t NOOP = 100;                                   //number of empty loops until threads sleeps
            m_thread_index = threadIndex;	                                //Remember your own thread index number
            static std::atomic<uint32_t> thread_counter = m_thread_count.load();	//Counted down when started

            thread_counter--;			                                    //count down
            while (thread_counter.load() > 0)	                            //Continue only if all threads are running
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));

            uint32_t next = rand() % m_thread_count;
            thread_local uint32_t noop = NOOP;                               //number of empty loops until threads sleeps
            while (!m_terminate) {			                                //Run until the job system is terminated
                m_current_job = m_local_queues[m_thread_index].pop();      //try get a job from the local queue
                if (m_current_job == nullptr) {
                    m_current_job = m_global_queues[m_thread_index].pop();
                }
                if (m_current_job == nullptr) {
                    next = ++next % m_thread_count;
                    m_current_job = m_global_queues[next].pop();
                }

                if (m_current_job != nullptr) {
                    std::chrono::high_resolution_clock::time_point t1, t2;	///< execution start and end

                    if (is_logging()) {
                        t1 = std::chrono::high_resolution_clock::now();	//time of finishing
                    }

                    m_run_jobs++;
                    bool is_job = m_current_job->is_job(); //coro might destroy itself if parent is a function!
                    int32_t type = m_current_job->m_type;
                    int32_t id = m_current_job->m_id;

                    (*m_current_job)();                                    //if any job found execute it

                    if (is_logging()) {
                        t2 = std::chrono::high_resolution_clock::now();	//time of finishing
                        log_data(t1, t2, m_thread_index, false, type, id);
                    }

                    if (is_job) {                      //job is its own child
                        child_finished(m_current_job);                     //a coro might just be suspended by co_await
                    }
                }
                --noop;
                if (noop == 0) {               //if none found too longs let thread sleep
                    noop = NOOP;                                            //thread 0 goes on to make the system reactive
                    //std::cout << "Before " << m_thread_index << " " << m_delete[m_thread_index].size() << "\n";

                    uint32_t cnt = m_delete[m_thread_index].clear();
                    //m_deleted_jobs.fetch_add( cnt );

                    /*uint32_t created = m_created_jobs;
                    uint32_t scheduled = m_scheduled_jobs;
                    uint32_t run = m_run_jobs;
                    uint32_t finished = m_finished_jobs;
                    uint32_t recycled = m_recycled_jobs;
                    uint32_t deleted = m_deleted_jobs;

                    std::cout << "Created:   " << created << std::endl;
                    std::cout << "Scheduled: " << scheduled << std::endl;
                    std::cout << "Run:       " << run << std::endl;
                    std::cout << "Finished:  " << finished << std::endl;
                    std::cout << "Recycled:  " << recycled << std::endl;
                    std::cout << "Deleted:   " << deleted << "\n\n\n";*/

                    //std::cout << "After " << m_thread_index << " " << m_delete[m_thread_index].size() << "\n";
                    //std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            };

           std::cout << "Thread " << m_thread_index << " left " << m_thread_count << "\n";

           Job_base* job;
           while ((job = m_global_queues[m_thread_index].pop()) != nullptr) {
               if (job->is_job()) m_delete[((Job*)job)->m_create_thread].push((Job*)job);
           }
           while ((job = m_local_queues[m_thread_index].pop()) != nullptr) {
               if (job->is_job()) m_delete[((Job*)job)->m_create_thread].push((Job*)job);
           }
           while ((job = m_recycle[m_thread_index].pop()) != nullptr) {
               if (job->is_job()) m_delete[((Job*)job)->m_create_thread].push((Job*)job);
           }

           uint32_t num = m_thread_count.fetch_sub(1);  //last thread clears all queues (or else coros are destructed bevore queues!)
           while (m_thread_count.load() > 0) {	                            //Continue only if all threads are running
               std::this_thread::sleep_for(std::chrono::nanoseconds(100));
           }

           m_delete[m_thread_index].clear();

           if (num == 1) {
               if (m_logging) {
                   save_log_file();
               }
               std::cout << "Last thread " << m_thread_index << " terminated\n";
               m_terminated = true;
           }
        };

        void recycle(Job*job) noexcept {
            if (m_recycle[job->m_create_thread].size() <= c_queue_capacity) {
                m_recycle[job->m_create_thread].push(job);        //save it so it can be reused later
            }
            else {
                m_delete[job->m_create_thread].push(job);   //push to the current thread (the child calls parent on thread m_thread_index)
            }
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
            while (m_terminated.load() == false) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        * \brief Get the thread index the current job is running on
        * \returns the index of the thread the current job is running on, or -1
        */
        int32_t thread_index() {
            return m_thread_index;
        }

        /**
        * \brief Get the number of threads in the system
        * \returns the number of threads in the system
        */
        uint32_t thread_count() {
            return m_thread_count;
        }

        /**
        * \brief Schedule a job into the job system
        * \param[in] job A pointer to the job to schedule
        */
        void schedule(Job_base* job) noexcept {
            assert(job!=nullptr);
            m_scheduled_jobs++;

            if (job->m_thread_index < 0 || job->m_thread_index >= (int)m_thread_count ) {
                job->m_thread_index = rand() % m_thread_count;
            }

            m_local_queues[job->m_thread_index].push(job);
        };

        /**
        * \brief Schedule a job into the job system
        * \param[in] source An external source that is copied into the scheduled job
        */
        void schedule(Function&& source) noexcept {
            Job *job = allocate_job( std::forward<Function>(source) );
            job->m_parent = m_current_job;
            if (m_current_job != nullptr) {         //if there is a parent, increase its number of children by one
                m_current_job->m_children++;
            }
            schedule(job);
        };

        /**
        * \brief Store a continuation for the current Job. Will be scheduled once the current Job finishes
        * \param[in] f The function to schedule
        */
        void continuation( Function&& f ) noexcept {
            Job_base* current = current_job();
            if (current == nullptr || !current->is_job()) {
                return;
            }
            ((Job*)current)->m_continuation = allocate_job(std::forward<Function>(f));
            //std::cout << "Set Continuation\n";
        }

        //-----------------------------------------------------------------------------------------

        /**
        * \brief Get the logging data so it can be saved to file
        * \returns a reference to the logging data.
        */
        auto& get_logs() {
            return m_logs;
        }

        /**
        * \brief Clear all logs
        */
        void clear_logs() {
            for (auto& log : m_logs) {
                log.clear();
            }
        }

        /**
        * \brief Enable/Disable logging.
        * 
        * If logging is enabled, start/stop times and other data of each thread is saved
        * in a memory data structure
        * \param[in] flag True or false
        */
        void enable_logging() {
            m_logging = true;
        }

        void disable_logging() {
            if (m_logging) {
                save_log_file();
            }
            m_logging = false;
        }

        /**
        * \brief Ask whether logging is currently enabled or not
        * \returns true or false
        */
        bool is_logging() {
            return m_logging;
        }

        /**
        * \brief Get the the time when the job system was started (for logging)
        * \returns the time the job system was started
        */
        auto start_time() {
            return m_start_time;
        }

        /**
        * \brief Get the mapping between type number and a string explaining the type.
        * 
        * Jobs can have a type integer that ids the type, like a function or a coro.
        * If logging is to be used, this map should be filled with strings explaining the types.
        * This should be done before using the job system.
        * \returns a std::map that maps type integers to strings
        */
        auto& types() {
            return m_types;
        }

    };

    //----------------------------------------------------------------------------------------------

    /**
    * \brief A job and all its children have finished
    *
    * This is called when a job and its children has finished.
    * If there is a continuation stored in the job, then the continuation
    * gets scheduled. Also the job's parent is notified of this new child.
    * Then, if there is a parent, the parent's child_finished() function is called.
    */
    inline void JobSystem::on_finished(Job_base *job) noexcept {
        m_finished_jobs++;

        if (job->m_continuation != nullptr) {						 //is there a successor Job?
            
            if (job->is_job() && job->m_parent != nullptr) {         //is this a job and there is a parent?                
                job->m_parent->m_children++;
                job->m_continuation->m_parent = job->m_parent;       //add successor as child to the parent
            }

            schedule(job->m_continuation);    //schedule the successor 
        }

        if (job->is_job() ) {
            if (job->m_parent != nullptr) {		//if there is parent then inform it	
                child_finished(job->m_parent);	//if this is the last child job then the parent will also finish
            }
            recycle((Job*)job);  //last command in function
        }
    }


    //----------------------------------------------------------------------------------

    /**
    * \brief Schedule a function into the system.
    * \param[in] f A function to schedule
    */
    inline void schedule( Function&& f ) noexcept {
        JobSystem::instance()->schedule( std::forward<Function>(f) );
    }

    /**
    * \brief Schedule a function into the system.
    * \param[in] f A function to schedule
    */
    inline void schedule(Function& f) noexcept {
        Function func( f );              //make a copy
        schedule( std::move(func) );     //call schedule() above to make sure that parent is taken care of
    }

    /**
    * \brief Schedule a function into the system.
    * \param[in] f A function to schedule
    */
    inline void schedule( std::function<void(void)>&& f ) noexcept {
        Function func(std::forward<std::function<void(void)>>(f)); //forward function
        JobSystem::instance()->schedule(std::move(func));
    };

    /**
    * \brief Schedule a function into the system.
    * \param[in] f A function to schedule
    */
    inline void schedule(std::function<void(void)>& f) noexcept {
        Function func(f);                                   //make a copy
        JobSystem::instance()->schedule(std::move(func));   //move the copy
    };


    /**
    * \brief Schedule functions into the system. T can be a Function, std::function or a task<U>
    * \param[in] functions A vector of functions to schedule
    */
    template<typename T>
    inline void schedule( std::pmr::vector<T>& functions ) noexcept {
        for (auto& f : functions) {
            schedule( f );
        }
    };

    /**
    * \brief Store a continuation for the current Job. The continuation will be scheduled once the job finishes.
    * \param[in] f A function to schedule
    * \param[in] thd Thread index to schedule to
    */
    inline void continuation(Function&& f) noexcept {
        JobSystem::instance()->continuation(std::forward<Function>(f)); //forward the rvalue
    }

    inline void continuation(Function& f) noexcept {
        Function func(f);                            //make a copy
        continuation(std::forward<Function>(func));  //move the copy
    }

    inline void continuation(std::function<void(void)>&& f) noexcept {
        Function func( std::forward<std::function<void(void)>>(f));
        continuation( std::move(func) );
    }

    inline void continuation(std::function<void(void)>& f) noexcept {
        Function func(f);
        continuation(std::move(func));
    }

    //----------------------------------------------------------------------------------

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

    inline bool is_logging() {
        return JobSystem::instance()->is_logging();
    }

    inline void log_data(
        std::chrono::high_resolution_clock::time_point& t1, std::chrono::high_resolution_clock::time_point& t2,
        int32_t exec_thread, bool finished, int32_t type, int32_t id) {

        auto& logs = JobSystem::instance()->get_logs();
        logs[JobSystem::instance()->thread_index()].emplace_back( t1, t2, JobSystem::instance()->thread_index(), finished, type, id);
    }

    inline void save_job(  std::ofstream& out, std::string cat, uint64_t pid, uint64_t tid,
                           uint64_t ts, int64_t dur, std::string ph, std::string name, std::string args) {

        std::stringstream time;
        time.precision(15);
        time << ts / 1.0e6;

        std::stringstream duration;
        duration.precision(15);
        duration << dur / 1.0e6;

        out << "{";
        out << "\"cat\": " << cat << ", ";
        out << "\"pid\": " << pid << ", ";
        out << "\"tid\": " << tid << ", ";
        out << "\"ts\": " << time.str() << ", ";
        out << "\"dur\": " << duration.str() << ", ";
        out << "\"ph\": " << ph << ", ";
        out << "\"name\": " << name << ", ";
        out << "\"args\": {" << args << "}";
        out << "}";
    }

    inline void save_log_file() {
        auto& logs = JobSystem::instance()->get_logs();
        std::ofstream outdata;
        outdata.open("log.json");
        auto& types = JobSystem::instance()->types();

        if (outdata) {
            outdata << "{" << std::endl;
            outdata << "\"traceEvents\": [" << std::endl;
            bool comma = false;
            for (uint32_t i = 0; i < logs.size(); ++i) {
                //if (i > 0 && logs[i - 1].empty()) comma = false;
                //if (comma) outdata << "," << std::endl;
                //comma = true;

                //bool comma2 = false;
                for (auto& ev : logs[i]) {
                    if (ev.m_t1 >= JobSystem::instance()->start_time() && ev.m_t2 >= ev.m_t1) {
                        //if (comma2) outdata << "," << std::endl;
                        //comma2 = true;

                        if (comma) outdata << "," << std::endl;

                        auto it = types.find(ev.m_type);
                        std::string name = "-";
                        if (it != types.end()) name = it->second;

                        save_job(outdata, "\"cat\"", 0, (uint32_t)ev.m_exec_thread,
                            std::chrono::duration_cast<std::chrono::nanoseconds>(ev.m_t1 - JobSystem::instance()->start_time()).count(),
                            std::chrono::duration_cast<std::chrono::nanoseconds>(ev.m_t2 - ev.m_t1).count(),
                            "\"X\"", "\"" + name + "\"", "\"finished\": " + std::to_string(ev.m_finished));

                        comma = true;
                    }
                    //comma2 = false;
                }
            }
            outdata << "]," << std::endl;
            outdata << "\"displayTimeUnit\": \"ms\"" << std::endl;
            outdata << "}" << std::endl;
        }
        outdata.close();
        JobSystem::instance()->clear_logs();
    }



}




