


/**
*
* \file
* \brief The Vienna Game Job System (VGJS)
*
* Designed and implemented by Prof. Helmut Hlavacs, Faculty of Computer Science, University of Vienna
* See documentation on how to use it at https://github.com/hlavacs/GameJobSystem
* The library is a single include file, and can be used under MIT license.
*
*/

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

    template<typename T>
    class task_promise_base {

    public:
        task_promise_base() : value_{} {}

        ~task_promise_base() {}

        task<T> get_return_object() {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise_base>::from_promise(*this) };
        }

        std::experimental::suspend_always initial_suspend() {
            return{};
        }

        template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
        void return_value(U&& value) {
            value_ = value;
        }

        void unhandled_exception() {
            std::terminate();
        }

        T& get() {
            return value_;
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            template<typename PROMISE>
            void await_suspend(std::experimental::coroutine_handle<PROMISE> h) noexcept {
                auto& promise = h.promise();
                if (!promise.continuation_) return;

                if (promise.ready_.exchange(true, std::memory_order_acq_rel)) {
                    promise.continuation_.resume();
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

    private:
        friend class task<T>;
        std::experimental::coroutine_handle<> continuation_;
        std::atomic<bool> ready_ = false;
        T value_;
    };


    //--------------------------------------------------------------------------------------

    template<typename T>
    class task {
    public:

        task(task_promise_base<T>& promise, std::experimental::coroutine_handle<> coro)
            : m_promise(std::addressof(promise))
            , m_coro(coro)
        {}

        task(task&& other)
            : m_promise(std::exchange(other.m_promise, nullptr))
            , m_coro(std::exchange(other.m_coro, {}))
        {}

        ~task() {
            if (m_coro)
                m_coro.destroy();
        }

        T get() {
            return m_promise->get();
        }

        bool resume() {
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        //----------------------------------------------------------------------------------------------------------

        bool await_ready() noexcept {
            return false;
        }

        bool await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
            m_promise->continuation_ = continuation;
            m_coro.resume();
            return !m_promise->ready_.exchange(true, std::memory_order_acq_rel);
        }

        T await_resume() noexcept {
            return m_promise->value_;
        }

    private:
        task_promise_base<T>* m_promise;
        std::experimental::coroutine_handle<> m_coro;
    };


    //--------------------------------------------------------------------------------------


    template<typename T, typename Allocator>
    class task_promise : public task_promise_base<T> {
    public:

        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, Allocator& allocator, Args&&...) {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* p = (char*)allocator.allocate(allocatorOffset + sizeof(Allocator));
            try {
                new (p + allocatorOffset) Allocator(allocator);
            }
            catch (...) {
                allocator.deallocate(p, allocatorOffset + sizeof(Allocator));
                throw;
            }
            return p;
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, Allocator& allocator, Args&&... args) {
            return operator new(sz, std::allocator_arg_t{}, allocator, args...);
        }

        void operator delete(void* pVoid, std::size_t sz)
        {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* p = static_cast<char*>(pVoid);
            Allocator& allocator = Allocator(*reinterpret_cast<Allocator*>(p + allocatorOffset));
            Allocator allocatorCopy = std::move(allocator); // assuming noexcept copy here.
            allocator.~Allocator();
            allocatorCopy.deallocate(p, allocatorOffset + sizeof(Allocator));
        }

        task<T> get_return_object() {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise>::from_promise(*this) };
        }
    };
}


namespace std
{
    namespace experimental
    {
        template<typename T, typename... Args>
        struct coroutine_traits<vgjs::task<T>, Args...> {
            using promise_type = vgjs::task_promise_base<T>;
        };

        template<typename T, typename Allocator, typename... Args>
        struct coroutine_traits<vgjs::task<T>, std::allocator_arg_t, Allocator, Args...> {
            using promise_type = vgjs::task_promise<T, Allocator>;
        };

        template<typename T, typename Class, typename Allocator, typename... Args>
        struct coroutine_traits<vgjs::task<T>, Class, std::allocator_arg_t, Allocator, Args...> {
            using promise_type = vgjs::task_promise<T, Allocator>;
        };
    }
}



namespace vgjs {



}





