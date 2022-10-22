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
    concept is_function = std::is_convertible_v< std::decay_t<T>, std::function<void(void)> >;

    template<typename >
    struct is_return_object : std::false_type {};

    template<typename T>
    struct is_return_object<VgjsCoroReturn<T>> : std::true_type {};

    template<typename T>
    concept is_parent_pointer = std::is_same_v< std::decay_t<T>, VgjsJobParentPointer >;

    template<typename T>
    concept is_tag = std::is_same_v< std::decay_t<T>, tag_t >;

    template<typename T>
    concept is_schedulable = is_function<T> || is_return_object<T>::value || is_parent_pointer<T> || is_tag<T>;

    //---------------------------------------------------------------------------------------------

    struct VgjsJobParent {
        VgjsJobParentPointer   m_next{0};

        thread_index_t         m_thread_index{};     //thread that the f should run on
        thread_type_t          m_type{};             //type of the call
        thread_id_t            m_id{};               //unique identifier of the call
        VgjsJobParentPointer   m_parent{};           //parent job that created this job

        std::atomic<uint32_t>  m_children{};         //number of children this job is waiting for
        bool                   m_is_function{ true };

        VgjsJobParent() = default;
        
        VgjsJobParent(thread_index_t index, thread_type_t type, thread_id_t id, VgjsJobParentPointer parent)
            : m_thread_index{ index }, m_type{ type }, m_id{ id }, m_parent{ parent } {};

        virtual void resume() = 0;
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

        VgjsJob(const VgjsJob& f) = default;
        VgjsJob(VgjsJob&& f) = default;
        VgjsJob& operator= (const VgjsJob& f) = default;
        VgjsJob& operator= (VgjsJob&& f) = default;
        void resume() noexcept { m_function(); }
    };


    //---------------------------------------------------------------------------------------------

    template<typename T>
    class VgjsCoroPromiseBase : public VgjsJobParent {
    private:
        coroutine_handle<>  m_handle{};    //<handle of the coroutine

    public:
        explicit VgjsCoroPromiseBase() noexcept : m_handle{ coroutine_handle<VgjsCoroPromiseBase<T>>::from_promise(*this) } {
            m_is_function = false;
        };
        
        auto unhandled_exception() noexcept -> void { std::terminate(); };
        auto initial_suspend() noexcept -> suspend_always { return {}; };
        auto resume() noexcept -> void { m_handle.resume(); };

        template<typename U>
        auto await_transform(U&& func) noexcept -> awaitable_tuple<T, U> { return { std::tuple<U&&>(std::forward<U>(func)) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>&& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { tuple }; };

        auto await_transform(thread_index_t index) noexcept -> awaitable_resume_on<T> { return { index }; };
        auto await_transform(tag_t tg) noexcept -> awaitable_tag<T> { return { tg }; };
        auto final_suspend() noexcept -> final_awaiter<T> { return {}; };
    };

    template<typename T>
    class VgjsCoroPromise : public VgjsCoroPromiseBase<T> {
        T m_value{};                     //<a local storage of the value, use if parent is a coroutine
    public:
       void return_value(T t) { this->m_value = t; }
       auto get_return_object() noexcept -> VgjsCoroReturn<T>;
    };

    template<>
    class VgjsCoroPromise<void> : public VgjsCoroPromiseBase<void> {
    public:
        void return_void() noexcept {}
        auto get_return_object() noexcept -> VgjsCoroReturn<void>;
    };


    //--------------------------------------------------------

    template<typename T>
    class VgjsCoroReturn {
    public:
        using promise_type = VgjsCoroPromise<T>;

    private:
        coroutine_handle<promise_type> m_handle{};       //<handle to Coro promise

    public:

        VgjsCoroReturn() noexcept {};
        VgjsCoroReturn(coroutine_handle<promise_type> h) noexcept {};
        ~VgjsCoroReturn() noexcept {}

        bool ready() noexcept {
            return true;
        }

        T get() noexcept { return {}; }

        /*decltype(auto) operator() (thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            auto promise = 
            m_promise->m_thread_index = index;
            m_promise->m_type = type;
            m_promise->m_id = id;
            return std::move(*this);
        }*/
    };


    template<typename T>
    auto VgjsCoroPromise<T>::get_return_object() noexcept -> VgjsCoroReturn<T> { return { coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this) }; };


    //---------------------------------------------------------------------------------------------

    template<typename T, bool SYNC, uint64_t LIMIT>
    class VgjsQueue {
    private:
        std::mutex  m_mutex{};
        T*          m_first{};
        T*          m_last{};
        uint64_t    m_size{ 0 };

    public:
        VgjsQueue() {};
        VgjsQueue(VgjsQueue&& rhs) {};

        auto size() { 
            if constexpr (SYNC) std::lock_guard<std::mutex> guard(m_mutex);
            return m_size;
        }

        void push(T* job) noexcept {
            if (m_size > LIMIT) {
                delete job;
                return;
            }
            if constexpr (SYNC) std::lock_guard<std::mutex> guard(m_mutex);
            if (m_last) m_last->m_next = job;
            m_last = job;
            job->m_next = nullptr;
            m_first = ( m_first ? m_first : job);
            ++m_size;
        }

        T* pop() noexcept {
            if constexpr (SYNC) std::lock_guard<std::mutex> guard(m_mutex);
            if (!m_first) return {};
            T* res = m_first;
            m_first = (T*)m_first->m_next;
            if (!m_first) m_last = nullptr;
            --m_size;
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

        static inline thread_local VgjsJobParentPointer m_current_job{};
        VgjsQueue<VgjsJob, false, 1 << 12>              m_recycle_jobs;
        static inline std::vector<std::unique_ptr<std::condition_variable>> m_cv;
        static inline std::vector<std::unique_ptr<std::mutex>>              m_mutex{};

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

        int64_t get_thread_count() { 
            return m_thread_count.load(); 
        };


        void task(thread_index_t index, thread_count_t count) noexcept {
            m_thread_count++;
            m_thread_count.notify_all();
            wait(count);

            thread_index_t my_index{ index };                       //<each thread has its own number
            thread_index_t other_index{ my_index };
            std::unique_lock<std::mutex> lk(*m_mutex[my_index]);

            while (!m_terminate) {  //Run until the job system is terminated
                bool found = false;
                found = (m_current_job = (VgjsJobParentPointer)m_local_job_queues[my_index].pop()) || (m_current_job = (VgjsJobParentPointer)m_global_job_queues[my_index].pop());
                if(found) { 
                    ((VgjsJobPointer)m_current_job)->m_function();          //avoid virtual call
                    m_recycle_jobs.push((VgjsJobPointer)m_current_job);     //recycle job
                }
                found = found || ((m_current_job = m_local_coro_queues[my_index].pop()) || (m_current_job = m_global_coro_queues[my_index].pop()));

                int64_t loops = count;
                while (!found && --loops > 0) {
                    other_index = (other_index + 1 == count ? 0 : other_index + 1);
                    if (my_index != other_index) {
                        if ( (found = (m_current_job = m_global_job_queues[other_index].pop()))) {
                            ((VgjsJobPointer)m_current_job)->m_function();          //avoid virtual call
                            m_recycle_jobs.push((VgjsJobPointer)m_current_job);     //recycle job
                        }
                        if(!found || (found = (m_current_job = m_global_coro_queues[other_index].pop()))) m_current_job->resume();
                    }
                }

                if (!found) m_cv[my_index]->wait_for(lk, std::chrono::microseconds(100));
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
            if (m_current_job) {
                m_thread_count--;
                m_thread_count.notify_all();
            }
            wait();
        }

        void wait(int64_t desired = 0) {
            do {
                auto num = m_thread_count.load();
                if(num!= desired) 
                    m_thread_count.wait(num);
            } while (m_thread_count.load() != desired);
        }

        //--------------------------------------------------------------------------------------------

        //template<typename F>
        //requires is_schedulable<F>
        //int64_t schedule(F&& f, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {

        int64_t schedule(VgjsJob* job, tag_t tag = tag_t{}) {
            thread_local static thread_index_t next_thread{0};

            if (tag >= 0) {                  //tagged scheduling
                //if (!m_tag_queues.contains(tg)) {
                //    m_tag_queues[tg] = std::make_unique<JobQueue<Job_base>>();
                //}
                //m_tag_queues.at(tg)->push(job); //save for later
                return 0;
            }

            next_thread = job->m_thread_index < 0 ? next_thread + 1 : job->m_thread_index;
            next_thread = (next_thread >= get_thread_count() ? thread_index_t{ 0 } : next_thread);
            //job->m_thread_index < 0 ? m_global_job_queues[next_thread].push(job) : m_local_job_queues[next_thread].push(job);
            m_cv[next_thread]->notify_all();

            return 1ul;
        }


        /*uint32_t schedule_job(Job_base* job, tag_t tg = tag_t{}) noexcept {
            thread_local static thread_index_t thread_index(rand() % m_thread_count);

            assert(job != nullptr);

            if (tg.value >= 0) {                  //tagged scheduling
                if (!m_tag_queues.contains(tg)) {
                    m_tag_queues[tg] = std::make_unique<JobQueue<Job_base>>();
                }
                m_tag_queues.at(tg)->push(job); //save for later
                return 0;
            }

            if (job->m_thread_index.value < 0 || job->m_thread_index.value >= (int)m_thread_count) {
                thread_index.value = (++thread_index.value) >= (decltype(thread_index.value))m_thread_count ? 0 : thread_index.value;
                m_global_queues[thread_index].push(job);
                //m_cv[thread_index.value]->notify_one();       //wake up the thread
                m_cv[0]->notify_all();       //wake up the thread
                return 1;
            }

            m_local_queues[job->m_thread_index.value].push(job); //to a specific thread
            //m_cv[job->m_thread_index]->notify_one();
            m_cv[0]->notify_all();       //wake up the thread
            return 1;
        };*/


        uint32_t schedule_tag(tag_t& tg, tag_t tg2 = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            /*if (!m_tag_queues.contains(tg)) return 0;

            JobQueue<Job_base>* queue = m_tag_queues[tg].get();   //get the queue for this tag
            uint32_t num_jobs = queue->size();

            if (parent != nullptr) {
                if (children < 0) children = num_jobs;     //if the number of children is not given, then use queue size
                parent->m_children.fetch_add((int)children);    //add this number to the number of children of parent
            }

            uint32_t num = num_jobs;        //schedule at most num_jobs, since someone could add more jobs now
            int i = 0;
            while (num > 0) {     //schedule all jobs from the tag queue
                Job_base* job = queue->pop();
                if (!job) return i;
                job->m_parent = parent;
                schedule_job(job, tag_t{});
                --num;
                ++i;
            }
            return i;*/
            return 1;
        };

        template<typename F>
        requires is_vector<F>::value
        uint32_t schedule(F&& f, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {

        }

        template<typename F>
        requires is_function<F>
        uint32_t schedule(F&& f, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            return schedule(new VgjsJob{ std::forward<decltype(f)>(f), index, type, id}, tag, parent, children );
        }

        /*template<typename F>
        requires is_return_object<F>::value
        uint32_t schedule(F&& f, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {

        }

        template<typename F>
        requires is_parent_pointer<F> && (!is_return_object<F>::value)
        uint32_t schedule(F&& f, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {

        }*/

        template<typename F>
        uint32_t schedule(F&& f, tag_t tag = tag_t{}, VgjsJobParentPointer parent = m_current_job, int32_t children = -1) noexcept {
            /*if constexpr (std::is_same_v<std::decay_t<F>, tag_t>) {
                return schedule_tag(f, tag, parent, children);
            }
            else {
                if constexpr (is_function<F>) {
                    return schedule_job(std::make_shared<VgjsJob>(f, index, type, id, m_current_job));
                }
                else if constexpr (is_parent_pointer<F>) {
                    return sched(f);
                }
                else if constexpr (is_return_object<std::decay_t<F>>::value) {

                }
                else if constexpr (is_tag<F>) {
                    return schedule_tag(function, tg, parent, children);
                }



                job->m_parent = nullptr;
                if (tg.value < 0) {
                    job->m_parent = parent;
                    if (parent != nullptr) {
                        if (children < 0) children = 1;
                        parent->m_children.fetch_add((int)children);
                    }
                }
                return schedule_job(job, tg);
            }*/
            return 0;
        };
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
#

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
        tag_t               m_tag;          //The tag to schedule to
        std::tuple<Ts&&...> m_tuple;        //tuple with all children to start
        std::size_t         m_number;       //total number of all new children to schedule

        template<typename U>
        size_t size(U& children) {
            return 1;
            /*if constexpr (is_pmr_vector< std::decay_t<U> >::value) { //if this is a vector
                return children.size();
            }
            if constexpr (std::is_same_v<std::decay_t<U>, tag_t>) { //if this is a tag
                m_tag = children;
                return 0;
            }
            return 1;   //if this is a std::function, Function, or Coro
            */
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
                    VgjsJobSystem().schedule(std::forward<T>(children), m_tag, (VgjsJobParentPointer) & h.promise(), (int32_t)m_number);   //in first call the number of children is the total number of all jobs
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

        awaitable_tuple(std::tuple<Ts&&...> tuple) noexcept : m_tag{}, m_number{ 0 }, m_tuple(std::forward<std::tuple<Ts&&...>>(tuple)) {};
    };


    template<typename U>
    struct final_awaiter : public suspend_always {

        bool await_suspend(coroutine_handle<VgjsCoroPromise<U>> h) noexcept { //called after suspending
            /*auto& promise = h.promise();
            bool is_parent_function = promise.m_is_parent_function;
            auto parent = promise.m_parent;

            if (parent != nullptr) {          //if there is a parent
                if (is_parent_function) {       //if it is a Job
                    JobSystem().child_finished((Job*)parent);//indicate that this child has finished
                }
                else {
                    uint32_t num = parent->m_children.fetch_sub(1);        //one less child
                    if (num == 1) {                                             //was it the last child?
                        JobSystem().schedule_job(parent);      //if last reschedule the parent coro
                    }
                }
            }
            return !is_parent_function; //if parent is coro, then you are in sync -> the future will destroy the promise
            */
            return true;
        }

        final_awaiter() noexcept {
        
        };
    };

}


