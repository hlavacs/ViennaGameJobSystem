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

    //---------------------------------------------------------------------------------------------------

    template<typename U>
    struct deleter {
        std::pmr::memory_resource* m_mr;

        deleter(std::pmr::memory_resource* mr) : m_mr(mr) {}

        void operator()(U* b) {
            std::pmr::polymorphic_allocator<U> allocator(m_mr);
            allocator.deallocate(b, 1);
        }
    };

    template<typename T, typename... ARGS>
    auto make_unique_ptr(std::pmr::memory_resource* mr, ARGS&&... args) {
        std::pmr::polymorphic_allocator<T> allocator(mr);
        T* p = allocator.allocate(1);
        new (p) T(std::forward<ARGS>(args)...);
        return std::unique_ptr<T, deleter<T>>(p, mr);
    }

    template<typename T>
    using unique_ptr_vector = std::pmr::vector<std::unique_ptr<T, deleter<T>>>;

    //---------------------------------------------------------------------------------------------------


    /**
    * \brief Schedule a task promise into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    * \param[in] thd Optional thread index to run the task
    */
    template<typename T>
    void schedule(T& task, int32_t thd = -1) noexcept {
        JobSystem::instance()->schedule(task.promise(), thd);
        return;
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of coroutine task_promise. Independent of promise return type
    * 
    * The base class derives from Job so it can be scheduled on the job system.
    */
    class task_promise_base : public Job {
    public:
        task_promise_base() noexcept {};

        void unhandled_exception() noexcept {
            std::terminate();
        }

        std::experimental::suspend_always initial_suspend() noexcept {
            return {};
        }

        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            char* ptr = (char*)mr->allocate(allocatorOffset + sizeof(mr));
            if (ptr == nullptr) {
                std::terminate();
            }
            *reinterpret_cast<std::pmr::memory_resource**>(ptr + allocatorOffset) = mr;
            return ptr;
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, mr, args...);
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        void operator delete(void* ptr, std::size_t sz) noexcept {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            auto allocator = (std::pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
            (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
        }

    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of coroutine task. Independent of promise return type
    */
    class task_base {
    public:
        task_base() noexcept {};
        virtual bool resume() { return true; };
        virtual task_promise_base* promise() { return nullptr; };
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of awaiter, contains default behavior
    */
    struct awaiter_base {
        bool await_ready() noexcept {
            return false;
        }

        void await_resume() noexcept {}
    };

    /**
    * \brief Awaiter for awaiting a vector of tasks.
    * 
    * The vector must contain pointers pointing to tasks to be run as jobs.
    * The caller will then await the completion of the tasks. Afterwards,
    * the return values can be retrieved by calling get().
    */
    struct awaitable_vector {

        struct awaiter : awaiter_base {
            task_promise_base* m_promise;                       //caller of the co_await
            std::pmr::vector<task_base*>& m_children_vector;   //vector with all children to start

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_children.store( (uint32_t)m_children_vector.size()  );
                for (auto& ptr : m_children_vector) {                   //loop over all children
                    ptr->promise()->m_parent = m_promise;               //remember parent
                    JobSystem::instance()->schedule(ptr->promise());    //schedule the promise as job
                }
            }

            awaiter(task_promise_base* promise, std::pmr::vector<task_base*>& children) noexcept : m_promise(promise), m_children_vector(children) {};
        };

        task_promise_base* m_promise;                       //caller of the co_await
        std::pmr::vector<task_base*>& m_children_vector;   //vector with all children to start

        awaitable_vector(task_promise_base* promise, std::pmr::vector<task_base*>& children) noexcept : m_promise(promise), m_children_vector(children) {};

        awaiter operator co_await() noexcept { return { m_promise, m_children_vector }; };
    };

    template<typename T>
    struct awaitable_vector_unique {

        template<typename U>
        struct awaiter : awaiter_base {
            task_promise_base* m_promise;
            unique_ptr_vector<U>& m_children;

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_children.store((uint32_t)m_children.size());
                for (auto& ptr : m_children) {
                    ptr->promise()->m_parent = m_promise;
                    JobSystem::instance()->schedule(ptr->promise());
                }
            }

            awaiter(task_promise_base* promise, unique_ptr_vector<T>& children) noexcept
                : m_promise(promise), m_children(children) {};
        };

        task_promise_base* m_promise;
        unique_ptr_vector<T>& m_children;

        awaitable_vector_unique(task_promise_base* promise, unique_ptr_vector<T>& children) noexcept
            : m_promise(promise), m_children(children) {};

        awaiter<T> operator co_await() noexcept { return { m_promise, m_children }; };
    };


    /**
    * \brief Awaiter for changing the thread that the job is run on
    */
    struct awaitable_resume_on {
        struct awaiter : awaiter_base {
            task_promise_base*  m_promise;
            uint32_t            m_thread_index;

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                JobSystem::instance()->schedule(m_promise, m_thread_index);
            }

            awaiter(task_promise_base* promise, uint32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};
        };

        task_promise_base*  m_promise;
        uint32_t            m_thread_index;

        awaitable_resume_on(task_promise_base* promise, uint32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};

        awaiter operator co_await() noexcept { return { m_promise, m_thread_index }; };
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Promise of the task. Depends on the return type.
    */
    template<typename T>
    class task_promise : public task_promise_base {
    private:
        T m_value{};

    public:

        task_promise() noexcept : task_promise_base{}, m_value{} {};

        task<T> get_return_object() noexcept {
            return task<T>{ std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this) };
        }

        void operator() () noexcept {
            resume();
        }

        bool resume() noexcept {
            auto coro = std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this);
            if (coro && !coro.done())
                coro.resume();
            return !coro.done();
        };

        void return_value(T t) noexcept {
            m_value = t;
        }

        T get() noexcept {
            return m_value;
        }

        template<typename T>
        awaitable_vector_unique<T> await_transform(unique_ptr_vector<T>& tasks) noexcept {
            return { this, tasks };
        }

        awaitable_vector await_transform( std::pmr::vector<task_base*>& tasks) noexcept {
            return { this, tasks };
        }

        awaitable_resume_on await_transform(uint32_t thread_index) noexcept {
            return { this, thread_index };
        }

        struct final_awaiter {
            task_promise_base* m_promise;

            final_awaiter(task_promise_base* promise) noexcept : m_promise(promise) {}

            bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<> h) noexcept {
                if (m_promise->m_parent) {
                    if (m_promise->m_parent->m_children.fetch_sub(1) == 1) {
                        JobSystem::instance()->schedule(m_promise->m_parent);
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

        ~task() noexcept {
            if (m_coro && !m_coro.done())             //use done() only if coro suspended
                m_coro.destroy();   //if you do not want this then move task
        }

        T get() noexcept {
            return m_coro.promise().get();
        }

        task_promise_base* promise() noexcept {
            return &m_coro.promise();
        }

        bool resume() noexcept {
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}

    };



}

