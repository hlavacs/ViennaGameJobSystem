#include <future>
#include <iostream>
#include <chrono>
#include <thread>
#include <array>
#include <memory_resource>
#include <concepts>
#include <algorithm>
#include <string>
#include <ranges>

#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/when_all.hpp>



namespace mycppcoro {

    using namespace std::chrono_literals;

    cppcoro::task<int> getFirst() {
        //std::this_thread::sleep_for(1s);
        co_return 1;
    }

    cppcoro::task<int> getSecond() {
        //std::this_thread::sleep_for(1s);
        co_return 2;
    }

    cppcoro::task<int> getThird() {
        //std::this_thread::sleep_for(1s);
        co_return 3;
    }

    template <typename Func>
    cppcoro::task<int> runOnThreadPool(cppcoro::static_thread_pool& tp, Func func) {
        co_await tp.schedule();
        auto res = co_await func();
        co_return res;
    }

    cppcoro::task<> runAll(cppcoro::static_thread_pool& tp) {

        //auto [fir, sec, thi] 
            
        auto fi = co_await cppcoro::when_all(
            runOnThreadPool(tp, getFirst));
            //runOnThreadPool(tp, getSecond),
            //runOnThreadPool(tp, getThird));

        //std::cout << fir << " " << sec << " " << thi << std::endl;

    }



	void test() {
        std::cout << std::endl;


        cppcoro::static_thread_pool tp;                         // (1)

        std::this_thread::sleep_for(2s);

        for (int i = 0; i < 100; ++i) {

            auto start = std::chrono::steady_clock::now();
            cppcoro::sync_wait(runAll(tp));                         // (2)
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;    // (4)

            typedef std::chrono::duration<long double, std::ratio<1, 1000>> MyMilliSecondTick;
            MyMilliSecondTick mytime(elapsed);

            std::cout << "Execution time " << mytime.count() << " mseconds." << std::endl;

            std::this_thread::sleep_for(2ms);

        }

        std::cout << std::endl;
	}

}

