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
    void schedule( T& task ) noexcept {
        Job_base * parent = (task.promise()->m_parent != nullptr ? task.promise()->m_parent : JobSystem::instance()->current_job());       //remember parent
        if (parent != nullptr) {
            parent->m_children++;                               //await the completion of all children      
        }
        task.promise()->m_parent = parent;
        JobSystem::instance()->schedule( task.promise() );      //schedule the promise as job
    };

    /**
    * \brief Schedule a task into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    */
    template<typename T>
    requires (std::is_base_of<task_base, T>::value)
    void schedule( T&& task ) noexcept {
        schedule( task );
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
        task_base() noexcept {};                                              //constructor
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
            std::tuple<std::pmr::vector<Ts>...>& m_tuple;                //vector with all children to start

            bool await_ready() noexcept {                                 //suspend only if there are no tasks
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::size_t num = 0;
                    std::initializer_list<int>{ ( num += std::get<Idx>(m_tuple).size(), 0) ...}; //called for every tuple element
                    return (num == 0);
                };
                return f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::initializer_list<int>{ ( schedule( std::get<Idx>(m_tuple) ) , 0) ...}; //called for every tuple element
                };
                f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            awaiter( std::tuple<std::pmr::vector<Ts>...>& children, int32_t thread_index = -1) noexcept
                : m_tuple(children) {};
        };

        std::tuple<std::pmr::vector<Ts>...>& m_tuple;              //vector with all children to start

        awaitable_tuple(std::tuple<std::pmr::vector<Ts>...>& children ) noexcept : m_tuple(children) {};

        awaiter operator co_await() noexcept { return { m_tuple }; };
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
            T& m_child;      //child task

            bool await_ready() noexcept {             //suspend only there are no tasks
                if constexpr (is_pmr_vector<T>::value) {
                    return m_child.empty();
                }
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                schedule( m_child );  //schedule the promise or function as job by calling the correct version
            }

            awaiter( T& child) noexcept : m_child(child) {};
        };

        T& m_child;              //child task

        awaitable_task( T& child ) noexcept : m_child(child) {};

        awaiter operator co_await() noexcept { return { m_child }; };
    };


    /**
    * \brief Awaiter for changing the thread that the job is run on
    */
    struct awaitable_resume_on {
        struct awaiter : awaiter_base {
            task_promise_base*  m_promise;
            int32_t             m_thread_index;

            bool await_ready() noexcept {   //default: go on with suspension
                return m_thread_index == JobSystem::instance()->thread_index();
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_thread_index = m_thread_index;
                JobSystem::instance()->schedule(m_promise);     //Job* is scheduled directly
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
        template<typename U> struct final_awaiter;
        template<typename U> friend struct final_awaiter;
        template<typename U> friend class task;

    private:
        std::atomic<int> m_count = 2;   //sync with parent if parent is a Job
        T m_value{};                    //the value that should be returned

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

        bool deallocate() noexcept {    //called when the job system is destroyed
            auto coro = std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this);
            if (coro) {
                coro.destroy();
            }

            return false;
        };

        template<typename... Ts>    //called by co_await for std::pmr::vector<Ts>& tasks or functions
        awaitable_tuple<Ts...> await_transform( std::tuple<std::pmr::vector<Ts>...>& tasks) noexcept {
            return { tasks };
        }

        template<typename T>        //called by co_await for tasks or functions, or std::pmr::vector thereof
        awaitable_task<T> await_transform(T& task) noexcept {
            return { task };
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
        * If the parent was a Job, then if the task<T> is still alive, the coro will suspend,
        * and the task<T> must destroy the promise in its destructor. If the task<T> has destructed,
        * then the coro must destroy the promise itself by resuming the final awaiter.
        */
        template<typename U>
        struct final_awaiter : public awaiter_base {
            final_awaiter() noexcept {}

            bool await_suspend(std::experimental::coroutine_handle<task_promise<U>> h) noexcept { //called after suspending
                auto& promise = h.promise();
                if (promise.m_parent) {                      //if there is a parent
                    promise.m_parent->child_finished();      //tell parent that this child has finished

                    if (promise.m_parent->is_job()) {        //if the parent is a job
                        int count = promise.m_count.fetch_sub(1);
                        if (count == 1 ) {      //if the task<T> has been destroyed then no one is waiting
                            return false;       //so destroy the promise
                        }
                    }
                }
                return true;
            }
        };

        final_awaiter<T> final_suspend() noexcept { //create the final awaiter at the final suspension point
            return {};
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
                        int count = m_coro.promise().m_count.fetch_sub(1);
                        if (count == 1) {       //if the coro is done, then destroy it
                            m_coro.destroy();
                        }
                    }
                }
            }
        }

        T get() noexcept {                  //retrieve the promised value
            return m_coro.promise().get();
        }

        task_promise_base* promise() noexcept { //get a pointer to the promise (can be used as Job)
            return &m_coro.promise();
        }

        void thread_index(uint32_t ti) {        //force the task to rn on thread ti
            m_coro.promise().m_thread_index = ti;
        }

        void type(int32_t type) {        //set type
            m_coro.promise().m_type = type;
        }

        void id(int32_t id) {        //set id
            m_coro.promise().m_id = id;
        }

        bool resume() noexcept {    //resume the task by calling resume() on the handle
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}
    };

}

