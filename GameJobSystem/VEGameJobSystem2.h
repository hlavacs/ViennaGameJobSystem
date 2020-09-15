


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

    struct Function {
        Job_base*                   m_parent = nullptr;
        std::function<void(void)>   m_function = []() {};       //empty function
        int32_t                     m_thread_index = -1;        //thread that the f should run on
        int32_t                     m_type = -1;
        int32_t                     m_id = -1;

        Function(std::function<void(void)>&& f, int32_t thread_index, int32_t type, int32_t id, Job_base* parent = nullptr) 
            : m_function(std::move(f)), m_thread_index(thread_index), m_type(type), m_id(id), m_parent(parent) {};
    };

    void saveLogfile();

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
        std::chrono::high_resolution_clock::time_point t1, t2;	///< execution start and end

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

        void reset() noexcept {         //call only if you want to wipe out the Job data
            m_next = nullptr;           //e.g. when recycling from a used Jobs queue
            m_parent = nullptr;
            m_thread_index = -1;
            m_type = -1;
            m_id = -1;
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
        std::pmr::vector<std::pmr::vector<JobLog>>	m_logs;				    ///< log the start and stop times of jobs
        bool                                        m_logging = false;      ///< if true then jobs will be logged
        std::map<int32_t, std::string>              m_types;                ///<map types to a string for logging
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time = std::chrono::high_resolution_clock::now();	//time when program started


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
            Job* job = m_recycle.pop();                                         //try recycle queue
            if (!job) {                                                         //none found
                std::pmr::polymorphic_allocator<Job> allocator(m_mr);           //use this allocator
                job = allocator.allocate(1);                                    //allocate the object
                new (job) Job();                                                //call constructor
            }
            else {                                  //job found
                job->reset();                       //reset it
            }
            return job;
        }

        Job* allocate_job(std::function<void(void)>&& f) noexcept {
            Job* job = allocate_job();
            job->m_function = std::move(f);     //move the function
            return job;
        }

        Job* allocate_job( Function&& f) noexcept {
            Job* job            = allocate_job();
            job->m_parent       = f.m_parent;
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

            m_logs.resize(m_thread_count, std::pmr::vector<JobLog>{mr});    //make room for the log files
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
                    if (m_logging) {
                        m_current_job->t1 = std::chrono::high_resolution_clock::now();	//time of execution
                    }

                    (*m_current_job)();                                             //if any job found execute it

                    if (m_logging) {
                        m_current_job->t2 = std::chrono::high_resolution_clock::now();	//time of finishing
                        m_logs[m_thread_index].emplace_back(
                            m_current_job->t1, m_current_job->t2, m_thread_index, false, m_current_job->m_type, m_current_job->m_id);
                    }
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
               if (m_logging) {
                   saveLogfile();
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
        void schedule(Job_base* job) noexcept {
            if (job->m_thread_index >= 0 && job->m_thread_index < (int)m_thread_count) {
                m_local_queues[job->m_thread_index].push(job);
                return;
            }
            m_central_queue.push(job);
        };

        /**
        * \brief Schedule a job into the job system
        * \param[in] source An external source that is copied into the scheduled job
        */
        void schedule(Function&& source) noexcept {
            schedule( allocate_job( std::forward<Function>(source) ) );
        };

        /**
        * \brief Store a continuation for the current Job. Will be scheduled once the current Job finishes
        * \param[in] f The function to schedule
        */
        void continuation( Function&& f ) noexcept {
            auto current = current_job();
            if (current == nullptr || !current->is_job()) {
                return;
            }
            ((Job*)current)->m_continuation = allocate_job(std::forward<Function>(f));
        }

        auto& get_logs() {
            return m_logs;
        }

        void clear_logs() {
            for (auto& log : m_logs) {
                log.clear();
            }
        }

        void set_logging(bool flag) {
            m_logging = flag;
        }

        bool is_logging() {
            return m_logging;
        }

        uint32_t thread_index() {
            return m_thread_index;
        }

        uint32_t thread_count() {
            return m_thread_count;
        }

        auto start_time() {
            return m_start_time;
        }

        auto& types() {
            return m_types;
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
    * \param[in] type Type of the job
    * \param[in] id Unique id of the job
    */
    inline void schedule(Function&& f, int32_t thread_index = -1, int32_t type = -1, int32_t id = -1, Job_base* parent = nullptr) noexcept {
        f.m_thread_index    = (f.m_thread_index != -1   ? f.m_thread_index  : thread_index);
        f.m_type            = (f.m_type != -1           ? f.m_type          : type);
        f.m_id              = (f.m_id != -1             ? f.m_id            : id);
        f.m_parent          = (f.m_parent != nullptr    ? f.m_parent        : parent);

        f.m_parent = (f.m_parent != nullptr ? f.m_parent : JobSystem::instance()->current_job());
        if (f.m_parent != nullptr) {         //if there is a parent, increase its number of children by one
            f.m_parent->m_children++;
        }
        JobSystem::instance()->schedule( std::forward<Function>(f) );
    }

    inline void schedule(std::function<void(void)>&& f, int32_t thread_index = -1, int32_t type = -1, int32_t id = -1, Job_base* parent = nullptr) noexcept {
        Function func(std::forward<std::function<void(void)>>(f), thread_index, type, id, parent);
        schedule(std::move(func));
    };

    /**
    * \brief Schedule functions into the system. T can be a std::function or a task<U>
    * \param[in] functions A vector of functions to schedule
    * \param[in] thd Thread index to schedule to
    * \param[in] parent Parent job to use
    * \param[in] type Type of the job
    * \param[in] id Unique id of the job
    */
    template<typename T>
    inline void schedule(std::pmr::vector<T>& functions, int32_t thd = -1, int32_t type = -1, int32_t id = -1, Job_base* parent = nullptr) noexcept {
        int32_t cid = id;
        for (auto&& f : functions) {
            schedule(std::forward<T>(f), thd, type, cid++, parent);
        }
    };

    /**
    * \brief Store a continuation for the current Job. The continuation will be scheduled once the job finishes.
    * \param[in] f A function to schedule
    * \param[in] thd Thread index to schedule to
    */
    inline void continuation(Function&& f) noexcept {
        JobSystem::instance()->continuation(std::forward<Function>(f));
    }

    inline void continuation(std::function<void(void)>&& f, int32_t thd = -1, int32_t type = -1, int32_t id = -1) noexcept {
        Function func( std::forward<std::function<void(void)>>(f), thd, type, id );
        continuation( std::move(func) );
    }

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

    inline void saveJob(   std::ofstream& out, std::string cat, uint64_t pid, uint64_t tid,
                            uint64_t ts, int64_t dur, std::string ph, std::string name, std::string args) {

        std::stringstream time;
        time.precision(3);
        time << ts / 1000.0;

        std::stringstream duration;
        duration.precision(3);
        duration << dur / 1000.0;

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

    inline void saveLogfile() {
        auto& logs = JobSystem::instance()->get_logs();
        std::ofstream outdata;
        outdata.open("log.json");
        if (outdata) {
            outdata << "{" << std::endl;
            outdata << "\"traceEvents\": [" << std::endl;
            bool comma = false;
            for (uint32_t i = 0; i < JobSystem::instance()->thread_count(); ++i) {
                if (i > 0 && logs[i - 1].empty()) comma = false;
                if (comma) outdata << "," << std::endl;
                comma = true;

                bool comma2 = false;
                for (auto& ev : logs[i]) {
                    if (ev.m_t1 >= JobSystem::instance()->start_time() && ev.m_t2 >= ev.m_t1) {
                        if (comma2) outdata << "," << std::endl;
                        comma2 = true;
                        auto it = JobSystem::instance()->types().find(ev.m_type);
                        std::string name = "-";
                        if (it != JobSystem::instance()->types().end()) name = it->second;

                        saveJob(outdata, "\"cat\"", 0, (uint32_t)ev.m_exec_thread,
                            std::chrono::duration_cast<std::chrono::nanoseconds>(ev.m_t1 - JobSystem::instance()->start_time()).count(),
                            std::chrono::duration_cast<std::chrono::nanoseconds>(ev.m_t2 - ev.m_t1).count(),
                            "\"X\"", "\"" + name + "\"", "\"finished\": " + std::to_string(ev.m_finished));
                    }
                    bool comma2 = false;
                }
            }
            outdata << "]," << std::endl;
            outdata << "\"displayTimeUnit\": \"ns\"" << std::endl;
            outdata << "}" << std::endl;
        }
        outdata.close();
        JobSystem::instance()->clear_logs();
    }



}




