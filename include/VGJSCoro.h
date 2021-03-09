#ifndef VECORO_H
#define VECORO_H



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



namespace vgjs {

    //---------------------------------------------------------------------------------------------------
    //declaration of all classes and structs 

    //struct for deallocating coroutine promises
    template<typename T> struct coro_deallocator;
    template<> struct coro_deallocator<void>;
    
    //awaitables for co_await and co_yield, and final awaiting
    template<typename PT, typename... Ts> struct awaitable_tuple;
    template<typename PT> struct awaitable_resume_on; //change the thread
    template<typename PT> struct awaitable_tag; //schedule all jobs for a tag
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
    //schedule functions for coroutines

    template<typename T>
    concept CORO = std::is_base_of_v<Coro_base, std::decay_t<T> >; //resolve only for coroutines

    /**
    * \brief Schedule a Coro into the job system.
    * Basic function for scheduling a coroutine Coro into the job system.
    * \param[in] coro A ref to coroutine Coro, whose promise is a job that is scheduled into the job system
    * \param[in] parent The parent of this Job.
    * \param[in] children Number used to increase the number of children of the parent.
    */
    template<typename T>
    requires CORO<T>   
    uint32_t schedule( T&& coro, tag_t tg = tag_t{}, Job_base* parent = current_job(), int32_t children = 1) noexcept {
        JobSystem js;

        auto promise = coro.promise();

        promise->m_parent = parent;
        if (tg.value < 0 ) {           //schedule now
            if (parent != nullptr) {
                parent->m_children.fetch_add((int)children);       //await the completion of all children      
            }
        }
        else {                                  //schedule for future tag - the promise will not be available then
            promise->set_self_destruct(true);
            promise->m_parent = nullptr;
        }
        js.schedule_job( promise, tg );      //schedule the promise as job
        return 1;
    };


    template<typename T>
    requires CORO<T>
    void continuation(T&& coro) noexcept {
        Job_base* current = current_job();
        if (current == nullptr || !current->is_function()) {
            return;
        }
        ((Job*)current)->m_continuation = coro.promise();
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
    * \brief This can be called as co_await parameter. It constructs a tuple
    * holding only references to the arguments. The arguments are passed into a
    * function get_ref, which SFINAEs to either a lambda version or for any other parameter.
    *
    * \param[in] args Arguments to be put into tuple
    * \returns a tuple holding references to the arguments.
    *
    */
    template<typename... Ts>
    inline decltype(auto) parallel(Ts&&... args) {
        return std::tuple<Ts&&...>(std::forward<Ts>(args)...);
    }


    using suspend_always = n_exp::suspend_always;


    /**
    * \brief Awaitable for changing the thread that the coro is run on.
    * After suspending the thread number is set to the target thread, then the job
    * is immediately rescheduled into the system
    */
    template<typename PT>
    struct awaitable_resume_on : suspend_always {
        thread_index_t m_thread_index; //the thread index to use

        /**
        * \brief Test whether the job is already on the right thread.
        */
        bool await_ready() noexcept {   //do not go on with suspension if the job is already on the right thread
            return (m_thread_index == JobSystem().get_thread_index());
        }

        /**
        * \brief Set the thread index and reschedule the coro
        * \param[in] h The coro handle, can be used to get the promise.
        */
        void await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
            h.promise().m_thread_index = m_thread_index;
            JobSystem().schedule_job(&h.promise());
        }

        /**
        * \brief Awaiter constructor
        * \parameter[in] thread_index Number of the thread to migrate to
        */
        awaitable_resume_on(thread_index_t index) noexcept : m_thread_index(index) {};
    };


    /**
    * \brief Awaitable for scheduling a tag
    */
    template<typename PT>
    struct awaitable_tag : suspend_always {
        tag_t     m_tag;            //the tag to schedule
        uint32_t  m_number = 0;     //Number of scheduled jobs

        /**
        * \brief Test whether the given tag is valid.
        * \returns true if nothing is to be done, else false.
        */
        bool await_ready() noexcept {  //do nothing if the given tag is invalid
            return m_tag.value < 0; 
        }

        /**
        * \brief Schedule tag jobs. Resume immediately if there were no jobs.
        * \param[in] h The coro handle, can be used to get the promise.
        * \returns true of the coro should be suspended, else false.
        */
        bool await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
            m_number = JobSystem().schedule_tag(m_tag);
            return m_number > 0;     //if jobs were scheduled - await them
        }

        /**
        * \brief Returns the number of jobs that have been scheduled
        * \returns the number of jobs that have been scheduled
        */
        uint32_t await_resume() {
            return m_number;
        }

        /**
        * \brief Awaiter constructor
        * \parameter[in] tg The tag to schedule
        */
        awaitable_tag( tag_t tg) noexcept : m_tag(tg) {};
    };


    /**
    * \brief Awaitable for scheduling jobs.
    * All jobs are put into std::tuples.
    * If one of the elements is a valid tag the jobs are scheduled for this
    * tag and the coro is resumed immediately.
    */
    template<typename PT, typename... Ts>
    struct awaitable_tuple : suspend_always {
        tag_t               m_tag;          ///<The tag to schedule to
        std::tuple<Ts&&...> m_tuple;          ///<vector with all children to start
        std::size_t         m_number;         ///<total number of all new children to schedule

        /**
        * \brief Count the number of children to schedule.
        * \returns the number of children to schedule.
        */
        template<typename U>
        size_t size(U& children) {
            if constexpr (is_pmr_vector< std::decay_t<U> >::value) { //if this is a vector
                return children.size();
            }
            if constexpr (std::is_same_v<std::decay_t<U>, tag_t>) { //if this is a tag
                m_tag = children;
                return 0;
            }
            return 1;   //if this is a std::function, Function, or Coro
        };

        /**
        * \brief Count the number of new jobs, then return false to force suspension.
        * If nothings is to be done, then prevent suspension.
        * \returns true if nothing is to be done, else false
        */
        bool await_ready() noexcept {               //suspend only if there is something to do
            auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                m_number = (size(std::get<Idx>(m_tuple)) + ... + 0); //called for every tuple element
            };
            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_number == 0;   //nothing to be done -> do not suspend
        }

        /**
        * \brief Determine whether to stay in suspension or not.
        *
        * The coro should suspend if m_tag is -1
        * The coro should continue if m_tag>=0 
        *
        * \param[in] h The coro handle, can be used to get the promise.
        * \returns true if the coro suspends, or false if the coro should continue.
        *
        */
        bool await_suspend(n_exp::coroutine_handle<Coro_promise<PT>> h) noexcept {
            auto g = [&, this]<std::size_t Idx>() {

                using tt = decltype(m_tuple);
                using T = decltype(std::get<Idx>(std::forward<tt>(m_tuple)));
                decltype(auto) children = std::forward<T>(std::get<Idx>(std::forward<tt>(m_tuple)));

                if constexpr (std::is_same_v<std::decay_t<T>, tag_t> ) { //never schedule tags here
                    return;
                }
                else {
                    /*if constexpr (std::is_reference_v<T>) {
                        if constexpr (std::is_rvalue_reference_v<T>) {
                            int i = 1;
                        }
                        else {
                            int i = 2;
                        }
                    }
                    else {
                        int i = 3;
                    }*/

                    schedule(std::forward<T>(children), m_tag, &h.promise(), (int)m_number);   //in first call the number of children is the total number of all jobs
                    m_number = 0;                                               //after this always 0
                }
            };

            auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                ( g.template operator() <Idx> (), ...); //called for every tuple element
            };

            f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1

            return m_tag.value < 0; //if tag value < 0 then schedule now, so return true to suspend
        }

        /**
        * \brief Dummy for catching all functions that are not a coro.
        * These will not be in the result tuple!
        *
        * \param[in] t Any function other than Coro. 
        * \returns a tuple holding nothing.
        *
        */
        template<typename T>
        decltype(auto) get_val(T& t) {
            return std::make_tuple(); //ignored by std::tuple_cat
        }

        /**
        * \brief Collect the results and put them into a tuple
        *
        * \param[in] t The current coro promise
        * \returns a tuple holding the return value.
        *
        */
        template<typename T>
        requires (!std::is_void_v<T>)
        decltype(auto) get_val(Coro<T>& t) {
            return std::make_tuple(t.get());
        }

        /**
        * \brief Collect the results and put them into a tuple
        *
        * \param[in] t A vector of promises holding the values
        * \returns a tuple with a vector holding the return value.
        *
        */
        template<typename T>
        requires (!std::is_void_v<T>)
        decltype(auto) get_val( n_pmr::vector<Coro<T>>& vec) {
            n_pmr::vector<T> ret;
            ret.reserve(vec.size());
            for (auto& coro : vec) { ret.push_back(coro.get()); }
            return std::make_tuple(std::move(ret));
        }

        /**
        * \brief Return the results from the co_await
        * \returns the results from the co_await
        *
        */
        auto await_resume() {
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

        /**
        * \brief Awaiter constructor.
        * \parameter[in] tuple The tuple to schedule
        */
        awaitable_tuple(std::tuple<Ts&&...> tuple) noexcept : m_tag{}, m_number{0}, m_tuple(std::forward<std::tuple<Ts&&...>>(tuple)){};
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
                    JobSystem().child_finished((Job*)promise.m_parent); //indicate that this child has finished
                }
                else {  //parent is a coro
                    uint32_t num = promise.m_parent->m_children.fetch_sub(1);   //one less child
                    if (num == 1) {                                             //was it the last child?
                        JobSystem().schedule_job(promise.m_parent);      //if last reschedule the parent coro
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
        n_exp::coroutine_handle<> m_coro;   ///<handle of the coroutine
        bool m_is_parent_function = current_job() == nullptr ? true : current_job()->is_function(); ///<is the parent a Function or nullptr?
        bool* m_ready_ptr = nullptr;        ///<points to flag which is true if value is ready, else false
        bool m_self_destruct = false;

    public:
        /**
        * \brief Constructor
        * \param[in] coro The handle of the coroutine (typeless because the base class does not depend on types)
        */
        explicit Coro_promise_base(n_exp::coroutine_handle<> coro) noexcept : Job_base(), m_coro(coro) {};

        /**
        * \brief React to unhandled exceptions
        */
        void unhandled_exception() noexcept { std::terminate(); };

        /**
        * \brief Initially always suspend
        */
        suspend_always initial_suspend() noexcept { return {}; };

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

        void set_self_destruct(bool b = true) { m_self_destruct = b; }
        bool get_self_destruct() { return m_self_destruct; }

        //operators for allocating and deallocating memory, implementations follow later in this file
        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, n_pmr::memory_resource* mr, Args&&... args) noexcept;

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, n_pmr::memory_resource* mr, Args&&... args) noexcept;

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
        std::shared_ptr<std::pair<bool, T>> m_value_ptr;    ///<a shared view of the return value, use if parent is a function
        std::pair<bool, T>                  m_value;        ///<a local storage of the value, use if parent is a coroutine

    public:

        /**
        * \brief Constructor.
        */
        Coro_promise() noexcept
            : Coro_promise_base{ n_exp::coroutine_handle<Coro_promise<T>>::from_promise(*this) } {
        };

        static Coro<T>  get_return_object_on_allocation_failure();
        Coro<T>         get_return_object() noexcept;

        /**
        * \returns a deallocator, used only if program ends.
        */
        job_deallocator get_deallocator() noexcept { return coro_deallocator<T>{}; };    //called for deallocation

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

        /**
        * \brief Called by co_await to create an awaitable for coroutines, Functions, vectors, or tuples thereof.
        * \param[in] func The coroutine, Function or vector to await.
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        template<typename U>
        awaitable_tuple<T, U> await_transform(U&& func) noexcept { return { std::tuple<U&&>(std::forward<U>(func)) }; };

        template<typename... Ts>
        awaitable_tuple<T, Ts...> await_transform(std::tuple<Ts...>&& tuple) noexcept { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        template<typename... Ts>
        awaitable_tuple<T, Ts...> await_transform(std::tuple<Ts...>& tuple) noexcept { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        /**.
        * \brief Called by co_await to create an awaitable for migrating to another thread.
        * \param[in] thread_index The thread to migrate to.
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        awaitable_resume_on<T> await_transform(thread_index_t index) noexcept { return { index }; };

        /**.
        * \brief Called by co_await to create an awaitable for scheduling a tag
        * \param[in] tg The tag to schedule
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        awaitable_tag<T> await_transform(tag_t tg) noexcept { return { tg }; };

        /**
        * \brief Create the final awaiter. This awaiter makes sure that the parent is scheduled if there are no more children.
        * \returns the final awaiter.
        */
        final_awaiter<T> final_suspend() noexcept { return {}; };
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
        /**
        * \brief Constructor empty
        */
        explicit Coro_base() noexcept {};

        /**
        * \brief Constructor 
        * \param[in] promise The promise corresponding to this future.
        */
        explicit Coro_base(Coro_promise_base* promise ) noexcept : Queuable(), m_promise(promise) {};   //constructor
        
        /**
        * \brief Resume the coroutine.
        */
        bool resume() noexcept { return m_promise->resume(); };         //resume the Coro
        
        /**
        * \returns a pointer to the promise of this coroutine.
        */
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
        bool m_is_parent_function;                          ///<if true then the parent is a function or nullptr
        std::shared_ptr<std::pair<bool, T>> m_value_ptr;    ///<pointer to the value, use only if parent is a function or nullptr

    private:
        n_exp::coroutine_handle<promise_type> m_coro;       ///<handle to Coro promise

    public:
        /**
        * \brief Coro future constructor
        */
        Coro() noexcept : Coro_base() {};

        /**
        * \brief Coro future constructor
        * \param[in] h Coroutine handle
        * \param[in] value_ptr Shared pointer to the value to return, used only if parent is a function or nullptr
        * \param[in] is_parent_function Flag for using shared value pointer
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
        * \param[in] t Source coroutine that is moved into this coroutine
        */
        Coro(Coro<T>&& t)  noexcept : Coro_base(t.m_promise)
                                        , m_coro(std::exchange(t.m_coro, {}))
                                        , m_value_ptr(std::exchange(t.m_value_ptr, {}))
                                        , m_is_parent_function(std::exchange(t.m_is_parent_function, {})) {};

        /**
        * \brief Move operator
        * \param[in] t Source coroutine that is moved into this coroutine
        */
        void operator= (Coro<T>&& t) noexcept {
            m_is_parent_function    = t.m_is_parent_function;
            m_coro                  = std::exchange(t.m_coro, {});
            m_value_ptr             = std::exchange( t.m_value_ptr, nullptr );
            m_promise               = std::exchange( t.m_promise, {});
        }

        /**
        * \brief Destructor of the Coro promise.
        */
        ~Coro() noexcept {
            if (!m_is_parent_function && m_coro) { //destroy the promise only if the parent is a coroutine (else it would destroy itself)
                if (m_coro.promise().get_self_destruct()) return;
                m_coro.destroy();
            }
        }

        /**
        * \brief Test whether promised value is available
        * \returns true if promised value is available, else false
        */
        bool ready() noexcept {
            if (m_is_parent_function) {
                return m_value_ptr->first;
            }
            return m_coro.promise().m_value.first;
        }

        /**
        * \brief Retrieve the promised value - nonblocking
        * \returns the promised value
        */
        T get() noexcept {
            if (m_is_parent_function) {
                return m_value_ptr->second;
            }
            return m_coro.promise().m_value.second;
        }

        /**
        * \brief Function operator so you can pass on parameters to the Coro.
        *
        * \param[in] thread_index The thread that should execute this coro
        * \param[in] type The type of the coro.
        * \param[in] id A unique ID of the call.
        * \returns a reference to this Coro so that it can be used with co_await.
        */
        decltype(auto) operator() (thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            m_promise->m_thread_index = index;
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
        Coro<void>      get_return_object() noexcept;

        /**
        * \returns a deallocator, used only if program ends.
        */
        job_deallocator get_deallocator() noexcept { return coro_deallocator<void>{}; };    //called for deallocation

        /**
        * \brief Return from aco_return.
        */
        void return_void() noexcept {};

        /**
        * \brief Await a co_yield.
        * \returns a yield_awaiter.
        */
        yield_awaiter<void> yield_value() noexcept { return {}; };

        /**
        * \brief Called by co_await to create an awaitable for coroutines, Functions, vectors, or tuples thereof.
        * \param[in] func The coroutine, Function or vector to await.
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        template<typename U>
        awaitable_tuple<void, U> await_transform(U&& func) noexcept { return { std::tuple<U&&>(std::forward<U>(func)) }; };

        template<typename... Ts>
        awaitable_tuple<void, Ts...> await_transform(std::tuple<Ts...>&& tuple) noexcept { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        template<typename... Ts>
        awaitable_tuple<void, Ts...> await_transform(std::tuple<Ts...>& tuple) noexcept { return { std::forward<std::tuple<Ts...>>(tuple) }; };

        /**.
        * \brief Called by co_await to create an awaitable for migrating to another thread.
        * \param[in] thread_index The thread to migrate to.
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        awaitable_resume_on<void> await_transform(thread_index_t index ) noexcept { return { index }; };

        /**.
        * \brief Called by co_await to create an awaitable for scheduling a tag
        * \param[in] tg The tag to schedule
        * \returns the awaitable for this parameter type of the co_await operator.
        */
        awaitable_tag<void> await_transform(tag_t tg) noexcept { return { tg }; };

        /**
        * \brief Create the final awaiter. This awaiter makes sure that the parent is scheduled if there are no more children.
        * \returns the final awaiter.
        */
        final_awaiter<void> final_suspend() noexcept { return {}; };
    };


    /**
    * \brief Coro future for void
    */
    template<>
    class Coro<void> : public Coro_base {
    protected:
        bool m_is_parent_function;      ///<If true then the parent is a function or nullptr, if false the parent is a coroutine

    public:
        using promise_type = Coro_promise<void>;

    private:
        n_exp::coroutine_handle<promise_type> m_coro;   //handle to Coro promise

    public:
        /**
        * \brief Coro future constructor
        */
        Coro() noexcept : Coro_base() {};

        /**
        * \brief Coro future constructor
        * \param[in] h Coroutine handle
        * \param[in] is_parent_function Flag for using shared value pointer
        */
        Coro(n_exp::coroutine_handle<promise_type> coro, bool is_parent_function) noexcept
            : Coro_base(&coro.promise()), m_is_parent_function(is_parent_function), m_coro(coro) {};

        /**
        * \brief Coro future constructor
        * \param[in] t Source coroutine that is moved into this coroutine
        */
        Coro(Coro<void>&& t) noexcept : Coro_base(t.m_promise)
                                        , m_coro(std::exchange(t.m_coro, {}))
                                        , m_is_parent_function(t.m_is_parent_function) {};

        /**
        * \brief Move operator
        * \param[in] t Source coroutine that is moved into this coroutine
        */
        void operator= (Coro<void>&& t) noexcept { 
            m_is_parent_function    = t.m_is_parent_function;
            m_coro                  = std::exchange(t.m_coro, {});
            m_promise               = std::exchange(t.m_promise, {});
        };

        /**
        * \brief Destructor of the Coro promise.
        */
        ~Coro() noexcept {
            if (!m_is_parent_function && m_coro) { //destroy the promise only if the parent is a coroutine (else it would destroy itself)
                if (m_coro.promise().get_self_destruct()) return;
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
        decltype(auto) operator() (thread_index_t index = thread_index_t{}, thread_type_t type = thread_type_t{}, thread_id_t id = thread_id_t{}) {
            m_promise->m_thread_index = index;
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
    * \param[in] job Pointer to the job (=coroutine promise) to deallocate (=destroy)
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
    * \param[in] job Pointer to the job (=coroutine promise) to deallocate (=destroy)
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
        Coro_promise<void>& promise = h.promise();                 ///<tmp pointer to promise
        bool is_parent_function = promise.m_is_parent_function;    ///<tmp copy of flag
        auto parent = promise.m_parent;                            ///<tmp pointer to parent

        if (parent != nullptr) {          //if there is a parent
            if (is_parent_function) {       //if it is a Job
                JobSystem().child_finished((Job*)parent); //indicate that this child has suspended
            }
            else {  //parent is a coro
                uint32_t num = parent->m_children.fetch_sub(1);   //one less child
                if (num == 1) {                                   //was it the last child?
                    JobSystem().schedule_job(parent);       //if last reschedule the parent coro
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
        Coro_promise<void>& promise = h.promise();                 ///<tmp pointer to promise
        bool is_parent_function = promise.m_is_parent_function;    ///<tmp copy of flag
        auto parent = promise.m_parent;                            ///<tmp pointer to parent

        if (parent != nullptr) {            //if there is a parent
            if (is_parent_function) {       //if it is a Job
                JobSystem().child_finished((Job*)promise.m_parent);//indicate that this child has finished
            }
            else {
                uint32_t num = parent->m_children.fetch_sub(1);   //one less child
                if (num == 1) {                                   //was it the last child?
                    JobSystem().schedule_job(parent);       //if last reschedule the parent coro
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
    inline void* Coro_promise_base::operator new(std::size_t sz, std::allocator_arg_t, n_pmr::memory_resource* mr, Args&&... args) noexcept {
        //std::cout << "Coro new " << sz << "\n";
        auto allocatorOffset = (sz + alignof(n_pmr::memory_resource*) - 1) & ~(alignof(n_pmr::memory_resource*) - 1);
        char* ptr = (char*)mr->allocate(allocatorOffset + sizeof(mr));
        if (ptr == nullptr) {
            std::terminate();
        }
        *reinterpret_cast<n_pmr::memory_resource**>(ptr + allocatorOffset) = mr;
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
    inline void* Coro_promise_base::operator new(std::size_t sz, Class, std::allocator_arg_t, n_pmr::memory_resource* mr, Args&&... args) noexcept {
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
        return operator new(sz, std::allocator_arg, 
            JobSystem::is_instance_created() ? JobSystem().memory_resource() : n_pmr::new_delete_resource(), 
            args...);
    }

    /**
    * \brief Create a promise object using the system standard allocator.
    * \param[in] sz Number of bytes to allocate.
    * \param[in] args the rest of the coro args.
    * \returns a pointer to the newly allocated promise.
    */
    template<typename... Args>
    inline void* Coro_promise_base::operator new(std::size_t sz, Args&&... args) noexcept {
        return operator new(sz, std::allocator_arg, 
            JobSystem::is_instance_created() ? JobSystem().memory_resource() : n_pmr::new_delete_resource(),
            args...);
    }

    /**
    * \brief Use the pointer after the promise as deallocator.
    * \param[in] ptr Pointer to the memory to deallocate.
    * \param[in] sz Number of bytes to deallocate.
    */
    inline void Coro_promise_base::operator delete(void* ptr, std::size_t sz) noexcept {
        //std::cout << "Coro delete " << sz << "\n";
        auto allocatorOffset = (sz + alignof(n_pmr::memory_resource*) - 1) & ~(alignof(n_pmr::memory_resource*) - 1);
        auto allocator = (n_pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
        (*allocator)->deallocate(ptr, allocatorOffset + sizeof(n_pmr::memory_resource*));
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
            n_exp::coroutine_handle<Coro_promise<T>>::from_promise(*this), m_value_ptr, m_is_parent_function };
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
