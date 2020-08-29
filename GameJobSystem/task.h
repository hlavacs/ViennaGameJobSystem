#pragma once



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


namespace vgjs {

    template<typename T> class task;

    class task_promise_base {
    public:
        task_promise_base* m_next = nullptr;
        std::atomic<int>     m_children = 0;
        task_promise_base* m_continuation;
        std::atomic<bool>    m_ready = false;

        task_promise_base()
        {};

        virtual bool resume() { return true; };

        void unhandled_exception() noexcept {
            std::terminate();
        }

        void operator() () {
            resume();
        }

        /*bool continue_parent() {
            if (m_continuation && !m_continuation.done())
                m_continuation.resume();
            return !m_continuation.done();
        };*/

        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            char* ptr = (char*)mr->allocate(allocatorOffset + sizeof(mr));
            if (ptr == nullptr) {
                std::terminate();
            }
            *reinterpret_cast<std::pmr::memory_resource**>(ptr + allocatorOffset) = mr;
            return ptr;
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) {
            return operator new(sz, std::allocator_arg, mr, args...);
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        void operator delete(void* ptr, std::size_t sz) {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            auto allocator = (std::pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
            (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
        }

    };


    template<typename T>
    class task_promise : public task_promise_base {
    private:
        T m_value{};

    public:

        task_promise() : task_promise_base{}, m_value{} {};

        std::experimental::suspend_always initial_suspend() noexcept {
            return {};
        }

        task<T> get_return_object() noexcept {
            return task<T>{ std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this) };
        }

        bool resume() {
            auto coro = std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this);
            if (coro && !coro.done())
                coro.resume();
            return !coro.done();
        };

        void return_value(T t) noexcept {
            m_value = t;
        }

        T get() {
            return m_value;
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<task_promise<T>> h) noexcept {
                task_promise<T>& promise = h.promise();
                if (!promise.m_continuation) return;

                if (promise.m_ready.exchange(true, std::memory_order_acq_rel)) {
                    //promise.m_continuation.resume();
                    //JobSystem::instance()->schedule(promise.m_continuation);
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

    };


    //---------------------------------------------------------------------------------------------------


    class task_base {
    private:
        task_base* m_next = nullptr;

    public:
        task_base() {};
        virtual bool resume() = 0;
        virtual task_promise_base* promise() = 0;
    };

    template<typename T>
    class task : public task_base {
    public:

        using promise_type = task_promise<T>;

    private:
        std::experimental::coroutine_handle<promise_type> m_coro;

    public:
        task(task<T>&& t) noexcept : m_coro(std::exchange(t.m_coro, {})) {}

        ~task() {
            //if (m_coro && m_coro.done())
            //    m_coro.destroy();
        }

        T get() {
            return m_coro.promise().get();
        }

        task_promise_base* promise() {
            return &m_coro.promise();
        }

        bool resume() {
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };


        struct awaiter {
            std::experimental::coroutine_handle<promise_type> m_coro;

            awaiter(std::experimental::coroutine_handle<promise_type> coro) : m_coro(coro) {};

            bool await_ready() noexcept {
                return false;
            }

            bool await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                auto* promise = &m_coro.promise();
                promise->m_continuation = JobSystem<task_promise_base>::instance()->current_job();

                //m_coro.resume();
                schedule(promise);

                return !promise->m_ready.exchange(true, std::memory_order_acq_rel);
            }

            T await_resume() noexcept {
                promise_type& promise = m_coro.promise();
                return promise.get();
            }

        };

        awaiter operator co_await() { return awaiter{ m_coro }; };

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}

    };



    struct awaiter {

        bool await_ready() noexcept {
            return false;
        }

        bool await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
            //auto* promise = &m_coro.promise();
            //promise->m_continuation = JobSystem::instance()->current_job();

            //m_coro.resume();
            //schedule(promise);

            return false; // !promise->m_ready.exchange(true, std::memory_order_acq_rel);
        }

        void await_resume() noexcept {
            //promise_type& promise = m_coro.promise();
            return; // promise.get();
        }

    };

    template<typename T>
    awaiter schedule(T* task, int32_t thd = -1) {
        JobSystem<task_promise_base>::instance()->schedule(task, thd);
        return {};
    };

    template<typename T>
    awaiter wait_all(std::pmr::vector<T> tasks) {
        return {};

    };

    template<typename T>
    awaiter resume_on() {
        return {};
    }


}

