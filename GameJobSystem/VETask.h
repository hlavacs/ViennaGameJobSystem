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

    /**
    * \brief A custom deleter using a given memory resource.
    *
    * This deleter is used together with a unique ptr that was allocated using a memory resource.
    * When the unique ptr goes out of scope, the deleter will deallocate its memory
    * using the used memory resource.
    */
    template<typename U>
    struct deleter {
        std::pmr::memory_resource* m_mr;    //memory resource

        deleter(std::pmr::memory_resource* mr) : m_mr(mr) {}    //constructor

        void operator()(U* b) {                                 //called for deletion
            std::pmr::polymorphic_allocator<U> allocator(m_mr); //construct a polymorphic allocator
            allocator.deallocate(b, 1);                         //use it to delete the pointer
        }
    };

    /**
    * \brief Creates a unique ptr using a given memory resource.
    *
    * This function uses a given memory resource to create a unique pointer.
    * The unique ptr owns an object and will automatically delete it.
    * The appropriate deleter is also stored with the unique ptr.
    */
    template<typename T, typename... ARGS>
    auto make_unique_ptr(std::pmr::memory_resource* mr, ARGS&&... args) {
        std::pmr::polymorphic_allocator<T> allocator(mr);   //create a polymorphic allocator for allocation and construction
        T* p = allocator.allocate(1);                       //allocate the object
        new (p) T(std::forward<ARGS>(args)...);             //call constructor
        return std::unique_ptr<T, deleter<T>>(p, mr);       //return the unique ptr holding the object and deleter
    }

    //define a vector owning tasks (using unique ptrs)
    template<typename T>
    using unique_ptr_vector = std::pmr::vector<std::unique_ptr<T, deleter<T>>>;

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Schedule a task promise into the job system
    *
    * Basic function for scheduling a coroutine task into the job system
    * \param[in] task A coroutine task, whose promise is a job that is scheduled into the job system
    * \param[in] thread_index Optional thread index to run the task
    */
    template<typename T>
    requires (std::is_base_of<task_base, T>::value)
    void schedule(T& task) noexcept {
        JobSystem::instance()->schedule(task.promise());
        return;
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

        void child_finished() noexcept {
            if (m_children.fetch_sub(1) == 1) {
                JobSystem::instance()->schedule(this);    //schedule the promise as job
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
        virtual bool resume() { return true; };                     //resume the task
        virtual task_promise_base* promise() { return nullptr; };   //get the promise to use it as Job 
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
    * \brief Awaiter for awaiting a vector of tasks (task_base*)
    * 
    * The vector must contain pointers pointing to tasks to be run as jobs.
    * The caller will then await the completion of the tasks. Afterwards,
    * the return values can be retrieved by calling get().
    */
    struct awaitable_vector {

        struct awaiter : awaiter_base {
            task_promise_base* m_promise;                       //caller of the co_await (Job and promise at the same time)
            std::pmr::vector<task_base*>& m_children_vector;    //vector with all children to start

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_children.store( (uint32_t)m_children_vector.size()  ); //await the completion of all children
                for (auto& ptr : m_children_vector) {                   //loop over all children
                    ptr->promise()->m_parent = m_promise;               //remember parent
                    JobSystem::instance()->schedule(ptr->promise());    //schedule the promise as job
                }
            }

            awaiter(task_promise_base* promise, std::pmr::vector<task_base*>& children) noexcept 
                : m_promise(promise), m_children_vector(children) {};
        };

        task_promise_base* m_promise;                       //caller of the co_await
        std::pmr::vector<task_base*>& m_children_vector;    //vector with all children to start

        awaitable_vector(task_promise_base* promise, std::pmr::vector<task_base*>& children) noexcept 
            : m_promise(promise), m_children_vector(children) {};

        awaiter operator co_await() noexcept { return { m_promise, m_children_vector }; };
    };

    /**
    * \brief Awaiter for awaiting a vector of tasks (unique_ptr)
    *
    * The vector must contain pointers pointing to tasks to be run as jobs.
    * The caller will then await the completion of the tasks. Afterwards,
    * the return values can be retrieved by calling get().
    */
    template<typename T>
    struct awaitable_vector_unique {

        template<typename U>
        struct awaiter : awaiter_base {
            task_promise_base* m_promise;
            unique_ptr_vector<U>& m_children_vector;

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_children.store((uint32_t)m_children_vector.size());
                for (auto& ptr : m_children_vector) {
                    ptr->promise()->m_parent = m_promise;
                    JobSystem::instance()->schedule(ptr->promise());
                }
            }

            awaiter(task_promise_base* promise, unique_ptr_vector<T>& children) noexcept
                : m_promise(promise), m_children_vector(children) {};
        };

        task_promise_base* m_promise;
        unique_ptr_vector<T>& m_children_vector;

        awaitable_vector_unique(task_promise_base* promise, unique_ptr_vector<T>& children) noexcept
            : m_promise(promise), m_children_vector(children) {};

        awaiter<T> operator co_await() noexcept { return { m_promise, m_children_vector }; };
    };


    /**
    * \brief Awaiter for changing the thread that the job is run on
    */
    struct awaitable_resume_on {
        struct awaiter : awaiter_base {
            task_promise_base*  m_promise;
            uint32_t            m_thread_index;

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_thread_index = m_thread_index;
                JobSystem::instance()->schedule(m_promise);
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

        template<typename T>    //called by co_await unique_ptr_vector<T>& tasks, creates the correct awaitable
        awaitable_vector_unique<T> await_transform(unique_ptr_vector<T>& tasks) noexcept {
            return { this, tasks };
        }

        //called by co_await std::pmr::vector<task_base*>& tasks, creates the correct awaitable
        awaitable_vector await_transform( std::pmr::vector<task_base*>& tasks) noexcept {
            return { this, tasks };
        }

        awaitable_resume_on await_transform(uint32_t thread_index) noexcept { //called by co_await INT, for changing the thread
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
                if (m_promise->m_parent) {                                      //if there is a parent
                    if (m_promise->m_parent->m_children.fetch_sub(1) == 1) {    //have all children finished yet?
                        JobSystem::instance()->schedule(m_promise->m_parent);   //yes -> schedule the parent to resuming
                    }
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
            if (m_coro && !m_coro.done())   //use done() only if coro suspended
                m_coro.destroy();           //if you do not want this then move task
        }

        T get() noexcept {                  //retrieve the promised value
            return m_coro.promise().get();
        }

        task_promise_base* promise() noexcept { //get a pointer to the promise (can be used as Job)
            return &m_coro.promise();
        }

        bool resume() noexcept {    //resume the task by calling resume() on the handle
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}
    };

}

