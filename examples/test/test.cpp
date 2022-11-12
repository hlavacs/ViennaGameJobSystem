#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>

#include "VGJS.h"

using namespace std::chrono;

using namespace simple_vgjs;

namespace test {

    VgjsJob FF{ []() {}, thread_index_t{-1}};

    void F1(int v) {
        std::cout << "F1\n";
    }

    void F2() {
        std::cout << "F2\n";
        F1(1);
        F1(2);
    }

    VgjsCoroReturn<int> coro3() {
        std::cout << "coro3\n";
        co_return 10;
    }

    VgjsCoroReturn<int> coro2() {
        std::cout << "coro2\n";
        //co_await []() { F2(); };
        co_return 100;
    }

    VgjsCoroReturn<> coro() {
        std::cout << "coro\n";
        /*int res = co_await coro2();

        std::cout << "coro - 2 \n";
        auto f1 = []() {F1(1); };
        auto c3 = coro3();
        auto res2 = co_await parallel(coro2(), coro3(), c3, f1, []() {F2(); });
        std::cout << "coro - 3 \n";*/

        std::vector<VgjsCoroReturn<int>> vec;
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        auto res3 = co_await parallel(vec, F2, F2);
        std::cout << "coro - 4 \n";

        std::vector<void(*)()> vec2;
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        co_await parallel(vec2);
        std::cout << "coro - 5 \n";

        co_return;
    }


};



int main(int argc, char* argv[])
{
    //test::F2();

    /*auto f = [](int i) {
        std::cout << "f(" << i << ")" << "\n";
        volatile static uint64_t sum{ 0 };
        for (int i = 0; i < 10; ++i) {
            sum += i;
            std::cout << sum << "\n";
        };
    };

    auto g = [&](int i) {
        std::cout << "g(" << i << ")" << "\n";
        VgjsJobSystem().schedule([&]() { f(i); });
    };

    for (int i = 0; i < 10; ++i) {
        VgjsJobSystem().schedule([&]() { g(i); });
    }*/

    VgjsJobSystem(thread_count_t{1}).schedule(test::coro());

    //std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::seconds>(10s));

    std::string str;
    std::cin >> str;
    VgjsJobSystem().terminate();

	return 0;
}


