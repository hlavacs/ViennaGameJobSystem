

#include <type_traits>


#define _NODISCARD [[nodiscard]]
#include <experimental/coroutine>
#include <experimental/resumable>
#include <experimental/generator>

#include <future>
#include <iostream>
#include <chrono>
#include <thread>


#include "VEGameJobSystem2.h"


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
    std::function<void(void)> poolfunction0 = 0;
    std::function<void(void)> poolfunction1 = 0;



    class resumable : public std::experimental::suspend_always {
    public:
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
        using coro_handle = std::experimental::coroutine_handle<promise_type>;

        resumable(coro_handle handle) : handle_(handle) { }
        resumable(resumable&) = delete;
        resumable(resumable&&) = delete;

        bool resume() {
            if (! handle_.done())
                handle_.resume();
            return ! handle_.done();
        };

        ~resumable() { handle_.destroy(); }

    private:
        coro_handle handle_;

    };

    resumable bar( int i) {
        //std::cout << "Bar " << i << " old thread " << std::this_thread::get_id() << std::endl;
        //co_await resume_new_thread{};
        std::cout << "Bar " << i << " new thread2 " << std::this_thread::get_id() << std::endl;
        co_return;
    }

    class resume_new_thread : public resumable {
    public:
        void await_suspend(std::experimental::coroutine_handle<> handle)
        {
            handle_ = handle;

            //std::thread([handle] { handle(); }).detach();
            if (!ready0) {
                std::cout << "Moving to thread 0" << std::endl;
                poolfunction0 = poolfunction0;
                ready0 = true;
                return;
            }
            if (!ready1) {
                std::cout << "Moving to thread 1" << std::endl;
                poolfunction1 = poolfunction0;
                ready1 = true;
            }
        }
    private:
        std::experimental::coroutine_handle<> handle_;
    };



    resumable foo() {
        std::cout << "Old thread " << std::this_thread::get_id() << std::endl;
        std::cout << "Hello" << std::endl;
        co_await suspend_always{};
        std::cout << "World" << std::endl;

        auto b0 = bar(0);
        co_await b0;

        auto b1 = bar(1);
        co_await b1;

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

    void test() {

        std::thread t0( tpool, 0, std::ref(ready0), std::ref(poolfunction0) );
        t0.detach();

        std::thread t1 (tpool, 1, std::ref(ready1), std::ref(poolfunction1));
        t1.detach();

        std::this_thread::sleep_for((std::chrono::seconds)1);

        auto f = foo();

        f.resume();
        f.resume();

        char s[100];
        std::cin >> s;

        abort = true;

        std::this_thread::sleep_for((std::chrono::seconds)1);

    }


}



