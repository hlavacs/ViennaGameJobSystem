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
        T operator()() const noexcept { return value; };
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

    struct VgjsJobParent;
    using VgjsJobParentPointer = std::shared_ptr<VgjsJobParent>;

    template<typename T>
    concept is_function = std::is_convertible_v< std::decay_t<T>, std::function<void(void)> >;

    template<typename T>
    concept is_parent_pointer = std::is_same_v< std::decay_t<T>, VgjsJobParentPointer >;

    template<typename T>
    concept is_schedulable = is_function<T> || is_parent_pointer<T>;


    class VgjsJobSystem;
    //---------------------------------------------------------------------------------------------

    struct VgjsJobParent {
        thread_index_t         m_thread_index{};     //thread that the f should run on
        thread_type_t          m_type{};             //type of the call
        thread_id_t            m_id{};               //unique identifier of the call
        VgjsJobParentPointer   m_parent{};           //parent job that created this job
        std::atomic<uint32_t>  m_children{};         //number of children this job is waiting for

        VgjsJobSystem*         m_system{};
        thread_index_t         m_current_thread_index{};  //thread that the f is running on

        VgjsJobParent() = default;
        VgjsJobParent(thread_index_t index, thread_type_t type, thread_id_t id, VgjsJobParentPointer parent)
            : m_thread_index{ index }, m_type{ type }, m_id{ id }, m_parent{ parent } {};
        virtual void resume(thread_index_t index) = 0;
    };

    struct VgjsJob : public VgjsJobParent {
        std::function<void(void)> m_function = []() {};  //empty function

        VgjsJob() : VgjsJobParent() {};

        template<typename F>
        VgjsJob(F&& f = []() {}
            , thread_index_t index = thread_index_t{ -1 }
            , thread_type_t type = thread_type_t{}
            , thread_id_t id = thread_id_t{}
            , VgjsJobParentPointer parent = nullptr) : VgjsJobParent(index, type, id, parent) {};

        VgjsJob(const VgjsJob& f) = default;
        VgjsJob(VgjsJob&& f) = default;
        VgjsJob& operator= (const VgjsJob& f) = default;
        VgjsJob& operator= (VgjsJob&& f) = default;
        void resume(thread_index_t index) noexcept {
            m_current_thread_index = index;
            m_function();
        }
    };

    //---------------------------------------------------------------------------------------------

    class VgjsJobSystem;

    template<typename T>
    struct awaitable_resume_on;

    template<typename PT, typename... Ts>
    struct awaitable_tuple;

    template<typename PT>
    struct awaitable_tag;

    template<typename U>
    struct final_awaiter;


    //---------------------------------------------------------------------------------------------


    template<typename T>
    class VgjsCoroReturn;

    template<typename T = void>
    class VgjsCoroPromise : public VgjsJobParent {
    private:
        coroutine_handle<T> m_handle;    //<handle of the coroutine
        T m_value{};                     //<a local storage of the value, use if parent is a coroutine

    public:
        explicit VgjsCoroPromise() noexcept : m_handle{ coroutine_handle<VgjsCoroPromise<T>>::from_promise(*this) } {};
        
        auto unhandled_exception() noexcept -> void { std::terminate(); };
        auto initial_suspend() noexcept -> suspend_always { return {}; };
        auto resume() noexcept -> void { m_handle.resume(); };
        static auto get_return_object_on_allocation_failure() -> VgjsCoroReturn<T> { return {}; };
        auto get_return_object() noexcept -> VgjsCoroReturn<T> { return {m_handle}; };
        auto return_value(T t) noexcept -> void { m_value = t; }

        template<typename U>
        auto await_transform(U&& func) noexcept -> awaitable_tuple<T, U> { return { std::tuple<U&&>(std::forward<U>(func)) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>&& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        template<typename... Ts>
        auto await_transform(std::tuple<Ts...>& tuple) noexcept -> awaitable_tuple<T, Ts...> { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        auto await_transform(thread_index_t index) noexcept -> awaitable_resume_on<T> { return { index }; };
        auto await_transform(tag_t tg) noexcept -> awaitable_tag<T> { return { tg }; };
        auto final_suspend() noexcept -> final_awaiter<T> { return {}; };
    };

    template<typename T>
    using VgjsCoroPromisePointer = std::shared_ptr<VgjsCoroPromise<T>>;


    //--------------------------------------------------------

    template<typename T = void >
    class VgjsCoroReturn {
    private:
        using promise_type = VgjsCoroPromise<T>;
        coroutine_handle<promise_type> m_handle;       //<handle to Coro promise

    public:
        VgjsCoroReturn(coroutine_handle<promise_type> h) {};

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
    using VgjsCoroReturnPointer = std::shared_ptr<VgjsCoroReturn<T>>;


    //---------------------------------------------------------------------------------------------

    template<typename T>
    class VgjsQueue {
    private:
        std::mutex      m_mutex{};
        std::queue<T>   m_queue{};

    public:
        VgjsQueue() = default;
        VgjsQueue(const VgjsQueue& q) noexcept { m_queue = q.m_queue; };
        VgjsQueue(VgjsQueue&& q) noexcept { m_queue = std::move(q.m_queue); };

        template<typename J>
        requires std::is_same_v< std::decay_t<J>, T>
        void push(J&& job) noexcept {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_queue.push(std::forward<T>(job));
        }

        T pop() noexcept {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_queue.empty()) return {};
            T ref = m_queue.front();
            m_queue.pop();
            return ref;  //hope for RVO
        }
    };


    //---------------------------------------------------------------------------------------------


    template<class... Ts> struct visitor_t : Ts... { using Ts::operator()...; };

    class VgjsJobSystem {
    private:
        std::atomic<uint32_t>                         m_thread_count{0};       //<number of threads in the pool
        std::vector<std::thread>                      m_threads;	        //<array of thread structures
        std::vector<VgjsQueue<VgjsJobParentPointer>>  m_global_queues;	    //<each thread has its shared Job queue, multiple produce, multiple consume
        std::vector<VgjsQueue<VgjsJobParentPointer>>  m_local_queues;	    //<each thread has its own Job queue, multiple produce, single consume
        thread_local inline static VgjsJobParentPointer      m_current_job{};
        bool                                          m_terminate{ false };
        std::vector<std::unique_ptr<std::condition_variable>>  m_cv;
        static inline std::vector<std::unique_ptr<std::mutex>> m_mutex;

    public:

        VgjsJobSystem(thread_count_t count = thread_count_t(0), thread_index_t start = thread_index_t(0)) {
            count = ( count() == 0 ? std::thread::hardware_concurrency() : count() );

            VgjsQueue<VgjsJobParentPointer> p{};

            for (auto i = start(); i < count(); ++i) {
                m_global_queues.emplace_back();     //global job queue
                m_local_queues.emplace_back();     //local job queue
                m_cv.emplace_back(std::make_unique<std::condition_variable>());
                m_mutex.emplace_back(std::make_unique<std::mutex>());
                m_threads.push_back( std::thread( &VgjsJobSystem::task, this, thread_index_t(i), count ) );	//spawn the pool threads
            }
            wait(count());
        };

        ~VgjsJobSystem() {}

        int64_t get_thread_count() { return m_thread_count.load(); };

        void task(thread_index_t index, thread_count_t count) noexcept {
            thread_index_t my_index{ index };                       //<each thread has its own number
            thread_index_t other_index{ my_index };
            std::unique_lock<std::mutex> lk(*m_mutex[index()]);

            m_thread_count++;
            m_thread_count.notify_all();
            wait(count());

            while (!m_terminate) {  //Run until the job system is terminated
                m_current_job = m_local_queues[my_index()].pop();
                if (m_current_job == nullptr) {
                    m_current_job = m_global_queues[my_index()].pop();
                    auto loops = count();
                    while (m_current_job == nullptr && --loops>0) {
                        other_index = (other_index() + 1 == count() ? 0 : other_index() + 1);
                        if(my_index() != other_index()) m_current_job = m_global_queues[other_index()].pop();
                    }
                }
                if (m_current_job != nullptr) {
                    m_current_job->resume(my_index);
                }
                else {
                    m_cv[0]->wait_for(lk, std::chrono::microseconds(100));
                }
            }
            m_thread_count--;
            m_thread_count.notify_all();
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

        void wait(size_t desired = 0) {
            do {
                auto num = m_thread_count.load();
                if(num!= desired) 
                    m_thread_count.wait(num);
            } while (m_thread_count.load() != desired);
        }

        template<typename F>
        requires is_schedulable<F>
        void schedule(F&& f, thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            thread_local static thread_index_t next_thread{0};

            VgjsJobParentPointer job;
            if constexpr (!is_function<std::decay_t<F>>) {
                job = std::make_shared(VgjsJob{ f, index, type, id, m_current_job });
            }
            else if constexpr (is_parent_pointer<F> ) {
                job = f; 
            }

            next_thread = job->m_thread_index() < 0 ? next_thread() + 1 : job->m_thread_index();
            next_thread = (next_thread() >= get_thread_count() ? 0u : next_thread());
            job->m_thread_index() < 0 ? m_global_queues[next_thread()].push(job) : m_local_queues[next_thread()].push(job);
        }
    };


    //---------------------------------------------------------------------------------------------


    template<typename T>
    struct awaitable_resume_on : suspend_always {
        VgjsJobSystem* m_system;
        thread_index_t m_thread_index; //the thread index to use

        bool await_ready() noexcept {   //do not go on with suspension if the job is already on the right thread
            return (m_thread_index == system->m_thread_index);
        }

        void await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            h.promise().m_thread_index = m_thread_index;
            system->schedule(&h.promise());
        }

        awaitable_resume_on(VgjsJobSystem* system, thread_index_t index) noexcept : m_system{ system }, m_thread_index(index) {};
    };
#

    template<typename T>
    struct awaitable_tag : suspend_always {
        VgjsJobSystem* m_system;
        tag_t    m_tag;            //the tag to schedule
        int32_t  m_number = 0;     //Number of scheduled jobs

        bool await_ready() noexcept {  //do nothing if the given tag is invalid
            return m_tag() < 0;
        }

        bool await_suspend(coroutine_handle<VgjsCoroPromise<T>> h) noexcept {
            m_number = system->schedule(m_tag);
            return m_number > 0;     //if jobs were scheduled - await them
        }

        int32_t await_resume() {
            return m_number;
        }

        awaitable_tag(VgjsJobSystem* system, tag_t tg) noexcept : m_system{ system }, m_tag{ tg } {};
    };


    template<typename PT, typename... Ts>
    struct awaitable_tuple : suspend_always {
        VgjsJobSystem*      m_system;
        tag_t               m_tag;          ///<The tag to schedule to
        std::tuple<Ts&&...> m_tuple;          ///<vector with all children to start
        std::size_t         m_number;         ///<total number of all new children to schedule

        template<typename U>
        size_t size(U& children) {
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
            /*auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                m_number = (size(std::get<Idx>(m_tuple)) + ... + 0); //called for every tuple element
            };
            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_number == 0;   //nothing to be done -> do not suspend
            */
            return true;
        }

        bool await_suspend(coroutine_handle<VgjsCoroPromise<PT>> h) noexcept {
            /*auto g = [&, this]<std::size_t Idx>() {

                using tt = decltype(m_tuple);
                using T = decltype(std::get<Idx>(std::forward<tt>(m_tuple)));
                decltype(auto) children = std::forward<T>(std::get<Idx>(std::forward<tt>(m_tuple)));

                if constexpr (std::is_same_v<std::decay_t<T>, tag_t>) { //never schedule tags here
                    return;
                }
                else {
                    schedule(std::forward<T>(children), m_tag, &h.promise(), (int)m_number);   //in first call the number of children is the total number of all jobs
                    m_number = 0;                                               //after this always 0
                }
            };

            auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                (g.template operator() < Idx > (), ...); //called for every tuple element
            };

            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_tag.value < 0; //if tag value < 0 then schedule now, so return true to suspend
            */
            return true;
        }

        auto await_resume() {
            /*auto f = [&, this]<typename... Us>(Us&... args) {
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
            }*/
        }

        awaitable_tuple(VgjsJobSystem *system, std::tuple<Ts&&...> tuple) noexcept : m_tag{}, m_number{ 0 }, m_tuple(std::forward<std::tuple<Ts&&...>>(tuple)) {};
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
        }
    };

}


