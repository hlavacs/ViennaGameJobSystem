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

    template<typename>
    struct is_pmr_vector : std::false_type {};

    template<typename T>
    struct is_pmr_vector<std::pmr::vector<T>> : std::true_type {};

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Schedule a task into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    */
    template<typename T>
    requires (std::is_base_of<task_base, T>::value)
    void schedule(T& task, uint32_t thd = -1 , Job_base* parent = nullptr) noexcept {
        if (parent == nullptr) {
            parent = JobSystem::instance()->current_job();
        }
        task.promise()->m_parent = parent;                      //remember parent
        if (parent != nullptr) {
            parent->m_children++;                               //await the completion of all children      
        }
        if (thd != -1) {
            task.promise()->m_thread_index = thd;
        }
        JobSystem::instance()->schedule(task.promise(), thd, parent);
    };

    /**
    * \brief Schedule a task into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    */
    template<typename T>
    requires (std::is_base_of<task_base, T>::value)
    void schedule( T&& task, uint32_t thd = -1, Job_base* parent = nullptr) noexcept {
        schedule( task, thd, parent );
    };


    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of coroutine task_promise, derived from Job so it can be scheduled.
    * 
    * The base promise class derives from Job so it can be scheduled on the job system!
    * The base does not depend on any type information that is stored with the promise.
    * It defines default behavior and the operators for allocating and deallocating memory
    * for the promise.
    */
    class task_promise_base : public Job_base {

    public:
        task_promise_base() noexcept {};        //constructor

        void unhandled_exception() noexcept {   //in case of an exception terminate the program
            std::terminate();
        }

        std::experimental::suspend_always initial_suspend() noexcept {  //always suspend at start when creating a coroutine task
            return {};
        }

        void child_finished() noexcept {                    //if children are running then the coro must be suspended
            uint32_t num = m_children.fetch_sub(1);
            if ( num == 1) {                                //if there are no more children
                JobSystem::instance()->schedule(this);      //then resume the coroutine by scheduling its promise
            }
        }

        /**
        * \brief Use the given memory resource to create the promise object for a normal function.
        * 
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        */
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

        /**
        * \brief Use the given memory resource to create the promise object for a member function.
        *
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        */
        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, mr, args...);
        }

        /**
        * \brief Create a promise object for a class member function using the system standard allocator.
        *
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        */
        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        /**
        * \brief Create a promise object using the system standard allocator.
        *
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        */
        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        /**
        * \brief Use the pointer after the promise as deallocator
        */
        void operator delete(void* ptr, std::size_t sz) noexcept {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            auto allocator = (std::pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
            (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
        }
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of coroutine task. Independent of promise return type.
    */
    class task_base {
    public:
        task_base() noexcept {};                                    //constructor
        virtual bool resume() noexcept { return true; };                     //resume the task
        virtual task_promise_base* promise() noexcept { return nullptr; };   //get the promise to use it as Job 
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of awaiter, contains default behavior.
    */
    struct awaiter_base {
        bool await_ready() noexcept {   //default: go on with suspension
            return false;
        }

        void await_resume() noexcept {} //default: no return value
    };

    /**
    * \brief Awaiter for awaiting a tuple of vector of tasks of type task<T> or std::function<void(void)>
    *
    * The tuple can contain vectors with different types.
    * The caller will then await the completion of the tasks. Afterwards,
    * the return values can be retrieved by calling get().
    */
    template<typename... Ts>
    struct awaitable_tuple {

        struct awaiter : awaiter_base {
            task_promise_base*                      m_promise;              //caller of the co_await (Job and promise at the same time)
            std::tuple<std::pmr::vector<Ts>...>&    m_tuple;                //vector with all children to start
            int32_t                                 m_thread_index;

            bool await_ready() noexcept {                                 //suspend only if there are no tasks
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::size_t num = 0;
                    std::initializer_list<int>{ (  num += std::get<Idx>(m_tuple).size(), 0) ...}; //called for every tuple element
                    return (num == 0);
                };
                return f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::initializer_list<int>{ ( schedule( std::get<Idx>(m_tuple), m_thread_index, m_promise) , 0) ...}; //called for every tuple element
                };
                f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            awaiter(task_promise_base* promise, std::tuple<std::pmr::vector<Ts>...>& children, int32_t thread_index = -1) noexcept
                : m_promise(promise), m_tuple(children), m_thread_index(thread_index)  {};
        };

        task_promise_base*                      m_promise;            //caller of the co_await
        std::tuple<std::pmr::vector<Ts>...>&    m_tuple;              //vector with all children to start
        int32_t                                 m_thread_index;

        awaitable_tuple(task_promise_base* promise, std::tuple<std::pmr::vector<Ts>...>& children, int32_t thread_index = -1) noexcept
            : m_promise(promise), m_tuple(children), m_thread_index(thread_index) {};

        awaiter operator co_await() noexcept { return { m_promise, m_tuple, m_thread_index }; };
    };


    /**
    * \brief Awaiter for awaiting a task of type task<T> or std::function<void(void)>, or std::pmr::vector thereof
    *
    * The caller will await the completion of the task(s). Afterwards,
    * the return values can be retrieved by calling get() for task<t>
    */
    template<typename T>
    struct awaitable_task {

        struct awaiter : awaiter_base {
            task_promise_base*          m_promise;    //caller of the co_await (Job and promise at the same time)
            T&                          m_child;      //child task
            int32_t                     m_thread_index;

            bool await_ready() noexcept {             //suspend only there are no tasks
                if constexpr (is_pmr_vector<T>::value) {
                    return m_child.empty();
                }
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                schedule( m_child, m_thread_index, m_promise);    //schedule the promise or function as job by calling the correct version
            }

            awaiter(task_promise_base* promise, T& child, int32_t thread_index = -1) noexcept
                : m_promise(promise), m_child(child), m_thread_index(thread_index) {};
        };

        task_promise_base*  m_promise;            //caller of the co_await
        T&                  m_child;              //child task
        int32_t             m_thread_index;

        awaitable_task(task_promise_base* promise, T& child, int32_t thread_index = -1) noexcept
            : m_promise(promise), m_child(child), m_thread_index(thread_index) {};

        awaiter operator co_await() noexcept { return { m_promise, m_child, m_thread_index}; };
    };


    /**
    * \brief Awaiter for changing the thread that the job is run on
    */
    struct awaitable_resume_on {
        struct awaiter : awaiter_base {
            task_promise_base*  m_promise;
            int32_t             m_thread_index;

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_thread_index = m_thread_index;
                JobSystem::instance()->schedule(m_promise);
            }

            awaiter(task_promise_base* promise, int32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};
        };

        task_promise_base*  m_promise;
        int32_t             m_thread_index;

        awaitable_resume_on(task_promise_base* promise, int32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};

        awaiter operator co_await() noexcept { return { m_promise, m_thread_index }; };
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Promise of the task. Depends on the return type.
    * 
    * The task promise can hold values that are produced by the task. They can be
    * retrieved later by calling get() on the task (which calls get() on the promise)
    */
    template<typename T>
    class task_promise : public task_promise_base {
    private:
        T m_value{};

    public:

        task_promise() noexcept : task_promise_base{}, m_value{} {};

        task<T> get_return_object() noexcept {  //get task coroutine from promise
            return task<T>{ std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this) };
        }

        bool resume() noexcept {                //resume the task at its suspension point
            auto coro = std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this);
            if (coro && !coro.done())
                coro.resume();
            return !coro.done();
        };

        void return_value(T t) noexcept {   //is called by co_return <VAL>, saves <VAL> in m_value
            m_value = t;
        }

        T get() noexcept {      //return the stored m_value to the caller
            return m_value;
        }

        bool deallocate() noexcept {
            auto coro = std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this);
            if (coro) {
                coro.destroy();
            }

            return false;
        };

        template<typename... Ts>    //called by co_await for std::pmr::vector<Ts>& tasks or functions
        awaitable_tuple<Ts...> await_transform( std::tuple<std::pmr::vector<Ts>...>& tasks) noexcept {
            return { this, tasks, -1 };
        }

        template<typename T>        //called by co_await for tasks or functions, 
        awaitable_task<T> await_transform(T& task) noexcept {
            return { this, task, -1 };
        }

        template<typename... Ts>    //called by co_await for std::pmr::vector<Ts>& tasks or functions, 2nd parameters is the thread to use
        awaitable_tuple<Ts...> await_transform(std::pair< std::tuple<std::pmr::vector<Ts>...>*, int32_t>& tasks) noexcept {
            return { this, *std::get<0>(tasks), std::get<1>(tasks) };
        }

        template<typename T>        //called by co_await for tasks or functions, 2nd parameters is the thread to use
        awaitable_task<T> await_transform(std::pair<T*, int32_t>& task) noexcept {
            return { this, *std::get<0>(task), std::get<1>(task) };
        }

        awaitable_resume_on await_transform(uint32_t thread_index) noexcept { //called by co_await for INT, for changing the thread
            return { this, thread_index };
        }

        /**
        * \brief When a coroutine reaches its end, it may suspend a last time using such a final awaiter
        *
        * Suspending as last act prevents the promise to be destroyed. This way the caller
        * can retrieve the stored value by calling get(). Also we want to resume the parent
        * if all children have finished their tasks.
        */
        struct final_awaiter {
            task_promise_base* m_promise;

            final_awaiter(task_promise_base* promise) noexcept : m_promise(promise) {}

            bool await_ready() noexcept {   //go on with suspension at final suspension point
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<> h) noexcept { //called after suspending
                if (m_promise->m_parent) {                      //if there is a parent
                    m_promise->m_parent->child_finished();      //tell parent that this child has finished
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { //create the final awaiter at the final suspension point
            return { this };
        }
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief The main task class. Can be constructed to return any value type
    *
    * The task is an accessor class much like a std::future that is used to
    * access the promised value once it is awailable.
    * It also holds a handle to the task promise.
    */
    template<typename T>
    class task : public task_base {
    public:

        using promise_type = task_promise<T>;

    private:
        std::experimental::coroutine_handle<promise_type> m_coro;   //handle to task promise

    public:
        task(task<T>&& t) noexcept : m_coro(std::exchange(t.m_coro, {})) {}

        ~task() noexcept {
            if (m_coro) {
                if (m_coro.promise().m_parent != nullptr) {         //if the parent is a coro then destroy the coro, 
                    if (!m_coro.promise().m_parent->is_job()) {     //because they are in sync
                        m_coro.destroy();                           //if you do not want this then move task
                    }
                    else {  //if the parent is a job+function, then the function often returns before the child finishes
                    }       //don not do anything but wait for the children to finish
                }
            }
        }

        T get() noexcept {                  //retrieve the promised value
            return m_coro.promise().get();
        }

        task_promise_base* promise() noexcept { //get a pointer to the promise (can be used as Job)
            return &m_coro.promise();
        }

        void thread_index(uint32_t ti) {
            m_coro.promise().m_thread_index = ti;
        }

        bool resume() noexcept {    //resume the task by calling resume() on the handle
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}
    };

}

