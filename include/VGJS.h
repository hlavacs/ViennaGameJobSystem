#pragma once

#include <queue>
#include <latch>
#include <variant>

#include <iostream>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <functional>
#include <optional>
#include <condition_variable>
#include <queue>
#include <map>
#include <set>
#include <iterator>
#include <algorithm>
#include <assert.h>
#include <utility>
#include <tuple>
#include <numeric>

namespace simple_vgjs {

    using namespace std::experimental;

    /// <summary>
    /// A strong type must be explicitly created, and cannot be created by implicit conversion.
    /// </summary>
    /// <typeparam name="T">Value type.</typeparam>
    /// <typeparam name="D">Default value.</typeparam>
    /// <typeparam name="P">Phantom parameter to make type unique.</typeparam>
    template<typename T, auto D, int64_t P>
    struct strong_type_t {
        T value{D};
        strong_type_t() = default;
        explicit strong_type_t(const T& v) noexcept { value = v; };
        explicit strong_type_t(T&& v) noexcept { value = std::move(v); };
        operator const T& () const { return value; }
        operator T& () { return value; }
        strong_type_t<T, D, P>& operator=(const T& v) noexcept { value = v; return *this; };
        strong_type_t<T, D, P>& operator=(T&& v) noexcept { value = std::move(v); return *this; };
        strong_type_t<T, D, P>& operator=(const strong_type_t<T, D, P>& v) noexcept { value = v.value; return *this; };
        strong_type_t<T, D, P>& operator=(strong_type_t<T, D, P>&& v) noexcept { value = std::move(v.value); return *this; };
        strong_type_t(const strong_type_t<T, D, P>& v) noexcept { value = v.value; };
        strong_type_t(strong_type_t<T, D, P>&& v) noexcept { value = std::move(v.value); };
        struct hash {
            std::size_t operator()(const strong_type_t<T, D, P>& tag) const { return std::hash<T>()(tag.value); };
        };
    };

    using thread_count_t    = strong_type_t<int64_t, -1, 0>;
    using thread_index_t    = strong_type_t<int64_t, -1, 1>;
    using thread_id_t       = strong_type_t<int64_t, -1, 2>;
    using thread_type_t     = strong_type_t<int64_t, -1, 3>;
    using tag_t             = strong_type_t<int64_t, -1, 4>;

    //---------------------------------------------------------------------------------------------
    //Declaration of classes 

    template<typename T> struct awaitable_resume_on;
    template<typename PT, typename... Ts> struct awaitable_tuple;
    template<typename PT> struct awaitable_tag;
    template<typename U> struct final_awaiter;

    struct VgjsJob;
    using VgjsJobPointer = VgjsJob*;
    struct VgjsJobParent; 
    using VgjsJobParentPointer = VgjsJobParent*;
    template<typename T = void> class VgjsCoroPromiseBase;
    template<typename T = void> class VgjsCoroPromise;
    template<typename T = void> using VgjsCoroPromisePointer = VgjsCoroPromise<T>*;
    template<typename T = void> class VgjsCoroReturn;
    template<typename T = void> using VgjsCoroReturnPointer = VgjsCoroReturn<T>*;
    
    template<typename T, bool SYNC = true, uint64_t LIMIT = std::numeric_limits<uint64_t>::max()> class VgjsQueue;

    class VgjsJobSystem; 
    using VgjsJobSystemPointer = VgjsJobSystem*;

    //---------------------------------------------------------------------------------------------
    //Concepts

    template<typename>
    struct is_vector : std::false_type {};    //test whether a template parameter T is a std::pmr::vector

    template<typename T>
    struct is_vector<std::vector<T>> : std::true_type {};

    template<typename T>    //Concept of things that can get scheduled
    concept is_function = std::is_convertible_v< std::decay_t<T>, std::function<void(void)> > && (!is_coro_return<std::decay_t<T>>::value);

    template<typename T>
    concept is_parent = std::is_same_v< std::decay_t<T>, VgjsJobParent >; //Is derived from job parent class

    template<typename T>
    concept is_job = std::is_same_v< std::decay_t<T>, VgjsJob >; //Is a job.

    template<typename T>
    concept is_coro_promise = std::is_base_of_v<VgjsJobParent, std::decay_t<T>> && !is_job<T>; //Is a coro promise

    template<typename >
    struct is_coro_return : std::false_type {}; //Is NOT a coro return object class.

    template<typename T>
    struct is_coro_return<VgjsCoroReturn<T>> : std::true_type {}; //Is a coro return object class.

    template<typename T>
    concept is_tag = std::is_same_v< std::decay_t<T>, tag_t >; //Is a tag that can be scheduled.

    /// <summary>
    /// A wrapper for scheduling tuples of functions and coros.
    /// </summary>
    /// <typeparam name="...Ts">The types to be scheduled</typeparam>
    /// <param name="...args">The actual functions and coros to be scheduled.</param>
    /// <returns>Returns a tuple that can be scheduled.</returns>
    template<typename... Ts>
    inline decltype(auto) parallel(Ts&&... args) {
        return std::tuple<Ts&&...>(std::forward<Ts>(args)...);
    }

    //---------------------------------------------------------------------------------------------
    //Function objects

    /// <summary>
    /// This is the parent class of all jobs.
    /// </summary>
    struct VgjsJobParent {
        VgjsJobParentPointer   m_next{0};            //For storing in a queue.
        thread_index_t         m_index{};            //thread that the f should run on
        thread_type_t          m_type{};             //type of the call
        thread_id_t            m_id{};               //unique identifier of the call
        VgjsJobParentPointer   m_parent{};           //parent job that created this job
        bool                   m_is_function{ true };//If true then this is a function, if false then a coroutine.
        std::atomic<uint32_t>  m_children{};         //number of children this job is waiting for

        VgjsJobParent() = default;
        
        VgjsJobParent(thread_index_t index, thread_type_t type, thread_id_t id, VgjsJobParentPointer parent) noexcept
            : m_index{ index }, m_type{ type }, m_id{ id }, m_parent{ parent } {};

        VgjsJobParent(const VgjsJobParent&& j) noexcept {
            m_index = j.m_index;
            m_type = j.m_type;
            m_id = j.m_id;
            m_parent = j.m_parent;
            m_is_function = j.m_is_function;
        }
        VgjsJobParent& operator= (const VgjsJobParent& j) noexcept = default;
        VgjsJobParent& operator= (VgjsJobParent&& j) noexcept = default;
        
        virtual void resume() = 0;
        virtual bool destroy() = 0; //Called when the object should be destroyed.
    };

    /// <summary>
    /// A job is a function object that can be scheduled.
    /// </summary>
    struct VgjsJob : public VgjsJobParent {
        std::function<void(void)> m_function = []() {};  //The function that should be executed

        VgjsJob() noexcept : VgjsJobParent() {};

        template<typename F>
        VgjsJob(F&& f = []() {}
            , thread_index_t index = thread_index_t{ -1 }
            , thread_type_t type = thread_type_t{}
            , thread_id_t id = thread_id_t{}
            , VgjsJobParentPointer parent = nullptr) noexcept : m_function{ f }, VgjsJobParent(index, type, id, parent) {};

        VgjsJob(const VgjsJob& j) noexcept = default;
        VgjsJob(VgjsJob&& j) noexcept { m_function = std::move(j.m_function); };
        VgjsJob& operator= (const VgjsJob& j) noexcept { m_function = j.m_function; return *this; };
        VgjsJob& operator= (VgjsJob&& j) noexcept = default;
        void resume() noexcept { m_function(); }
        bool destroy() noexcept { return true; }
    };

    //---------------------------------------------------------------------------------------------
    //Coroutines

    /// <summary>
    /// This is the base class of all coroutine promises. We need it to distinguish between return type
    /// of void or any value. Coroutines must have different interfaces for these two cases, but we
    /// need a unified interfaces to handle all cases.
    /// </summary>
    /// <typeparam name="T">The return type of the coroutine.</typeparam>
    template<typename T>
    class VgjsCoroPromiseBase : public VgjsJobParent {
    protected:
        coroutine_handle<> m_handle{};    //Handle of the coroutine

    public:
        VgjsCoroPromiseBase(coroutine_handle<> handle) noexcept : m_handle{ handle } { m_is_function = false; };

        auto unhandled_exception() noexcept -> void { std::terminate(); };
        auto initial_suspend() noexcept -> suspend_always { return {}; };
        auto final_suspend() noexcept -> final_awaiter<T> { return {}; };
        auto resume() noexcept -> void {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();       //Coro could destroy itself here!!
            }
        }

        bool destroy() noexcept {
            m_handle.destroy();
            return false;
        }

        template<typename U>
        auto await_transform(U&& func) noexcept -> awaitable_tuple<T, U> { return awaitable_tuple<T, U>{ std::tuple<U&&>(std::forward<U>(func)) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>&& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { tuple }; };

        auto await_transform(thread_index_t index) noexcept -> awaitable_resume_on<T> { return { index }; };
        auto await_transform(tag_t tg) noexcept -> awaitable_tag<T> { return { tg }; };
    };

    /// <summary>
    /// The coroutine promise class for returning a specific value type.
    /// </summary>
    /// <typeparam name="T">Return type of the coroutine.</typeparam>
    template<typename T>
    class VgjsCoroPromise : public VgjsCoroPromiseBase<T> {
    private:
        T m_value{};   //<a local storage of the value, use if parent is a coroutine
    public:
       VgjsCoroPromise() noexcept : VgjsCoroPromiseBase<T>(coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this)) {}
       void return_value(T t) { this->m_value = t; }
       auto get_return_object() noexcept -> VgjsCoroReturn<T>;
       auto get() { return m_value; };
    };

    /// <summary>
    /// The coroutine promise class for returning void.
    /// </summary>
    template<>
    class VgjsCoroPromise<void> : public VgjsCoroPromiseBase<void> {
    public:
        VgjsCoroPromise() noexcept : VgjsCoroPromiseBase<void>(coroutine_handle<VgjsCoroPromise<void>>::from_promise(*this)) {} 
        void return_void() noexcept {}
        auto get_return_object() noexcept -> VgjsCoroReturn<void>;
    };

    //---------------------------------------------------------------------------------------------
    //Coroutine return object

    /// <summary>
    /// The coroutine return object.
    /// </summary>
    /// <typeparam name="T">Return value type.</typeparam>
    template<typename T>
    class VgjsCoroReturn {
    public:
        using promise_type = VgjsCoroPromise<T>;

    private:
        coroutine_handle<VgjsCoroPromise<T>> m_handle{};       //handle to Coro promise

    public:
        VgjsCoroReturn() noexcept {};
        VgjsCoroReturn(coroutine_handle<promise_type> h) noexcept : m_handle { h } {};
        VgjsCoroReturn(const VgjsCoroReturn& rhs) noexcept = delete;
        VgjsCoroReturn(VgjsCoroReturn&& rhs) noexcept { m_handle = std::exchange(rhs.m_handle, {}); };

        ~VgjsCoroReturn() noexcept {
            if (!m_handle || m_handle.done() || !m_handle.promise().m_parent) return;
            m_handle.destroy(); 
        }

        void operator= (VgjsCoroReturn<T>&& rhs) noexcept { m_handle = std::exchange(rhs.m_handle, {}); }

        T get() noexcept { 
            if constexpr (!std::is_void_v<std::decay_t<T>>) {
                if (m_handle) return m_handle.promise().get();
            }
            return {};
        }
        VgjsCoroPromise<T>& promise() { return m_handle.promise(); }
        void resume() { 
            if(m_handle && !m_handle.done()) 
                m_handle.promise().resume(); 
        }
        auto handle() { return m_handle; };

        decltype(auto) operator() (thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            promise().m_index = index;
            promise().m_type = type;
            promise().m_id = id;
            return *this;
        }
    };

    template<typename T>
    auto VgjsCoroPromise<T>::get_return_object() noexcept -> VgjsCoroReturn<T> { return { coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this) }; };

    auto VgjsCoroPromise<void>::get_return_object() noexcept -> VgjsCoroReturn<void> { return { coroutine_handle<VgjsCoroPromise<void>>::from_promise(*this) }; };

    //---------------------------------------------------------------------------------------------
    //A general internally synchronized FIFO queue class

    /// <summary>
    /// A general FIFO queue class. The class is thread safe and internally synchronized.
    /// </summary>
    /// <typeparam name="T">Type of items to be stored in the queue.</typeparam>
    /// <typeparam name="SYNC">If true then the queue is thread safe.</typeparam>
    /// <typeparam name="LIMIT">Max number of items that the queue can store.</typeparam>
    template<typename T, bool SYNC, uint64_t LIMIT>
    class VgjsQueue {
    private:
        std::mutex  m_mutex{};          //Mutex for synchronization.
        T*          m_first{ nullptr }; //First item in the queue.
        T*          m_last{ nullptr };  //Last item in the queue.
        size_t      m_size{ 0 };        //Number of items currently in the queue.

    public:
        VgjsQueue() noexcept {};
        VgjsQueue(VgjsQueue&& rhs) noexcept {};
        ~VgjsQueue() noexcept {                 
            auto* p = m_first;                  
            while (p) {
                auto* q = p;
                p = (T*)p->m_next;
                if( q->destroy() ) delete q; //Destroy returns true if you should call delete
            }
        }

        /// <summary>
        /// Return the number of items currently in the queue.
        /// </summary>
        /// <returns>Number of items currently in the queue.</returns>
        auto size() noexcept { 
            if constexpr (SYNC) m_mutex.lock();
            auto size = m_size;
            if constexpr (SYNC) m_mutex.unlock();
            return size;
        }

        /// <summary>
        /// Push a new item into the back of the queue.
        /// </summary>
        /// <param name="job">The new item.</param>
        /// <returns>If true then the item was stored.</returns>
        bool push(T* job) noexcept {
            if constexpr (SYNC) m_mutex.lock();
            if (m_size > LIMIT) {   //is queue full -> do not accept the new entry
                if constexpr (SYNC) m_mutex.unlock();
                return false;
            }
            job->m_next = nullptr;                  //No successor
            if (m_last) m_last->m_next = job;       //Is there a predecessor -> link
            else m_first = job;                     //No -> its the first element
            m_last = job;                           //Its always the new last element in the queue
            m_size++;                               //increase size
            if constexpr (SYNC) m_mutex.unlock();
            return true;
        }

        /// <summary>
        /// Pop an item from the queue.
        /// </summary>
        T* pop() noexcept {
            if constexpr (SYNC) m_mutex.lock();
            if (!m_first) {
                if constexpr (SYNC) m_mutex.unlock();
                return nullptr; //queue is empty -> return nullptr
            }
            T* res = m_first;               //there is a first entry
            m_first = (T*)m_first->m_next;  //point to next in queue
            if (!m_first) m_last = nullptr; //if there is not next -> queue is empty
            m_size--;                       //reduce size
            if constexpr (SYNC) m_mutex.unlock();
            return res;
        }
    };


    //---------------------------------------------------------------------------------------------
    //The main job system class 

    /// <summary>
    /// The main job system class. The class uses the mono state pattern, i.e., all state variables are static.
    /// </summary>
    class VgjsJobSystem {
        template<typename T> friend struct awaitable_resume_on;
        template<typename PT, typename... Ts> friend struct awaitable_tuple;
        template<typename PT> friend struct awaitable_tag;
        template<typename U> friend struct final_awaiter;

    private:
        static inline std::atomic<bool>               m_terminate{ false };     //If true then terminate the job system
        static inline std::atomic<uint32_t>           m_init_counter = 0;       //Counter used when starting the system
        static inline std::atomic<uint32_t>           m_thread_count{ 0 };      //<number of threads in the pool
        static inline std::vector<std::thread>        m_threads;	            //<array of thread structures

        static inline std::vector<VgjsQueue<VgjsJob>> m_global_job_queues;	    //<each thread has its shared Job queue, multiple produce, multiple consume
        static inline std::vector<VgjsQueue<VgjsJob>> m_local_job_queues;	    //<each thread has its own Job queue, multiple produce, single consume

        static inline std::vector<VgjsQueue<VgjsJobParent>> m_global_coro_queues;	//<each thread has its shared Coro queue, multiple produce, multiple consume
        static inline std::vector<VgjsQueue<VgjsJobParent>> m_local_coro_queues;	//<each thread has its own Coro queue, multiple produce, single consume

        static inline std::unordered_map<tag_t, std::unique_ptr<VgjsQueue<VgjsJobParent>>, tag_t::hash> m_tag_queues;

        static inline thread_local VgjsJobParentPointer m_current_job{};    //A pointer to the current job of this thread.
        VgjsQueue<VgjsJob, false, 1 << 12>              m_recycle_jobs;     //A queue to recycle old jobs.
        thread_local static inline thread_index_t       m_next_thread{ 0 }; //The index of the next thread to schedule to
        static inline std::unique_ptr<std::condition_variable> m_cv;       //Condition variable to wake up threads
        static inline std::vector<std::unique_ptr<std::mutex>> m_mutex{};  //For sleeping and waking up again

        /// <summary>
        /// Determine the thread that should receive a new job.
        /// </summary>
        /// <returns>Thread index to schedule to.</returns>
        thread_index_t next_thread_index() {
            m_next_thread = thread_index_t{ m_next_thread + 1 };
            m_next_thread = (m_next_thread >= m_thread_count ? thread_index_t{ 0 } : m_next_thread);
            return m_next_thread;
        }

    public:

        /// <summary>
        /// Constructor of the job system class. Can be called any number of times, but it will
        /// only do real work at the first time.
        /// </summary>
        /// <param name="count">Number of threads to be in the job system.</param>
        /// <param name="start">Start index, can be 0 or 1.</param>
        VgjsJobSystem(thread_count_t count = thread_count_t(0), thread_index_t start = thread_index_t(0)) {
            if (m_init_counter > 0) [[likely]] return;
            auto cnt = m_init_counter.fetch_add(1);
            if (cnt > 0) return;

            count = ( count <= 0 ? (int64_t)std::thread::hardware_concurrency() : count );
            for (auto i = start; i < count; ++i) {
                m_global_job_queues.emplace_back();    //global job queue
                m_local_job_queues.emplace_back();     //local job queue
                m_global_coro_queues.emplace_back();   //global coro queue
                m_local_coro_queues.emplace_back();    //local coro queue
                m_mutex.emplace_back(std::make_unique<std::mutex>());
            }
            m_cv = std::make_unique<std::condition_variable>();

            for (auto i = start; i < count; ++i) {
                m_threads.push_back(std::thread(&VgjsJobSystem::task, this, thread_index_t(i), count));	//spawn the pool threads
                m_threads[i].detach();
            }
            wait(count);
        };

        ~VgjsJobSystem() {} //Keep empty!! Otherwise there will be death each time the destructor is called. 

        /// <summary>
        /// Return the number of threads in the system.
        /// </summary>
        /// <returns>Number of threads in the system.</returns>
        int64_t thread_count() { return m_thread_count.load(); };

        /// <summary>
        /// Test whether there is a function job in a queue. If found, pop it and run it.
        /// </summary>
        /// <param name="queue">The queue to test.</param>
        /// <returns>True, if a job was found and executed.</returns>
        inline bool test_job(auto& queue) {
            m_current_job = (VgjsJobParentPointer)queue.pop();      //Pop a job.
            if (m_current_job) {
                ((VgjsJobPointer)m_current_job)->m_function();      //Run it, but avoid virtual call.
                auto save = m_current_job;              //Safe the job for later recycling.
                child_finished(m_current_job);          //Test whether parent should be notified
                if (!m_recycle_jobs.push((VgjsJobPointer)save)) delete save;  //recycle job if possible.
                return true;
            }
            return false;
        }

        /// <summary>
        /// Test whether there is a coroutine job in a queue. If found, pop it and run it.
        /// </summary>
        /// <param name="queue">The queue to test.</param>
        /// <returns>True, if a job was found and executed.</returns>
        inline bool test_coro(auto& queue) {
            m_current_job = queue.pop();    //Pop a job
            if (m_current_job) {
                m_current_job->resume();    //Resume the coroutine (virtual call)
                return true;
            }
            return false;
        }

        /// <summary>
        /// The main function that all threads must enter.
        /// </summary>
        /// <param name="index">The index of this thread.</param>
        /// <param name="count">The total number of threads in the system.</param>
        void task(thread_index_t index, thread_count_t count) noexcept {
            m_thread_count++;
            m_thread_count.notify_all();
            m_next_thread = index;
            wait(count);

            thread_index_t my_index{ index };                       //<each thread has its own number
            thread_index_t other_index{ my_index };
            std::unique_lock<std::mutex> lk(*m_mutex[my_index]);

            while (!m_terminate) {  //Run until the job system is terminated
                bool found1 = test_job(m_local_job_queues[my_index]);
                bool found2 = test_coro(m_local_coro_queues[my_index]);
                bool found3 = test_job(m_global_job_queues[my_index]);
                bool found4 = test_coro(m_global_coro_queues[my_index]);
                bool found = found1 || found2 || found3 || found4;
                int64_t loops = count;
                while (!found && --loops > 0) {
                    other_index = (other_index + 1 == count ? 0 : other_index + 1);
                    if (my_index != other_index) {
                        found = ( test_job(m_global_job_queues[other_index]) || test_coro(m_global_coro_queues[other_index]) );
                    }
                }

                if (!found) m_cv->wait_for(lk, std::chrono::microseconds(100));
            }
            m_current_job = nullptr;
            m_thread_count--;
            m_thread_count.notify_all();
        }

        /// <summary>
        /// If a function job finishes, it calls this function. A function also counts itself as a child.
        /// </summary>
        /// <param name="job"></param>
        void child_finished(VgjsJobParentPointer job) noexcept {
            uint32_t num = job->m_children.fetch_sub(1);        //one less child
            if (num == 1) {                                     //was it the last child?
                if (job->m_is_function) {            //Jobs call always on_finished()
                    if (job->m_parent) {
                        child_finished(job->m_parent);	//if this is the last child job then the parent will also finish
                    }
                }
                else {
                    m_current_job = nullptr;
                    schedule_job(job, tag_t{}, job->m_parent, 0);   //a coro just gets scheduled again so it can go on
                }
            }
        }

        void terminate() {
            m_terminate = true;
            m_cv->notify_all();
            if (m_current_job) {                //if called from a job
                m_thread_count--;               //Remove this job, because it is blocking
                m_thread_count.notify_all();    //notify the others waiting
            }
            wait(); //wait for the threads to return
        }

        void wait(int64_t desired = 0) {
            do {
                auto num = m_thread_count.load();           //current number of threads
                if(num!= desired) m_thread_count.wait(num); //if not yet there -> blocking wait
            } while (m_thread_count.load() != desired);     //until we are there
        }

        //--------------------------------------------------------------------------------------------
        //schedule

        private:

        template<typename R>
            requires is_coro_return<std::decay_t<R>>::value
        uint32_t schedule_job(R&& job, tag_t tag, VgjsJobParentPointer parent, int32_t children) noexcept {
            if (!job.handle() || job.handle().done()) return 0;
            return schedule_job(&job.promise(), tag, parent, children);
        }

        template<typename T>
            requires is_parent<std::decay_t<T>> || is_job< std::decay_t<T>> || is_coro_promise< std::decay_t<T>>
        uint32_t schedule_job(T* job, tag_t tag, VgjsJobParentPointer parent, int32_t children) noexcept {
            if constexpr (is_coro_promise<T>) {
                if (m_current_job && m_current_job->m_is_function) { //function is not allowed to schedule a coro
                    assert(false && "Error: only coros allowed to schedule a coro!");
                    std::exit(1);
                }
            }

            if (tag >= 0) {
                if (!m_tag_queues.contains(tag)) {
                    m_tag_queues[tag] = std::make_unique<VgjsQueue<VgjsJobParent>>();
                }
                m_tag_queues.at(tag)->push(job); //save for later
                return 0;
            }

            job->m_parent = parent;
            if (parent != nullptr && children != 0) {
                if (children < 0) children = 1;
                parent->m_children.fetch_add((int)children);
            }

            auto next_thread = job->m_index < 0 ? next_thread_index() : job->m_index;
            if constexpr (is_job<T>) {
                job->m_children = 1;
                job->m_index < 0 ? m_global_job_queues[next_thread].push(job) : m_local_job_queues[next_thread].push(job);
            }
            if constexpr (is_coro_promise<T>) {  
                job->m_children = 0;
                job->m_index < 0 ? m_global_coro_queues[next_thread].push(job) : m_local_coro_queues[next_thread].push(job);
            }

            m_cv->notify_all();
            return 1l;
        }

        template<typename F>
            requires is_function<std::decay_t<F>>
        uint32_t schedule_job(F&& f, tag_t tag, VgjsJobParentPointer parent, int32_t children) noexcept {
            return schedule(std::forward<F>(f), thread_index_t{}, thread_type_t{}, thread_id_t{}, tag, parent, children);
        }

        template<typename V>
            requires is_vector<std::decay_t<V>>::value
        uint32_t schedule_job(V&& vector, tag_t tag, VgjsJobParentPointer parent, int32_t children) noexcept {
            uint32_t sum = 0;
            std::ranges::for_each(vector, [&](auto&& v) { sum += schedule_job(std::forward<decltype(v)>(v), tag, parent, children); children = 0;  });
            return sum;
        }

        //---------------------------------------------------------------------------------------

        public:

        template<typename R>
            requires is_coro_return<std::decay_t<R>>::value
        uint32_t schedule(R&& job, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            if (!job.handle() || job.handle().done()) return 0;
            job.promise().m_index = index;
            job.promise().m_type = type;
            job.promise().m_id = id;
            return schedule_job(&job.promise(), tag, parent, children);
        }

        template<typename T>
            requires is_tag<std::decay_t<T>>
        uint32_t schedule(T&& tg, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            if (!m_tag_queues.contains(tg)) return 0;

            auto queue = m_tag_queues[tg].get();   //get the queue for this tag
            uint32_t num_jobs = queue->size();

            if (parent != nullptr) {
                if (children < 0) children = num_jobs;     //if the number of children is not given, then use queue size
                parent->m_children.fetch_add((int)children);    //add this number to the number of children of parent
            }

            uint32_t num = num_jobs;        //schedule at most num_jobs, since someone could add more jobs now
            int i = 0;
            while (num > 0) {     //schedule all jobs from the tag queue
                VgjsJobParent* job = queue->pop();
                if (!job) return i;
                job->m_parent = parent;
                schedule_job(job);
                --num;
                ++i;
            }
            return i;
        };

        template<typename F>
            requires is_function<std::decay_t<F>> 
        uint32_t schedule(F&& f, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            VgjsJob* job = m_recycle_jobs.pop();
            if (job) {
                *job = std::move(VgjsJob{ std::forward<decltype(f)>(f), index, type, id });
            }
            else {
                if (!job) job = new VgjsJob{ std::forward<decltype(f)>(f), index, type, id };
            }
            return schedule_job(job, tag, parent, children );
        }

        template<typename V>
            requires is_vector<std::decay_t<V>>::value
        uint32_t schedule(V&& vector, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            uint32_t sum = 0;
            std::ranges::for_each(vector, [&](auto&& v) { sum += schedule_job(std::forward<decltype(v)>(v), index, type, id, tag, parent, children); children = 0; });
            return sum;
        }

    };


    //---------------------------------------------------------------------------------------------


    template<typename T>
    struct awaitable_resume_on : suspend_always {
        thread_index_t m_index; //the thread index to use

        bool await_ready() noexcept {   //do not go on with suspension if the job is already on the right thread
            return (m_index == system->m_index);
        }

        void await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            h.promise().m_index = m_index;
            VgjsJobSystem()->schedule(&h.promise(), tag_t{}, (VgjsJobParentPointer)&h.promise().m_parent, -1);
        }

        awaitable_resume_on(thread_index_t index) noexcept : m_index(index) {};
    };


    template<typename T>
    struct awaitable_tag : suspend_always {
        tag_t    m_tag;            //the tag to schedule
        int32_t  m_number = 0;     //Number of scheduled jobs

        bool await_ready() noexcept {  //do nothing if the given tag is invalid
            return m_tag < 0;
        }

        bool await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            m_number = VgjsJobSystem().schedule_job(m_tag);
            return m_number > 0;     //if jobs were scheduled - await them
        }

        int32_t await_resume() {
            return m_number;
        }

        awaitable_tag(tag_t tg) noexcept : m_tag{ tg } {};
    };


    template<typename PT, typename... Ts>
    struct awaitable_tuple : suspend_always {
        tag_t                    m_tag;          //The tag to schedule to
        std::tuple<Ts&&...>      m_tuple;        //tuple with all children to start
        std::size_t              m_number;       //total number of all new children to schedule

        template<typename U>
        size_t size(U& children) {
            if constexpr (is_vector< std::decay_t<U> >::value) { //if this is a vector
                return children.size();
            }
            if constexpr (std::is_same_v<std::decay_t<U>, tag_t>) { //if this is a tag
                m_tag = children;
                return 0;
            }
            return 1;   //if this is a std::function, Function, or Coro
        };

        bool await_ready() noexcept {               //suspend only if there is something to do
            auto f = [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                m_number = (size(std::get<Idx>(m_tuple)) + ... + 0); //called for every tuple element
            };
            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_number == 0;   //nothing to be done -> do not suspend
        }

        bool await_suspend(coroutine_handle<VgjsCoroPromise<PT>> h) noexcept {
            auto g = [&]<std::size_t Idx>() {

                using tt = decltype(m_tuple);
                using T = decltype(std::get<Idx>(std::forward<tt>(m_tuple)));
                decltype(auto) children = std::forward<T>(std::get<Idx>(std::forward<tt>(m_tuple)));

                if constexpr (std::is_same_v<std::decay_t<T>, tag_t>) { //never schedule tags here
                    return;
                }
                else {
                    VgjsJobSystem().schedule_job(std::forward<T>(children), m_tag, (VgjsJobParentPointer)&h.promise(), (int32_t)m_number);   //in first call the number of children is the total number of all jobs
                    m_number = 0;                                               //after this always 0
                }
            };

            auto f = [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                (g.template operator() < Idx > (), ...); //called for every tuple element
            };

            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_tag.value < 0; //if tag value < 0 then schedule now, so return true to suspend
        }

        template<typename T>
        decltype(auto) get_val(T& t) {
            return std::make_tuple(); //ignored by std::tuple_cat
        }

        template<typename T>
            requires (!std::is_void_v<T>)
        decltype(auto) get_val(VgjsCoroReturn<T>& t) {
            return std::make_tuple(t.get());
        }

        template<typename T>
            requires (!std::is_void_v<T>)
        decltype(auto) get_val(std::vector<VgjsCoroReturn<T>>& vec) {
            std::vector<T> ret;
            ret.reserve(vec.size());
            for (auto& coro : vec) { ret.push_back(coro.get()); }
            return std::make_tuple(std::move(ret));
        }

        decltype(auto) await_resume() {
            auto f = [&, this]<typename... Us>(Us&... args) {
                return std::tuple_cat(get_val(args)...);
            };
            using rtype = decltype(std::apply(f, m_tuple));

            if constexpr (std::tuple_size_v < rtype > == 0) {
                return;
            }
            else if constexpr (std::tuple_size_v < rtype > == 1) {
                auto ret = std::get<0>(std::apply(f, m_tuple));
                return ret;
            }
            else {
                return std::apply(f, m_tuple);
            }
        }

        awaitable_tuple( std::tuple<Ts&&...> tuple ) noexcept : m_tag{}, m_number{ 0 }, m_tuple(std::forward<std::tuple<Ts&&...>>(tuple)) {};
    };


    template<typename U>
    struct final_awaiter : public suspend_always {

        bool await_suspend(coroutine_handle<VgjsCoroPromise<U>> h) noexcept { //called after suspending
            auto parent = h.promise().m_parent;

            if (parent != nullptr) {          //if there is a parent
                uint32_t num = parent->m_children.fetch_sub(1);        //one less child
                if (num == 1) {                                             //was it the last child?
                    VgjsJobSystem().schedule_job(parent, tag_t{}, parent->m_parent, 0);      //if last reschedule the parent coro
                }
                return true;        //leave destruction to parent coro
            }
            return false;  //no parent -> immediately destroy
        }
    };

}


