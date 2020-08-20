

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

#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/when_all.hpp>



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



namespace coro {
    using namespace std::experimental;


    std::atomic<bool> abort = false;
    std::atomic<bool> ready0 = false;
    std::atomic<bool> ready1 = false;

    //coroutine
    class resumable  {
    public:
        struct promise_type {
            using coro_handle = std::experimental::coroutine_handle<promise_type>;
            auto get_return_object() noexcept {
                return coro_handle::from_promise(*this);
            }
            auto initial_suspend() noexcept { 
                return std::experimental::suspend_always();
            }
            auto final_suspend() noexcept { 
                return std::experimental::suspend_always();
            }
            void return_void() {
            }
            void unhandled_exception() {
                std::terminate();
            }
        };
        using coro_handle = std::experimental::coroutine_handle<promise_type>;

        resumable(coro_handle handle) : handle_(handle) { }
        resumable(resumable&) = delete;
        resumable(resumable&&) = delete;

        bool resume() {
            if (! handle_.done())
                handle_.resume();
            return ! handle_.done();
        };

        ~resumable() { 
            //handle_.destroy(); 
        }

    private:
        coro_handle handle_;

    };

    std::function<void(void)> poolfunction0;
    std::function<void(void)> poolfunction1;

    resumable bar(int i) {
        std::cout << "Thread " << std::this_thread::get_id() << std::endl;
        co_return;
    }

    //awaiter
    class resume_new_thread : public std::experimental::suspend_always {
    public:
        void await_suspend(std::experimental::coroutine_handle<> handle)
        {
            handleAddr_ = handle.address();

            //std::thread([handle] { handle(); }).detach();
            if (!ready0) {
                std::cout << "Moving to thread 0" << std::endl;
                poolfunction0 = handle;
                ready0 = true;
                return;
            }
            if (!ready1) {
                std::cout << "Moving to thread 1" << std::endl;
                poolfunction1 = handle;
                ready1 = true;
            }
        }
    private:
        void* handleAddr_;
    };


    resumable foo() {
        std::cout << "Old thread " << std::this_thread::get_id() << std::endl;
        std::cout << "Hello" << std::endl;
        co_await suspend_always{};
        std::cout << "World" << std::endl;

        std::array<int,2> ar{0,1};
        for (auto i : ar) {  
            co_await resume_new_thread{};
            std::cout << "New thread Write0 " << std::this_thread::get_id() << std::endl;
        };
        co_return;

        /*co_await resume_new_thread{};
        std::cout << "New thread Write0 " << std::this_thread::get_id() << std::endl;
        co_await resume_new_thread{};
        std::cout << "New thread Write1 " << std::this_thread::get_id() << std::endl;
        */
    }

    void func(std::function<void(void)>&& f) {
        f();
    }

    void tpool(int i, std::atomic<bool> &ready, std::function<void(void)>& poolfunction) {
        std::cout << "Starting pool thread " << std::endl;
        while (!abort) {
            if (ready) {
                std::cout << "Running pool task on " << i << std::endl;
                poolfunction();
                ready = false;
            }
            else {
                std::this_thread::sleep_for((std::chrono::milliseconds)100);
            }
        }
        std::cout << "Ending pool thread " << i << std::endl;
    };



    void test1() {

        std::thread t0( tpool, 0, std::ref(ready0), std::ref(poolfunction0) );
        t0.detach();

        std::thread t1 (tpool, 1, std::ref(ready1), std::ref(poolfunction1));
        t1.detach();

        std::this_thread::sleep_for((std::chrono::seconds)1);

        resumable f = foo(); //f is now the return object (handle)
        f.resume();
        f.resume();

        char s[100];
        std::cin >> s;

        abort = true;

        std::this_thread::sleep_for((std::chrono::seconds)1);

    }

    auto g_global_mem = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    //
    template<typename T>
    class task {
    public:
        class awaiter;

        class promise_type {
        public:

            promise_type() : value_(0) {};

            void* operator new(std::size_t size) {
                void* ptr = g_global_mem.allocate(size);
                if (!ptr) throw std::bad_alloc{};
                return ptr;
            }

            void operator delete(void* ptr, std::size_t size) {
                g_global_mem.deallocate(ptr, size);
            }

            task<T> get_return_object() noexcept {
                return task<T>{ std::experimental::coroutine_handle<promise_type>::from_promise(*this) };
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

        class awaiter {
        public:
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

            explicit awaiter(std::experimental::coroutine_handle<promise_type> h) noexcept : coro_(h) {
            }

        private:
            std::experimental::coroutine_handle<task<T>::promise_type> coro_;
        };

        auto operator co_await() && noexcept {
            return awaiter{ coro_ };    //awaitable is the NEW task that is co_awaited
        }

        explicit task(std::experimental::coroutine_handle<promise_type> h) noexcept : coro_(h)
        {}

    private:
        std::experimental::coroutine_handle<promise_type> coro_;
    };



    task<int> completes_synchronously(int i) {
        co_return 2*i;
    }

    task<int> loop_synchronously(int count) {
        int sum = 0;

        for (int i = 0; i < count; ++i) {
            sum += co_await completes_synchronously(i);
        }
        co_return sum;
    }

    void testTask() {
        auto ls = loop_synchronously( 10 );
        ls.resume();
        int sum = ls.result();
        std::cout << "Sum: " << sum << std::endl;
    }


    void testRanges() {
        std::string s{ "hello" };

        // output: h e l l o
        std::ranges::for_each(s, [](char c) { std::cout << c << ' '; });
        std::cout << '\n';
    }



    void test() {
        //testTask();
        //test1();
        testRanges();
    }



}



