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

    template<typename T, auto D, int64_t P>
    struct strong_type_t {
        T value{D};
        strong_type_t() = default;
        explicit strong_type_t(const T& v) { value = v; };
        explicit strong_type_t(T&& v) { value = std::move(v); };
        T operator()() const { return value; };
        strong_type_t<T, D, P>& operator=(const T& v) { value = v; return *this; };
        strong_type_t<T, D, P>& operator=(T&& v) { value = std::move(v); return *this; };
        strong_type_t(const strong_type_t<T, D, P>& v) { value = v.value; };
        strong_type_t(strong_type_t<T, D, P>&& v) { value = std::move(v.value); };
        auto operator<=>(const strong_type_t<T, D, P>& v) const { return value < v.value; };
    };

    using thread_count_t    = strong_type_t<int64_t, -1, 0>;
    using thread_index_t    = strong_type_t<int64_t, -1, 1>;
    using thread_id_t       = strong_type_t<int64_t, -1, 2>;
    using thread_type_t     = strong_type_t<int64_t, -1, 3>;
    using tag_t             = strong_type_t<int64_t, -1, 4>;
    using parent_t          = strong_type_t<int64_t, -1, 5>;

    struct VgjsJob;
    using job_pointer_t     = std::shared_ptr<VgjsJob>;

    //---------------------------------------------------------------------------------------------

    struct VgjsJobParent {
        thread_index_t              m_thread_index;        //thread that the f should run on
        thread_type_t               m_type;                //type of the call
        thread_id_t                 m_id;                  //unique identifier of the call
        job_pointer_t               m_parent;           //parent job that created this job
        std::atomic<uint64_t>       m_children;         //number of children this job is waiting for

        VgjsJobParent() = default;
        virtual void resume() = 0;
    };

    using VgjsJobPointer = std::shared_ptr<VgjsJobParent>;


    struct VgjsJob : VgjsJobParent {
        std::function<void(void)>   m_function = []() {};  //empty function

        VgjsJob() {};

        template<typename F>
        VgjsJob(F&& f = []() {}
            , thread_index_t index = thread_index_t{ -1 }
            , thread_type_t type = thread_type_t{}
            , thread_id_t id = thread_id_t{}
            , job_pointer_t parent = nullptr) : VgjsJobParent(index, type, id, parent) {};

        VgjsJob(const VgjsJob& f) = default;
        VgjsJob(VgjsJob&& f) = default;
        VgjsJob& operator= (const VgjsJob& f) = default;
        VgjsJob& operator= (VgjsJob&& f) = default;
        void resume() noexcept {
            m_function();
        }
    };


    //---------------------------------------------------------------------------------------------

    template<typename T>
    class VgjsCoroReturn;

    template<typename T = void>
    class VgjsCoroPromise : VgjsJobParent {
    private:
        std::experimental::coroutine_handle<> m_coro;   //<handle of the coroutine
        T m_value;        //<a local storage of the value, use if parent is a coroutine

    public:
        explicit VgjsCoroPromise() noexcept : m_coro{ std::experimental::coroutine_handle<Coro_promise<T>>::from_promise(*this) } {};
        
        void unhandled_exception() noexcept { std::terminate(); };
        std::experimental::suspend_always initial_suspend() noexcept { return {}; };

        void resume() noexcept {
        };

        //static VgjsCoroReturn<T>  get_return_object_on_allocation_failure() {};

        VgjsCoroReturn<T> get_return_object() noexcept {
            return {};
        };

        void return_value(T t) noexcept {   //is called by co_return <VAL>, saves <VAL> in m_value
        }
    };

    template<typename T>
    using VgjsCoroPromisePointer = std::shared_ptr<VgjsCoroPromise<T>>;


    //--------------------------------------------------------

    template<typename T = void >
    class VgjsCoroReturn {
    private:
        using promise_type = VgjsCoroPromise<T>;
        std::experimental::coroutine_handle<promise_type> m_coro;       ///<handle to Coro promise

    public:
        VgjsCoroReturn() noexcept {};

        VgjsCoroReturn(std::experimental::coroutine_handle<promise_type> h) {
        };

        ~VgjsCoroReturn() noexcept {
        }

        bool ready() noexcept {
            return true;
        }

        T get() noexcept {
            return {};
        }

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
        std::mutex      m_mutex;
        std::queue<T>   m_queue;

    public:
        VgjsQueue() {};
      
        void push(T&& job) {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_queue.push(std::forward<T>(job));
        }

        T pop() {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_queue.empty()) return {};
            T ref = m_queue.front();
            m_queue.pop();
            return ref;  //hope for RVO
        }
    };



    template<class... Ts> struct visitor_t : Ts... { using Ts::operator()...; };

    class VgjsJobSystem {
    private:
        thread_count_t           m_thread_count{ 0 };    ///<number of threads in the pool
        std::vector<std::thread> m_threads;	            ///<array of thread structures
        std::vector<VgjsQueue<VgjsJobPointer>>  m_global_queues;	///<each thread has its shared Job queue, multiple produce, multiple consume
        std::vector<VgjsQueue<VgjsJobPointer>>  m_local_queues;	    ///<each thread has its own Job queue, multiple produce, single consume
        VgjsJobPointer  m_current_job{};
        bool            m_terminate{ false };

    public:

        VgjsJobSystem(thread_count_t count = thread_count_t(0), thread_index_t start = thread_index_t(0)) {
            count = ( count() == 0 ? std::thread::hardware_concurrency() : count() );
            for (auto i = start(); i < count(); ++i) {
                m_threads.push_back( std::thread( &VgjsJobSystem::task, this, thread_index_t(i), count ) );	//spawn the pool threads
            }
        };

        ~VgjsJobSystem() {}

        void task(thread_index_t index, thread_count_t count = thread_count_t(0)) noexcept {
            thread_index_t my_index{ index };  //<each thread has its own number
            thread_index_t other_index{ my_index };
            static std::latch latch{ count() };

            latch.arrive_and_wait();

            while (!m_terminate) {  //Run until the job system is terminated
                m_current_job = m_local_queues[my_index()].pop();
                if (m_current_job == nullptr) {
                    m_current_job = m_global_queues[my_index()].pop();
                    if (m_current_job == nullptr) {
                        other_index = (other_index() + 1 == count() ? 0 : other_index() + 1);
                        m_current_job = m_global_queues[other_index()].pop();
                    }
                }
                if (m_current_job != nullptr) m_current_job->resume();
            }
        }
    };


}


