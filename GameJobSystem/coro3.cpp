

#include <type_traits>


#define _NODISCARD [[nodiscard]]
#include <experimental/coroutine>
#include <experimental/resumable>
#include <experimental/generator>

#include <future>
#include <iostream>
#include <chrono>
#include <thread>
#include <array>
#include <memory_resource>
#include <concepts>
#include <algorithm>
#include <string>



namespace std::experimental {

    template <>
    struct coroutine_traits<void> {
        struct promise_type {
            using coro_handle = std::experimental::coroutine_handle<promise_type>;
            auto get_return_object() {
                return coro_handle::from_promise(*this);
            }
            auto initial_suspend() { return std::experimental::suspend_always(); }
            auto final_suspend() { return std::experimental::suspend_always(); }
            void return_void() {}
            void unhandled_exception() {
                std::terminate();
            }
        };
    };

};

namespace coro3 {
    using namespace std::experimental;

    auto g_global_mem3 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    template<typename T> class task;

    struct MyAllocator;

    //---------------------------------------------------------------------------------------------------

    template<typename T>
    class task_promise {
    public:

        task_promise() : value_(0) {};

        void* operator new(std::size_t size) {
            void* ptr = g_global_mem3.allocate(size);
            if (!ptr) throw std::bad_alloc{};
            return ptr;
        }

        void operator delete(void* ptr, std::size_t size) {
            g_global_mem3.deallocate(ptr, size);
        }

        task<T> get_return_object() noexcept {
            return task<T>{ std::experimental::coroutine_handle<task_promise<T>>::from_promise(*this) };
        }

        std::experimental::suspend_always initial_suspend() noexcept {
            return {};
        }

        void return_value(T t) noexcept {
            value_ = t;
        }

        T get() {
            return value_;
        }

        void unhandled_exception() noexcept {
            std::terminate();
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<task_promise<T>> h) noexcept {
                task_promise<T>& promise = h.promise();
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


    //---------------------------------------------------------------------------------------------------
    template<typename T>
    class task {
    public:
        class awaiter;
        using promise_type = task_promise<T>;
        using value_type = T;

        task(task<T>&& t) noexcept : coro_(std::exchange(t.coro_, {}))
        {}

        ~task() {
            if (coro_) 
                coro_.destroy();
        }

        T get() {
            return coro_.promise().get();
        }

        bool resume() {
            if (!coro_.done())
                coro_.resume();
            return !coro_.done();
        };

        bool await_ready() noexcept {
            return false;
        }

        bool await_suspend(std::experimental::coroutine_handle<promise_type> continuation) noexcept {
            promise_type& promise = coro_.promise();
            promise.continuation_ = continuation;
            coro_.resume();
            return !promise.ready_.exchange(true, std::memory_order_acq_rel);
        }

        T await_resume() noexcept {
            promise_type& promise = coro_.promise();
            return promise.value_;
        }

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : coro_(h)
        {}

    private:
        std::experimental::coroutine_handle<promise_type> coro_;
    };


    struct MyAllocator {
        MyAllocator() {};
        MyAllocator(const MyAllocator&) {};
        ~MyAllocator() {};
        void* allocate(std::size_t sz) {
            return new uint8_t[sz];
        };
        void deallocate(void* p) {
            delete[] p;
        };
    private:
        void* m_state;
    };

    template<typename ALLOCATOR>
    task<int> completes_synchronously(std::allocator_arg_t, ALLOCATOR allocator, int i) {
        co_return 2 * i;
    }

    template<typename ALLOCATOR>
    task<int> loop_synchronously(std::allocator_arg_t, ALLOCATOR allocator, int count) {
        int sum = 0;

        for (int i = 0; i < count; ++i) {
            sum += co_await completes_synchronously(std::allocator_arg, allocator, i);
        }
        co_return sum;
    }

    void testTask() {
        MyAllocator allocator;

        std::pmr::polymorphic_allocator<char> allocator2;

        auto ls = loop_synchronously(std::allocator_arg, allocator2, 10);
        ls.resume();
        int sum = ls.get();
        std::cout << "Sum: " << sum << std::endl;
    }


    void test() {
        testTask();
    }



}





