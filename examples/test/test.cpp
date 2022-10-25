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

    VgjsCoroReturn<int> coro2() {
        std::cout << "coro2\n";
        //co_await []() { F2(); };
        co_return 1;
    }

    VgjsCoroReturn<> coro() {
        std::cout << "coro\n";
        //int res = co_await coro2();
        co_await std::make_tuple(coro2(), []() {F1(1); });
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


