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

};


int main(int argc, char* argv[])
{
    //test::F2();

    VgjsJobSystem system(thread_count_t{ 1 });

    system.wait();

	return 0;
}


