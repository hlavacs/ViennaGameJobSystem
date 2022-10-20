#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>

#include "VGJS.h"
#include "VGJSCoro.h"

using namespace std::chrono;

using namespace simple_vgjs;

namespace test {

    VgjsJob FF{ []() {}, thread_index_t{-1}};

    template<typename F>
    void F1(F&& func) {

    }

    void F2() {
        VgjsJob G;
        F1(G);
        F1(VgjsJob{ []() {}, thread_index_t{-1} });

    }

    VgjsCoroReturn<int> coro() {
        co_await std::make_tuple( []() { F2(); } );
        co_return 1;
    }

};



int main(int argc, char* argv[])
{
    //test::F2();

    auto f = [](int i) {
        volatile static uint64_t sum{ 0 };
        for (int i = 0; i < 10; ++i) {
            sum += i;
            std::cout << sum << "\n";
        };
    };

    auto g = [&](int i) {
        VgjsJobSystem().schedule([&]() { f(i); });
    };

    for (int i = 0; i < 100; ++i) {
        VgjsJobSystem().schedule([&]() { f(i); });
    }

    auto c = test::coro();
    //VgjsJobSystem().schedule( c );

    std::string str;
    std::cin >> str;
    VgjsJobSystem().terminate();

	return 0;
}


