

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


namespace coro3 {
    using namespace std::experimental;

    auto g_global_mem3 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    template<typename T> class task;

    //---------------------------------------------------------------------------------------------------

    template<typename T>
    class task_promise {
    public:

        task_promise() : value_(0) {};

        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            char* ptr = (char*)mr->allocate(allocatorOffset + sizeof(mr));
            if (ptr == nullptr) {
                std::terminate();
            }
            *reinterpret_cast<std::pmr::memory_resource**>(ptr + allocatorOffset) = mr;
            return ptr;
        }

        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) {
            return operator new(sz, std::allocator_arg, mr, args...);
        }

       template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) {
            return operator new(sz, std::allocator_arg, std::pmr::get_default_resource(), args...);
        }

        void operator delete(void* ptr, std::size_t sz) {
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            auto allocator = (std::pmr::memory_resource**)((char*)(ptr) + allocatorOffset);
            (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
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

        task(task<T>&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

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

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : coro_(h) {}

    private:
        std::experimental::coroutine_handle<promise_type> coro_;
    };



    class TestClass {
    public:
        TestClass() {};

        task<int> getState(int i) {
            m_state = 2 * i;
            co_return m_state;
        }

        task<int> getState(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            m_state = 2 * i;
            co_return m_state;
        }

        int m_state = 0;
    };

    TestClass tc;


    task<int> completes_synchronously(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
        int j = co_await tc.getState( i);
        co_return j;
    }

    task<int> loop_synchronously(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
        int sum = 0;

        for (int i = 0; i < count; ++i) {
            sum += co_await completes_synchronously(std::allocator_arg, mr, i);
        }
        co_return sum;
    }

    void testTask() {
        auto ls = loop_synchronously(std::allocator_arg, &g_global_mem3, 30);
        ls.resume();
        int sum = ls.get();
        std::cout << "Sum: " << sum << std::endl;
    }


    void test() {
        testTask();
    }



}





