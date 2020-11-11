#ifndef VECORO_H
#define VECORO_h



#include <iostream>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <vector>
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



namespace vgjs {

    //---------------------------------------------------------------------------------------------------
    //declaration of all classes and structs 

    //struct for deallocating coroutine promises
    template<typename T> struct coro_deallocator;
    template<> struct coro_deallocator<void>;
    
    //awaitables for co_await and co_yield, and final awaiting
    template<typename PT, typename... Ts> struct awaitable_tuple; //co_await a tuple of vectors
    template<typename PT, typename T> struct awaitable_coro; //co_await coros, functions, vectors
    template<typename PT> struct awaitable_resume_on; //change the thread
    template<typename U> struct yield_awaiter;  //co_yield
    template<typename U> struct final_awaiter;  //final_suspend

    //coroutine promise classes
    class Coro_promise_base;                    //common base class independent of return type T
    template<typename T> class Coro_promise;    //main promise class for all Ts
    template<> class Coro_promise<void>;        //specializiation for T = void

    //coroutine future classes
    class Coro_base;                    //common base class independent of return type T
    template<typename T> class Coro;    //main promise class for all Ts
    template<> class Coro<void>;        //specializiation for T = void

    //---------------------------------------------------------------------------------------------------

    //test whether a template parameter T is a std::pmr::vector
    template<typename>
    struct is_pmr_vector : std::false_type {};

    template<typename T>
    struct is_pmr_vector<std::pmr::vector<T>> : std::true_type {};

    //---------------------------------------------------------------------------------------------------
    //schedule functions for coroutines

    template<typename T>
    concept CORO = std::is_base_of<Coro_base, T>::value; //resolve only for coroutines

    /**
    * \brief Schedule a Coro into the job system.
    * Basic function for scheduling a coroutine Coro into the job system.
    * \param[in] coro A coroutine Coro, whose promise is a job that is scheduled into the job system
    * \param[in] parent The parent of this Job.
    * \param[in] children Number used to increase the number of children of the parent.
    */
    template<typename T>
    requires CORO<T>   
    void schedule( T& coro, Job_base* parent = current_job(), int32_t children = 1) noexcept {
        if (parent != nullptr) {
            parent->m_children.fetch_add((int)children);       //await the completion of all children      
        }
        coro.promise()->m_parent = parent;
        JobSystem::instance().schedule(coro.promise() );      //schedule the promise as job
    };

    /**
    * \brief Schedule a Coro into the job system.
    * Basic function for scheduling a coroutine Coro into the job system.
    * \param[in] coro A coroutine Coro, whose promise is a job that is scheduled into the job system.
    * \param[in] parent The parent of this Job.
    * \param[in] children Number used to increase the number of children of the parent.
    */
    template<typename T>
    requires CORO<T>
    void schedule( T&& coro, Job_base* parent = current_job(), int32_t children = 1) noexcept {
        schedule(coro, parent, children);
    };


    //---------------------------------------------------------------------------------------------------
    //Deallocators

    /**
    * \brief Deallocator is used to destroy coro promises when the system is shut down.
    */
    template<typename T>
    struct coro_deallocator : public job_deallocator {
        inline void deallocate(Job_base* job) noexcept;
    };

    /**
    * \brief Deallocator spezialization for void
    */
    template<>
    struct coro_deallocator<void> : public job_deallocator {
        inline void deallocate(Job_base* job) noexcept;
    };


    //---------------------------------------------------------------------------------------------------
    //Awaitables


    /**
    * \brief Awaitable for awaiting a tuple of vector of type Coro<T>, Function{}, std::function<void(void)>.
    *
    * The tuple can contain vectors with different types.
    * The caller will then await the completion of the Coros. Afterwards,
    * the return values can be retrieved by calling get().
    */

    using suspend_always = n_exp::suspend_always;

    template<typename PT, typename... Ts>
    struct awaitable_tuple {
        struct awaiter : suspend_always {
            std::tuple<std::pmr::vector<Ts>...>& m_tuple;       //vector with all children to start
            std::size_t                          m_number = 0;   //total number of all new children to schedule

            /**
            * \brief Count the jobs in the vectors. Return false if there are no jobs, else true.
            */
            bool await_ready() noexcept {                                 //suspend only if there are no Coros
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::initializer_list<int>{ (m_number += std::get<Idx>(m_tuple).size(), 0) ...}; //called for every tuple element
                    return (m_number == 0);
                };
                return f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            /**
            * \brief Go through all tuple elements and schedule them.
            * Presets number of new children to avoid a race.
            * \param[in] h The coro handle, can be used to get the promise which is the parent of the children.
            */
            void await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
                auto g = [&, this]<typename T>(std::pmr::vector<T> & vec) {
                    schedule(vec, &h.promise(), (int)m_number);    //in first call the number of children is the total number of all jobs
                    m_number = 0;                               //after this always 0
                };

                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::initializer_list<int>{ (g(std::get<Idx>(m_tuple)), 0) ...}; //called for every tuple element
                };

                f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            awaiter(std::tuple<std::pmr::vector<Ts>...>& children) noexcept : m_tuple(children) {};
        };

        std::tuple<std::pmr::vector<Ts>...>& m_tuple;              //vector with all children to start

        awaitable_tuple(std::tuple<std::pmr::vector<Ts>...>& children) noexcept : m_tuple(children) {};

        //co_await operator is defined for this awaitable, and results in the awaiter
        awaiter operator co_await() noexcept { return { m_tuple }; };
    };


    /**
    * \brief Awaiter for awaiting a Coro of type Coro<T> or std::function<void(void)>, or std::pmr::vector thereof
    *
    * The caller will await the completion of the Coro(s). Afterwards,
    * the return values can be retrieved by calling get() for Coro<t>.
    */
    template<typename PT, typename T>
    struct awaitable_coro {
        struct awaiter : suspend_always {
            T& m_child;                      //child/children

            /**
            * \brief If m_child is a vector this makes sure that there are children in the vector
            */
            bool await_ready() noexcept {                   //suspend only if there are children to create
                if constexpr (is_pmr_vector<T>::value) {
                    return m_child.empty();
                }
                return false;
            }

            /**
            * \brief Forward the child to the correct version of schedule()
            * \param[in] h The coro handle, can be used to get the promise which is the parent of the children.
            */
            void await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
                schedule(std::forward<T>(m_child), &h.promise());  //schedule the coro, function or vector
            }

            awaiter(T& child) noexcept : m_child(child) {};
        };

        T& m_child;                     //child/children

        awaitable_coro(T& child) noexcept : m_child(child) {};

        //co_await operator is defined for this awaitable, and results in the awaiter
        awaiter operator co_await() noexcept { return { m_child }; };
    };


    /**
    * \brief Awaiter for changing the thread that the coro is run on.
    * After suspending the thread number is set to the target thread, then the job
    * is immediately rescheduled into the system
    */
    template<typename PT>
    struct awaitable_resume_on {
        struct awaiter : suspend_always {
            int32_t m_thread_index; //the thread index to use

            /**
            * \brief Test whether the job is already on the right thread.
            */
            bool await_ready() noexcept {   //do not go on with suspension if the job is already on the right thread
                return (m_thread_index == JobSystem::instance().thread_index());
            }

            /**
            * \brief Set the thread index and reschedule the coro
            * \param[in] h The coro handle, can be used to get the promise.
            */
            void await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
                h.promise().m_thread_index = m_thread_index;
                JobSystem::instance().schedule(&h.promise());
            }
            awaiter(int32_t thread_index) noexcept : m_thread_index(thread_index) {};
        };

        int32_t m_thread_index; //the thread index to use

        awaitable_resume_on(int32_t thread_index) noexcept : m_thread_index(thread_index) {};

        //co_await operator is defined for this awaitable, and results in the awaiter
        awaiter operator co_await() noexcept { return { m_thread_index }; };
    };


    /**
    * \brief When a coroutine calls co_yield this awaiter calls its parent.
    *
    * A coroutine calling co_yield suspends with this awaiter. The awaiter is similar
    * to the final awaiter, but always suspends.
    */
    template<typename U>
    struct yield_awaiter : public suspend_always {
        /**
        * \brief After suspension, call parent to run it as continuation
        * \param[in] h Handle of the coro, is used to get the promise (=Job)
        */
        void await_suspend(n_exp::coroutine_handle<Coro_promise<U>> h) noexcept { //called after suspending
            auto& promise = h.promise();

            if (promise.m_parent != nullptr) {          //if there is a parent
                if (promise.m_is_parent_function) {       //if it is a Job
                    JobSystem::instance().child_finished((Job*)promise.m_parent); //indicate that this child has finished
                }
                else {  //parent is a coro
                    uint32_t num = promise.m_parent->m_children.fetch_sub(1);   //one less child
                    if (num == 1) {                                             //was it the last child?
                        JobSystem::instance().schedule(promise.m_parent);      //if last reschedule the parent coro
                    }
                }
            }
            return;
        }
    };


    /**
    * \brief When a coroutine reaches its end, it may suspend a last time using such a final awaiter
    *
    * Suspending as last act prevents the promise to be destroyed. This way the caller
    * can retrieve the stored value by calling get(). Also we want to resume the parent
    * if all children have finished their Coros.
    * If the parent was a Job, then if the Coro<T> is still alive, the coro will suspend,
    * and the Coro<T> must destroy the promise in its destructor. If the Coro<T> has destructed,
    * then the coro must destroy the promise itself by resuming the final awaiter.
    */
    template<typename U>
    struct final_awaiter : public suspend_always {
        /**
        * \brief After suspension, call parent to run it as continuation
        * \param[in] h Handle of the coro, is used to get the promise (=Job)
        */
        bool await_suspend(n_exp::coroutine_handle<Coro_promise<U>> h) noexcept { //called after suspending
            auto& promise = h.promise();
            bool is_parent_function = promise.m_is_parent_function;
            auto parent = promise.m_parent;

            if (parent != nullptr) {          //if there is a parent
                if (is_parent_function) {       //if it is a Job
                    JobSystem::instance().child_finished((Job*)parent);//indicate that this child has finished
                }
                else {
                    uint32_t num = parent->m_children.fetch_sub(1);        //one less child
                    if (num == 1) {                                             //was it the last child?
                        JobSystem::instance().schedule(parent);      //if last reschedule the parent coro
                    }
                }
            }
            return !is_parent_function; //if parent is coro, then you are in sync -> the future will destroy the promise
        }
    };


    //---------------------------------------------------------------------------------------------------
    //The coro promise classes

    /**
    * \brief Base class of coroutine Coro_promise, derived from Job_base so it can be scheduled.
    *
    * The base promise class derives from Job_base so it can be scheduled on the job system!
    * The base does not depend on any type information that is stored with the promise.
    * It defines default behavior and the operators for allocating and deallocating memory
    * for the promise.
    */
    class Coro_promise_base : public Job_base {
        template<typename T> friend struct coro_deallocator;

    protected:
        n_exp::coroutine_handle<> m_coro;
        bool m_is_parent_function = current_job() == nullptr ? true : current_job()->is_function();
        bool* m_ready_ptr = nullptr; //points to flag which is true if value is ready, else false

    public:
        explicit         Coro_promise_base(n_exp::coroutine_handle<> coro) noexcept : m_coro(coro) {};
        void             unhandled_exception() noexcept { std::terminate(); };
        suspend_always   initial_suspend() noexcept { return {}; };

        /**
        * \brief Resume the Coro at its suspension point.
        */
        bool resume() noexcept {
            if (m_is_parent_function && m_ready_ptr != nullptr) {
                *m_ready_ptr = false;   //invalidate return value
            }

            if (m_coro && !m_coro.done()) {
                m_coro.resume();       //coro could destroy itself here!!
            }
            return true;
        };

        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept;

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept;

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) noexcept;

        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) noexcept;

        void operator delete(void* ptr, std::size_t sz) noexcept;
    };


    /**
    * \brief Promise of the Coro. Depends on the return type.
    *
    * The Coro promise can hold values that are produced by the Coro. They can be
    * retrieved later by calling get() on the Coro (which calls get() on the promise)
    */
    template<typename T = void>
    class Coro_promise : public Coro_promise_base {
        template<typename F> friend struct yield_awaiter;
        template<typename F> friend struct final_awaiter;
        template<typename F> friend class Coro;

    protected:
        std::shared_ptr<std::pair<bool, T>> m_value_ptr;  //a shared view of the return value
        std::pair<bool, T>                  m_value;

    public:

        /**
        * \brief Constructor.
        */
        Coro_promise() noexcept
            : Coro_promise_base{ n_exp::coroutine_handle<Coro_promise<T>>::from_promise(*this) } {
        };

        static Coro<T>  get_return_object_on_allocation_failure();
        job_deallocator get_deallocator() noexcept { return coro_deallocator<T>{}; };    //called for deallocation
        Coro<T>         get_return_object() noexcept;

        /**
        * \brief Store the value returned by co_return.
        * \param[in] t The value that was returned.
        */
        void return_value(T t) noexcept {   //is called by co_return <VAL>, saves <VAL> in m_value
            if (m_is_parent_function) {
                *m_value_ptr = std::make_pair(true, t);
                return;
            }
            m_value = std::make_pair(true, t);
        }

        /**
        * \brief Store the value returned by co_yield.
        * \param[in] t The value that was yielded
        * \returns a yield_awaiter
        */
        yield_awaiter<T> yield_value(T t) noexcept {
            if (m_is_parent_function) {
                *m_value_ptr = std::make_pair(true, t);
            }
            else {
                m_value = std::make_pair(true, t);
            }
            return {};  //return a yield_awaiter
        }

        template<typename... Ts>
        awaitable_tuple<T, Ts...> await_transform(std::tuple<std::pmr::vector<Ts>...>& tuple) noexcept { return { tuple }; };

        template<typename U>
        awaitable_coro<T, U>      await_transform(U&& coro) noexcept { return { coro }; };

        awaitable_resume_on<T>    await_transform(int thread_index) noexcept { return { (int32_t)thread_index}; };

        final_awaiter<T>          final_suspend() noexcept { return {}; };
    };


    //---------------------------------------------------------------------------------------------------
    //the coro future classes


    /**
    * \brief Base class of coroutine future Coro. Independent of promise return type.
    */
    class Coro_base : public Queuable {
    protected:
        Coro_promise_base* m_promise = nullptr;

    public:
        explicit Coro_base() noexcept {};
        explicit Coro_base(Coro_promise_base* promise) noexcept : Queuable(), m_promise(promise) {}         //constructor
        bool resume() noexcept { return m_promise->resume(); };         //resume the Coro
        Coro_promise_base* promise() noexcept { return m_promise; };   //get the promise to use it as Job 
    };


    /**
    * \brief The main Coro future class. Can be constructed to return any value type
    *
    * The Coro is an accessor class much like a std::future that is used to
    * access the promised value once it is awailable.
    * It also holds a handle to the Coro promise.
    */
    template<typename T = void >
    class Coro : public Coro_base {
    public:
        using promise_type = Coro_promise<T>;
        bool m_is_parent_function;
        std::shared_ptr<std::pair<bool, T>> m_value_ptr;

    private:
        n_exp::coroutine_handle<promise_type> m_coro;   //handle to Coro promise

    public:
        /**
        * \brief Coro future constructor
        */
        Coro() noexcept : Coro_base() {};

        /**
        * \brief Coro future constructor
        */
        Coro(   n_exp::coroutine_handle<promise_type> h
                , std::shared_ptr<std::pair<bool, T>>& value_ptr
                , bool is_parent_function) noexcept
            : Coro_base(&h.promise()), m_coro(h), m_is_parent_function(is_parent_function) {
            if (m_is_parent_function) {
                m_value_ptr = value_ptr;
            }
        };

        /**
        * \brief Coro future constructor
        */
        Coro(Coro<T>&& t)  noexcept : Coro_base(t.m_promise), m_coro(std::exchange(t.m_coro, {})),
                                        m_value_ptr(std::exchange(t.m_value_ptr, {})),
                                        m_is_parent_function(std::exchange(t.m_is_parent_function, {})) {};

        /**
        * \brief Assignment operator
        */
        void operator= (Coro<T>&& t) noexcept {
            std::swap(m_coro, t.m_coro);
            std::swap(m_value_ptr, t.m_value_ptr);
        }

        /**
        * \brief Destructor of the Coro promise.
        */
        ~Coro() noexcept {
            if (!m_is_parent_function && m_coro) {
                m_coro.destroy();
            }
        }

        /**
        * \brief Retrieve the promised value or std::nullopt - nonblocking
        * \returns the promised value or std::nullopt
        */
        std::pair<bool, T> get() noexcept {
            if (m_is_parent_function) {
                return *m_value_ptr;
            }
            return m_coro.promise().m_value;
        }

        /**
        * \brief Function operator so you can pass on parameters to the Coro.
        *
        * \param[in] thread_index The thread that should execute this coro
        * \param[in] type The type of the coro.
        * \param[in] id A unique ID of the call.
        * \returns a reference to this Coro so that it can be used with co_await.
        */
        Coro<T>&& operator() (int32_t thread_index, int32_t type, int32_t id) {
            m_promise->m_thread_index = thread_index;
            m_promise->m_type = type;
            m_promise->m_id = id;
            return std::move(*this);
        }
    };


    //---------------------------------------------------------------------------------------------------
    //specializations for void

    /**
    * \brief When a coroutine calls co_yield this awaiter calls its parent.
    */
    template<>
    struct yield_awaiter<void> : public suspend_always {
        void await_suspend(n_exp::coroutine_handle<Coro_promise<void>> h) noexcept;
    };

    /**
    * \brief When a coroutine reaches its end, it may suspend a last time using such a final awaiter
    */
    template<>
    struct final_awaiter<void> : public n_exp::suspend_always {
        bool await_suspend(n_exp::coroutine_handle<Coro_promise<void>> h) noexcept;
    };


    /**
    * \brief Coro promise for void
    */
    template<>
    class Coro_promise<void> : public Coro_promise_base {
        friend struct yield_awaiter<void>;
        friend struct final_awaiter<void>;
        friend class Coro<void>;

    public:
        /**
        * \brief Constructor.
        */
        Coro_promise() noexcept
            : Coro_promise_base{ n_exp::coroutine_handle<Coro_promise<void>>::from_promise(*this) } {
        };

        static Coro<>   get_return_object_on_allocation_failure();
        job_deallocator get_deallocator() noexcept { return coro_deallocator<void>{}; };    //called for deallocation
        Coro<void>      get_return_object() noexcept;
        void            return_void() noexcept {};
        yield_awaiter<void> yield_value() noexcept { return {}; };

        template<typename... Ts>
        awaitable_tuple<void, Ts...> await_transform(std::tuple<std::pmr::vector<Ts>...>& tuple) noexcept { return { tuple }; };

        template<typename U>
        awaitable_coro<void, U>      await_transform(U&& coro) noexcept { return { coro }; };

        awaitable_resume_on<void>    await_transform(int thread_index) noexcept { return (int32_t)thread_index; };

        final_awaiter<void>          final_suspend() noexcept { return {}; };
    };


    /**
    * \brief Coro future for void
    */
    template<>
    class Coro<void> : public Coro_base {
    protected:
        bool m_is_parent_function;

    public:
        using promise_type = Coro_promise<void>;

    private:
        n_exp::coroutine_handle<promise_type> m_coro;   //handle to Coro promise

    public:
        Coro() noexcept : Coro_base() {};

        Coro(n_exp::coroutine_handle<promise_type> coro, bool is_parent_function) noexcept 
            : Coro_base(&coro.promise()), m_is_parent_function(is_parent_function), m_coro(coro) {};

        Coro(Coro<void>&& t)  noexcept : Coro_base(t.m_promise), m_coro(std::exchange(t.m_coro, {})) {};

        void operator= (Coro<void>&& t) noexcept { std::swap(m_coro, t.m_coro); };

        ~Coro() noexcept {
            if (!m_is_parent_function && m_coro) {
                m_coro.destroy();
            }
        }

        /**
        * \brief Function operator so you can pass on parameters to the Coro.
        *
        * \param[in] thread_index The thread that should execute this coro
        * \param[in] type The type of the coro.
        * \param[in] id A unique ID of the call.
        * \returns a reference to this Coro so that it can be used with co_await.
        */
        Coro<void>&& operator() (int32_t thread_index, int32_t type, int32_t id) {
            m_promise->m_thread_index = thread_index;
            m_promise->m_type = type;
            m_promise->m_id = id;
            return std::move(*this);
        }
    };



    //---------------------------------------------------------------------------------------------------
    //Implementations that need definition of other classes



    //---------------------------------------------------------------------------------------------------
    //deallocators


    /**
    * \brief This deallocator is used to destroy coro promises when the system is shut down.
    */
    template<typename T>
    inline void coro_deallocator<T>::deallocate(Job_base* job) noexcept {    //called when the job system is destroyed
        auto coro_promise = (Coro_promise<T>*)job;
        auto coro = n_exp::coroutine_handle<Coro_promise<T>>::from_promise(*coro_promise);
        if (coro) {
            coro.destroy();
        }
    };

    /**
    * \brief This deallocator is used to destroy coro promises when the system is shut down.
    */
    inline void coro_deallocator<void>::deallocate(Job_base* job) noexcept {    //called when the job system is destroyed
        auto coro_promise = (Coro_promise<void>*)job;
        if (coro_promise->m_coro) {
            coro_promise->m_coro.destroy();
        }
    };


    //--------------------------------------------------------------------------------------------------
    //awaitables


    /**
    * \brief After suspension, call parent to run it as continuation
    * \param[in] h Handle of the coro, is used to get the promise (=Job)
    */
    inline void yield_awaiter<void>::await_suspend(n_exp::coroutine_handle<Coro_promise<void>> h) noexcept { //called after suspending
        Coro_promise<void>& promise = h.promise();

        if (promise.m_parent != nullptr) {          //if there is a parent
            if (promise.m_is_parent_function) {       //if it is a Job
                JobSystem::instance().child_finished((Job*)promise.m_parent); //indicate that this child has finished
            }
            else {  //parent is a coro
                uint32_t num = promise.m_parent->m_children.fetch_sub(1);   //one less child
                if (num == 1) {                                             //was it the last child?
                    JobSystem::instance().schedule(promise.m_parent);      //if last reschedule the parent coro
                }
            }
        }
        return;
    }


    /**
    * \brief After suspension, call parent to run it as continuation
    * \param[in] h Handle of the coro, is used to get the promise (=Job)
    */
    inline bool final_awaiter<void>::await_suspend(n_exp::coroutine_handle<Coro_promise<void>> h) noexcept { //called after suspending
        Coro_promise<void>& promise = h.promise();
        bool is_parent_function = promise.m_is_parent_function;
        auto parent = promise.m_parent;

        if (parent != nullptr) {          //if there is a parent
            if (is_parent_function) {       //if it is a Job
                JobSystem::instance().child_finished((Job*)promise.m_parent);//indicate that this child has finished
            }
            else {
                uint32_t num = parent->m_children.fetch_sub(1);        //one less child
                if (num == 1) {                                             //was it the last child?
                    JobSystem::instance().schedule(parent);      //if last reschedule the parent coro
                }
            }
        }
        return !is_parent_function; //if parent is coro, then you are in sync -> the future will destroy the promise
    }


    //---------------------------------------------------------------------------------------------------
    //Coro_promise_base


    /**
    * \brief Use the given memory resource to create the promise object for a normal function.
    *
    * Store the pointer to the memory resource right after the promise, so it can be used later
    * for deallocating the promise.
    *
    * \param[in] sz Number of bytes to allocate.
    * \param[in] std::allocator_arg_t Dummy parameter to indicate that the next parameter is the memory resource to use.
    * \param[in] mr The memory resource to use when allocating the promise.
    * \param[in] args the rest of the coro args.
    * \returns a pointer to the newly allocated promise.
    */
    template<typename... Args>
    inline void* Coro_promise_base::operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
        //std::cout << "Coro new " << sz << "\n";
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
    *
    * \param[in] sz Number of bytes to allocate.
    * \param[in] Class The class that defines this member function.
    * \param[in] std::allocator_arg_t Dummy parameter to indicate that the next parameter is the memory resource to use.
    * \param[in] mr The memory resource to use when allocating the promise.
    * \param[in] args the rest of the coro args.
    * \returns a pointer to the newly allocated promise.
    */
    template<typename Class, typename... Args>
    inline void* Coro_promise_base::operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
        return operator new(sz, std::allocator_arg, mr, args...);
    }

    /**
    * \brief Create a promise object for a class member function using the system standard allocator.
    * \param[in] sz Number of bytes to allocate.
    * \param[in] Class The class that defines this member function.
    * \param[in] args the rest of the coro args.
    * \returns a pointer to the newly allocated promise.
    */
    template<typename Class, typename... Args>
    inline void* Coro_promise_base::operator new(std::size_t sz, Class, Args&&... args) noexcept {
        return operator new(sz, std::allocator_arg, std::pmr::new_delete_resource(), args...);
    }

    /**
    * \brief Create a promise object using the system standard allocator.
    * \param[in] sz Number of bytes to allocate.
    * \param[in] args the rest of the coro args.
    * \returns a pointer to the newly allocated promise.
    */
    template<typename... Args>
    inline void* Coro_promise_base::operator new(std::size_t sz, Args&&... args) noexcept {
        return operator new(sz, std::allocator_arg, std::pmr::new_delete_resource(), args...);
    }

    /**
    * \brief Use the pointer after the promise as deallocator.
    * \param[in] ptr Pointer to the memory to deallocate.
    * \param[in] sz Number of bytes to deallocate.
    */
    inline void Coro_promise_base::operator delete(void* ptr, std::size_t sz) noexcept {
        //std::cout << "Coro delete " << sz << "\n";
        auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
        auto allocator = (std::pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
        (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
    }

    //---------------------------------------------------------------------------------------------------
    //Coro_promise<T>

    /**
    * \brief Define what happens if memory allocation fails
    * \returns the Coro<T> from the promise.
    */
    template<typename T>
    inline Coro<T> Coro_promise<T>::get_return_object_on_allocation_failure() {
        return Coro<T>();
    }

    /**
    * \brief Get Coro<T> from the Coro_promise<T>. Creates a shared value to trade the result.
    * \returns the Coro<T> from the promise.
    */
    template<typename T>
    inline Coro<T> Coro_promise<T>::get_return_object() noexcept {
        m_ready_ptr = &m_value.first;
        if (m_is_parent_function) {
            m_value_ptr = std::make_shared<std::pair<bool, T>>(std::make_pair(false, T{}));
            m_ready_ptr = &(m_value_ptr->first);
        }

        return Coro<T>{
            n_exp::coroutine_handle<Coro_promise<T>>::from_promise(*this),
                m_value_ptr, m_is_parent_function };
    }

    //---------------------------------------------------------------------------------------------------
    //Coro_promise<void>

    /**
    * \brief Define what happens if memory allocation fails
    * \returns the Coro<T> from the promise.
    */
    inline Coro<void> Coro_promise<void>::get_return_object_on_allocation_failure() {
        return Coro<void>();
    }

    /**
    * \brief Get Coro<void> from the Coro_promise<T>. Creates a shared value to trade the result.
    * \returns the Coro<void> from the promise.
    */
    inline Coro<void> Coro_promise<void>::get_return_object() noexcept {
        return Coro<void>{n_exp::coroutine_handle<Coro_promise<void>>::from_promise(*this), m_is_parent_function };
    }

}


#endif
