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

namespace simple_vgjs {

    using namespace std::experimental;

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
    using parent_t          = strong_type_t<int64_t, -1, 5>;

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

    //test whether a template parameter T is a std::pmr::vector
    template<typename>
    struct is_vector : std::false_type {};

    template<typename T>
    struct is_vector<std::vector<T>> : std::true_type {};

    //Concept of things that can get scheduled
    template<typename T>
    concept is_function = std::is_convertible_v< std::decay_t<T>, std::function<void(void)> > && (!is_coro_return<T>::value);

    template<typename T>
    concept is_parent = std::is_same_v< std::decay_t<T>, VgjsJobParent >;

    template<typename T>
    concept is_job = std::is_same_v< std::decay_t<T>, VgjsJob >;

    template<typename T>
    concept is_coro_promise = std::is_base_of_v<VgjsJobParent, std::decay_t<T>> && !is_job<T>;

    template<typename >
    struct is_coro_return : std::false_type {};

    template<typename T>
    struct is_coro_return<VgjsCoroReturn<T>> : std::true_type {};

    template<typename T>
    concept is_tag = std::is_same_v< std::decay_t<T>, tag_t >;


    //---------------------------------------------------------------------------------------------

    struct VgjsJobParent {
        VgjsJobParentPointer   m_next{0};

        thread_index_t         m_thread_index{};     //thread that the f should run on
        thread_type_t          m_type{};             //type of the call
        thread_id_t            m_id{};               //unique identifier of the call
        VgjsJobParentPointer   m_parent{};           //parent job that created this job
        bool                   m_is_function{ true };
        bool                   m_self_destruct{ false };
        std::atomic<uint32_t>  m_children{};         //number of children this job is waiting for

        VgjsJobParent() = default;
        
        VgjsJobParent(thread_index_t index, thread_type_t type, thread_id_t id, VgjsJobParentPointer parent)
            : m_thread_index{ index }, m_type{ type }, m_id{ id }, m_parent{ parent } {};

        VgjsJobParent(const VgjsJobParent&& j) noexcept {
            m_thread_index = j.m_thread_index;
            m_type = j.m_type;
            m_id = j.m_id;
            m_parent = j.m_parent;
            m_is_function = j.m_is_function;
        }
        VgjsJobParent& operator= (const VgjsJobParent& j) noexcept = default;
        VgjsJobParent& operator= (VgjsJobParent&& j) noexcept = default;
        
        virtual void resume() = 0;
        virtual bool destroy() = 0;
    };

    struct VgjsJob : public VgjsJobParent {
        std::function<void(void)> m_function = []() {};  //empty function

        VgjsJob() : VgjsJobParent() {};

        template<typename F>
        VgjsJob(F&& f = []() {}
            , thread_index_t index = thread_index_t{ -1 }
            , thread_type_t type = thread_type_t{}
            , thread_id_t id = thread_id_t{}
            , VgjsJobParentPointer parent = nullptr) : m_function{ f }, VgjsJobParent(index, type, id, parent) {};

        VgjsJob(const VgjsJob& j) noexcept = default;
        VgjsJob(VgjsJob&& j) noexcept { m_function = std::move(j.m_function); };
        VgjsJob& operator= (const VgjsJob& j) noexcept { m_function = j.m_function; return *this; };
        VgjsJob& operator= (VgjsJob&& j) noexcept = default;
        void resume() noexcept { m_function(); }
        bool destroy() noexcept { return true; }
    };


    //---------------------------------------------------------------------------------------------


    template<typename T>
    class VgjsCoroPromiseBase : public VgjsJobParent {
    protected:
        coroutine_handle<> m_handle{};    //<handle of the coroutine

    public:
        VgjsCoroPromiseBase(coroutine_handle<> handle) noexcept : m_handle{ handle } { m_is_function = false; };

        auto unhandled_exception() noexcept -> void { std::terminate(); };
        auto initial_suspend() noexcept -> suspend_always { return {}; };
        auto final_suspend() noexcept -> final_awaiter<T> { return {}; };

        auto resume() noexcept -> void {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();       //coro could destroy itself here!!
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


    template<typename T>
    class VgjsCoroPromise : public VgjsCoroPromiseBase<T> {
    private:
        T m_value{};   //<a local storage of the value, use if parent is a coroutine
    public:
       VgjsCoroPromise() noexcept : VgjsCoroPromiseBase<T>(coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this)) {}
       void return_value(T t) { this->m_value = t; }
       auto get_return_object() noexcept -> VgjsCoroReturn<T>;
    };

    template<>
    class VgjsCoroPromise<void> : public VgjsCoroPromiseBase<void> {
    public:
        VgjsCoroPromise() noexcept : VgjsCoroPromiseBase<void>(coroutine_handle<VgjsCoroPromise<void>>::from_promise(*this)) {} 
        void return_void() noexcept {}
        auto get_return_object() noexcept -> VgjsCoroReturn<void>;
    };

    //--------------------------------------------------------

    template<typename T>
    class VgjsCoroReturn {
    public:
        using promise_type = VgjsCoroPromise<T>;

    private:
        coroutine_handle<VgjsCoroPromise<T>> m_handle{};       //handle to Coro promise

    public:
        VgjsCoroReturn(coroutine_handle<promise_type> h) noexcept : m_handle{ h } {};
        VgjsCoroReturn(VgjsCoroReturn<T>&& t)  noexcept : m_handle{ t.m_handle } {}
        ~VgjsCoroReturn() noexcept {
            if (m_handle.promise().m_self_destruct) return;
            if(m_handle) m_handle.destroy(); 
        }

        T get() noexcept { return {}; }
        VgjsCoroPromise<T>& promise() { return m_handle.promise(); }
        void resume() { m_handle.promise().resume(); }

        decltype(auto) operator() (thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            promise().m_thread_index = index;
            promise().m_type = type;
            promise().m_id = id;
            return *this;
        }
    };

    template<typename T>
    auto VgjsCoroPromise<T>::get_return_object() noexcept -> VgjsCoroReturn<T> { return { coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this) }; };

    auto VgjsCoroPromise<void>::get_return_object() noexcept -> VgjsCoroReturn<void> { return { coroutine_handle<VgjsCoroPromise<void>>::from_promise(*this) }; };


    //---------------------------------------------------------------------------------------------

    template<typename T, bool SYNC, uint64_t LIMIT>
    class VgjsQueue {
    private:
        std::mutex  m_mutex{};
        T*          m_first{ nullptr };
        T*          m_last{ nullptr };
        size_t      m_size{ 0 };

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

        auto size() noexcept { 
            if constexpr (SYNC) m_mutex.lock();
            auto size = m_size;
            if constexpr (SYNC) m_mutex.unlock();
            return size;
        }

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


    class VgjsJobSystem {
    private:
        static inline std::atomic<bool>               m_terminate{ false };
        static inline std::atomic<uint32_t>           m_init_counter = 0;
        static inline std::atomic<uint32_t>           m_thread_count{ 0 };      //<number of threads in the pool
        static inline std::vector<std::thread>        m_threads;	            //<array of thread structures

        static inline std::vector<VgjsQueue<VgjsJob>> m_global_job_queues;	    //<each thread has its shared Job queue, multiple produce, multiple consume
        static inline std::vector<VgjsQueue<VgjsJob>> m_local_job_queues;	    //<each thread has its own Job queue, multiple produce, single consume

        static inline std::vector<VgjsQueue<VgjsJobParent>> m_global_coro_queues;	//<each thread has its shared Coro queue, multiple produce, multiple consume
        static inline std::vector<VgjsQueue<VgjsJobParent>> m_local_coro_queues;	//<each thread has its own Coro queue, multiple produce, single consume

        static inline std::unordered_map<tag_t, std::unique_ptr<VgjsQueue<VgjsJobParent>>, tag_t::hash> m_tag_queues;

        static inline thread_local VgjsJobParentPointer m_current_job{};
        VgjsQueue<VgjsJob, false, 1 << 12>              m_recycle_jobs;
        thread_local static inline thread_index_t       m_next_thread{ 0 };
        static inline std::vector<std::unique_ptr<std::condition_variable>> m_cv;
        static inline std::vector<std::unique_ptr<std::mutex>>              m_mutex{};

        thread_index_t next_thread_index() {
            m_next_thread = thread_index_t{ m_next_thread + 1 };
            m_next_thread = (m_next_thread >= m_thread_count ? thread_index_t{ 0 } : m_next_thread);
            return m_next_thread;
        }

    public:

        VgjsJobSystem(thread_count_t count = thread_count_t(0), thread_index_t start = thread_index_t(0)) {
            if (m_init_counter > 0) [[likely]] return;
            auto cnt = m_init_counter.fetch_add(1);
            if (cnt > 0) return;

            count = ( count <= 0 ? (int64_t)std::thread::hardware_concurrency() : count );
            for (auto i = start; i < count; ++i) {
                m_global_job_queues.emplace_back();     //global job queue
                m_local_job_queues.emplace_back();     //local job queue
                m_global_coro_queues.emplace_back();     //global coro queue
                m_local_coro_queues.emplace_back();     //local coro queue
                m_cv.emplace_back(std::make_unique<std::condition_variable>());
                m_mutex.emplace_back(std::make_unique<std::mutex>());
            }

            for (auto i = start; i < count; ++i) {
                m_threads.push_back(std::thread(&VgjsJobSystem::task, this, thread_index_t(i), count));	//spawn the pool threads
                m_threads[i].detach();
            }
            wait(count);
        };

        ~VgjsJobSystem() {} //keep empty!!

        int64_t thread_count() { 
            return m_thread_count.load(); 
        };

        inline bool test_job(auto& queue) {
            m_current_job = (VgjsJobParentPointer)queue.pop();
            if (m_current_job) {
                ((VgjsJobPointer)m_current_job)->m_function();          //avoid virtual call
                if (!m_recycle_jobs.push((VgjsJobPointer)m_current_job)) delete m_current_job;  //recycle job if possible
                return true;
            }
            return false;
        }

        inline bool test_coro(auto& queue) {
            m_current_job = queue.pop();
            if (m_current_job) {
                m_current_job->resume();
                return true;
            }
            return false;
        }

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
                bool found2 = test_job(m_global_job_queues[my_index]);
                bool found3 = test_coro(m_local_coro_queues[my_index]);
                bool found4 = test_coro(m_global_coro_queues[my_index]);
                bool found = found1 || found2 || found3 || found4;
                int64_t loops = count;
                while (!found && --loops > 0) {
                    other_index = (other_index + 1 == count ? 0 : other_index + 1);
                    if (my_index != other_index) {
                        found = ( test_job(m_global_job_queues[other_index]) || test_coro(m_global_coro_queues[other_index]) );
                    }
                }

                if (!found) m_cv[0]->wait_for(lk, std::chrono::microseconds(100));
            }
            m_current_job = nullptr;
            m_thread_count--;
            m_thread_count.notify_all();
        }

        void child_finished(VgjsJobParentPointer job) noexcept {
            uint32_t num = job->m_children.fetch_sub(1);        //one less child
            if (num == 1) {                                     //was it the last child?
                if (job->m_is_function) {            //Jobs call always on_finished()
                    if (job->m_parent) {
                        child_finished(job->m_parent);	//if this is the last child job then the parent will also finish
                    }
                }
                else {
                    schedule(job);   //a coro just gets scheduled again so it can go on
                }
            }
        }

        void terminate() {
            m_terminate = true;
            for (auto& cv : m_cv) cv->notify_all();
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

        template<typename R>
            requires is_coro_return<R>::value
        uint32_t schedule(R&& job, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            return schedule(&job.promise(), tag, parent, children);
        }

        template<typename T>
            requires is_parent<T> || is_job<T> || is_coro_promise<T>
        uint32_t schedule(T* job, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
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

            if (parent != nullptr) {
                if (children < 0) children = 1;
                parent->m_children.fetch_add((int)children);
            }

            auto next_thread = job->m_thread_index < 0 ? next_thread_index() : job->m_thread_index;
            if constexpr (is_job<T>) {
                job->m_thread_index < 0 ? m_global_job_queues[next_thread].push(job) : m_local_job_queues[next_thread].push(job);
            }
            if constexpr (is_coro_promise<T>) {
                job->m_thread_index < 0 ? m_global_coro_queues[next_thread].push(job) : m_local_coro_queues[next_thread].push(job);
            }

            m_cv[0]->notify_all();
            return 1l;
        }

        template<typename T>
            requires is_tag<T>
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
                schedule(job);
                --num;
                ++i;
            }
            return i;
        };

        template<typename F>
            requires is_function<F>
        uint32_t schedule(F&& f, tag_t tag, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            return schedule(std::forward<F>(f), thread_index_t{}, thread_type_t{}, thread_id_t{}, tag, parent, children);
        }

        template<typename F>
            requires is_function<F> 
        uint32_t schedule(F&& f, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            VgjsJob* job = m_recycle_jobs.pop();
            if (job) {
                *job = std::move(VgjsJob{ std::forward<decltype(f)>(f), index, type, id });
            }
            else {
                if (!job) job = new VgjsJob{ std::forward<decltype(f)>(f), index, type, id };
            }
            return schedule(job, tag, parent, children );
        }

        template<typename V>
            requires is_vector<V>::value
        uint32_t schedule(V&& vector, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            return std::accumulate(vector.begin, vector.end, 0, [](auto&& v) { return schedule(std::forward<decltype(v)>(v), index, type, id, tag, parent, children); });
        }
    };


    //---------------------------------------------------------------------------------------------


    template<typename T>
    struct awaitable_resume_on : suspend_always {
        thread_index_t m_thread_index; //the thread index to use

        bool await_ready() noexcept {   //do not go on with suspension if the job is already on the right thread
            return (m_thread_index == system->m_thread_index);
        }

        void await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            h.promise().m_thread_index = m_thread_index;
            VgjsJobSystem()->schedule(&h.promise());
        }

        awaitable_resume_on(thread_index_t index) noexcept : m_thread_index(index) {};
    };


    template<typename T>
    struct awaitable_tag : suspend_always {
        tag_t    m_tag;            //the tag to schedule
        int32_t  m_number = 0;     //Number of scheduled jobs

        bool await_ready() noexcept {  //do nothing if the given tag is invalid
            return m_tag < 0;
        }

        bool await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            m_number = VgjsJobSystem().schedule(m_tag);
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
                    VgjsJobSystem().schedule(std::forward<T>(children), m_tag, (VgjsJobParentPointer)&h.promise(), (int32_t)m_number);   //in first call the number of children is the total number of all jobs
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
            auto ret = std::apply(f, m_tuple);

            if constexpr (std::tuple_size_v < decltype(ret) > == 0) {
                return;
            }
            else if constexpr (std::tuple_size_v < decltype(ret) > == 1) {
                return std::get<0>(ret);
            }
            else {
                return ret;
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
                    VgjsJobSystem().schedule(parent);      //if last reschedule the parent coro
                }
            }
            else return false;  //no parent -> immediately destroy
            return true;        //leave destruction to parent coro
        }
    };

}


