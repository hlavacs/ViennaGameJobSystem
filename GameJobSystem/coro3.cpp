

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


    //---------------------------------------------------------------------------------------------------

    template<typename T>
    class task_promise_base {
    public:

        task_promise_base() : value_(0) {};


        task<T> get_return_object() noexcept {
            return task<T>{ std::experimental::coroutine_handle<task_promise_base<T>>::from_promise(*this) };
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

            void await_suspend(std::experimental::coroutine_handle<task_promise_base<T>> h) noexcept {
                task_promise_base<T>& promise = h.promise();
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

    template<typename T, typename Allocator>
    class task_promise;

}


namespace std
{
    namespace experimental
    {
        template<typename T, typename... Args>
        struct coroutine_traits<coro3::task<T>, Args...>
        {
            using promise_type = coro3::task_promise_base<T>;
        };

        template<typename T, typename Allocator, typename... Args>
        struct coroutine_traits<coro3::task<T>, std::allocator_arg_t, Allocator, Args...>
        {
            using promise_type = coro3::task_promise<T, Allocator>;
        };

        template<typename T, typename Class, typename Allocator, typename... Args>
        struct coroutine_traits<coro3::task<T>, Class, std::allocator_arg_t, Allocator, Args...>
        {
            using promise_type = coro3::task_promise<T, Allocator>;
        };
    }
}


namespace coro3 {
    //---------------------------------------------------------------------------------------------------
    template<typename T>
    class task {
    public:
        class awaiter;
        using promise_type = task_promise_base<T>;
        using value_type = T;

        task(task<T>&& t) noexcept : coro_(std::exchange(t.coro_, {}))
        {}

        ~task() {
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


    //---------------------------------------------------------------------------------------------------


    template<typename T, typename Allocator>
    class task_promise : public task_promise_base<T>
    {
    public:

        void* operator new(std::size_t size) {
            void* ptr = g_global_mem3.allocate(size);
            if (!ptr) throw std::bad_alloc{};
            return ptr;
        }

        void operator delete(void* ptr, std::size_t size) {
            g_global_mem3.deallocate(ptr, size);
        }

        /*template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, Allocator& allocator, Args&&...)
        {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* mem = (char*)allocator.allocate(allocatorOffset + sizeof(Allocator));
            try
            {
                new (&mem + sz) Allocator(allocator);
            }
            catch (...)
            {
                allocator.deallocate(mem);
                throw;
            }
            return mem;
        }
        void operator delete(void* p, std::size_t sz)
        {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* mem = static_cast<char*>(p);
            Allocator& allocator = *reinterpret_cast<Allocator*>(mem + allocatorOffset);
            Allocator allocatorCopy = std::move(allocator); // assuming noexcept copy here.
            allocator.~Allocator();
            allocatorCopy.deallocate(mem);
        }*/

        task<T> get_return_object()
        {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise>::from_promise(*this) };
        }
    };

}



namespace coro3 {
    task<int> completes_synchronously(int i) {
        co_return 2 * i;
    }

    task<int> loop_synchronously(int count) {
        int sum = 0;

        for (int i = 0; i < count; ++i) {
            sum += co_await completes_synchronously(i);
        }
        co_return sum;
    }

    void testTask() {
        auto ls = loop_synchronously(10);
        ls.resume();
        int sum = ls.result();
        std::cout << "Sum: " << sum << std::endl;
    }


    void test() {
        testTask();
    }



}





