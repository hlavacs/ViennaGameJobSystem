


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


    template<typename T>
    class job {
    public:
        class awaiter;

        template<typename ALLOCATOR>
        class promise_type {
        public:

            promise_type() : value_(0) {};

            template<typename... ARGS>
            void* operator new(std::size_t sz, std::allocator_arg_t, ALLOCATOR& allocator, ARGS&... args) {
                // Round up sz to next multiple of ALLOCATOR alignment
                std::size_t allocatorOffset = (sz + alignof(ALLOCATOR) - 1u) & ~(alignof(ALLOCATOR) - 1u);

                // Call onto allocator to allocate space for coroutine frame.
                void* ptr = allocator.allocate(allocatorOffset + sizeof(ALLOCATOR));

                // Take a copy of the allocator (assuming noexcept copy constructor here)
                new (((char*)ptr) + allocatorOffset) ALLOCATOR(allocator);

                return ptr;
            }

            void operator delete(void* ptr, std::size_t size) {
                std::size_t allocatorOffset =
                    (sz + alignof(ALLOCATOR) - 1u) & ~(alignof(ALLOCATOR) - 1u);

                ALLOCATOR& allocator = *reinterpret_cast<ALLOCATOR*>(
                    ((char*)ptr) + allocatorOffset);

                // Move allocator to local variable first so it isn't freeing its
                // own memory from underneath itself.
                // Assuming allocator move-constructor is noexcept here.
                ALLOCATOR allocatorCopy = std::move(allocator);

                // But don't forget to destruct allocator object in coroutine frame
                allocator.~ALLOCATOR();

                // Finally, free the memory using the allocator.
                allocatorCopy.deallocate(ptr, allocatorOffset + sizeof(ALLOCATOR));
            }

            job<T> get_return_object() noexcept {
                return job<T>{ std::experimental::coroutine_handle<promise_type>::from_promise(*this) };
            }

            std::experimental::suspend_always initial_suspend() noexcept {
                return {};
            }

            void return_value(T t) noexcept {
                value_ = t;
            }

            T result() {
                return value_;
            }

            void unhandled_exception() noexcept {
                std::terminate();
            }

            struct final_awaiter {
                bool await_ready() noexcept {
                    return false;
                }

                void await_suspend(std::experimental::coroutine_handle<promise_type> h) noexcept {
                    promise_type& promise = h.promise();
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

            std::experimental::coroutine_handle<> continuation_;
            std::atomic<bool> ready_ = false;
            T value_;
        };

        job(job<T>&& t) noexcept : coro_(std::exchange(t.coro_, {}))
        {}

        ~job() {
            if (coro_) coro_.destroy();
        }

        T result() {
            return coro_.promise().result();
        }

        bool resume() {
            if (!coro_.done())
                coro_.resume();
            return !coro_.done();
        };

        class awaiter {
        public:
            bool await_ready() noexcept {
                return false;
            }

            template<typename ALLOCATOR>
            bool await_suspend(std::experimental::coroutine_handle<promise_type<ALLOCATOR>> continuation) noexcept {
                promise_type<ALLOCATOR>& promise = coro_.promise();
                promise.continuation_ = continuation;
                coro_.resume();
                return !promise.ready_.exchange(true, std::memory_order_acq_rel);
            }

            template<typename ALLOCATOR>
            T await_resume() noexcept {
                promise_type<ALLOCATOR>& promise = coro_.promise();
                return promise.value_;
            }

            template<typename ALLOCATOR>
            explicit awaiter(std::experimental::coroutine_handle<promise_type<ALLOCATOR>> h) noexcept : coro_(h) {
            }

        private:
            std::experimental::coroutine_handle<> coro_;
        };

        auto operator co_await() && noexcept {
            return awaiter{ coro_ };    //awaitable is the NEW job that is co_awaited
        }

        explicit job(std::experimental::coroutine_handle<> h) noexcept : coro_(h)
        {}

    private:
        std::experimental::coroutine_handle<> coro_;
    };


}

