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

    class task_base;
    template<typename T> class task;
    class task_promise_base;


    template<typename T>
    void schedule(T* task, int32_t thd = -1) {
        JobSystem<task_promise_base>::instance()->schedule(task, thd);
        return;
    };

    //---------------------------------------------------------------------------------------------------

    class task_base {
    public:
        task_base() {};
        virtual bool resume() = 0;
        virtual task_promise_base* promise() = 0;
    };

    //---------------------------------------------------------------------------------------------------

    class task_promise_base {
    public:
        task_promise_base*  m_next = nullptr;
        std::atomic<int>    m_children = 0;
        task_promise_base*  m_continuation = nullptr;
        std::atomic<bool>   m_ready = false;

        task_promise_base() {};

        virtual bool resume() = 0;

        void unhandled_exception() noexcept {
            std::terminate();
        }

        void operator() () {
            resume();
        }

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

    //---------------------------------------------------------------------------------------------------

    struct awaiter_base {
        bool await_ready() noexcept {
            return false;
        }

        void await_resume() noexcept {
            return;
        }
    };

    struct awaitable_vector {
        struct awaiter : awaiter_base {
            task_promise_base* m_promise;
            std::pmr::vector<task_base*>& m_children;

            bool await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                for (auto ptr : m_children) {
                    auto promise = ptr->promise();
                    promise->m_continuation = m_promise;
                    m_promise->m_children++;
                    schedule(promise);
                }
                return false;
            }

            awaiter(task_promise_base* promise, std::pmr::vector<task_base*>& children) : m_promise(promise), m_children(children) {};
        };

        task_promise_base* m_promise;
        std::pmr::vector<task_base*>& m_children;

        awaitable_vector(task_promise_base* promise, std::pmr::vector<task_base*>& children) : m_promise(promise), m_children(children) {};

        awaiter operator co_await() { return { m_promise, m_children }; };
    };


    //---------------------------------------------------------------------------------------------------

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

        awaitable_vector await_transform( std::pmr::vector<task_base*>& tasks) {
            return { this, tasks };
        }

        struct final_awaiter {
            task_promise_base* m_promise;

            final_awaiter(task_promise_base* promise) : m_promise(promise) {}

            bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<task_promise<T>> h) noexcept {
                if (m_promise->m_continuation) {
                    auto children = m_promise->m_continuation->m_children.fetch_sub(1);
                    if (children <= 1) {
                        schedule(m_promise->m_continuation);
                    }
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return { this };
        }

    };

    //---------------------------------------------------------------------------------------------------

    template<typename T>
    class task : public task_base {
    public:

        using promise_type = task_promise<T>;

    private:
        std::experimental::coroutine_handle<promise_type> m_coro;

    public:
        task(task<T>&& t) noexcept : m_coro(std::exchange(t.m_coro, {})) {}

        ~task() {
            if (m_coro)             //use done() only if coro suspended
                m_coro.destroy();   //if you do not want this then move task
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

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}

    };



    template<typename T>
    void wait_all(std::pmr::vector<T> tasks) {
        return;

    };

    template<typename T>
    void resume_on() {
        return;
    }


}

