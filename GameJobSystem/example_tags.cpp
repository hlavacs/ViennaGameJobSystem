#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VEGameJobSystem.h"
#include "VECoro.h"


using namespace std::chrono;


namespace tags {

    using namespace vgjs;

    auto g_global_mem5 = ::n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, n_pmr::new_delete_resource());

    Coro<int> tag2() {
        std::cout << "Tag 2" << std::endl;
        co_await thread_index{ 1 };
        co_return 0;
    }

    void printPar( int i) {
        std::cout << "i: " << i << std::endl;
    }

    Coro<int> tag1() {
        std::cout << "Tag 1" << std::endl;

        co_await parallel(tag{ 1 }, [=]() { printPar(4); }, [=]() { printPar(5); }, tag{ 1 }, [=]() { printPar(6); }, tag{ 1 });
        co_await tag{ 1 };
        co_await tag2();
        co_return 0;
    }

    void tag0() {
        std::cout << "Tag 0" << std::endl;
        schedule([=]() { printPar(1); }, tag{ 0 });
        schedule([=]() { printPar(2); }, tag{ 0 });
        schedule([=]() { printPar(3); }, tag{ 0 });
        schedule(tag{ 0 });   //run jobs with tag 0
        continuation(tag1()); //continue with tag1()
    }

    void test() {
        std::cout << "Starting tag test()\n";
        schedule([=](){ tag0(); });
        std::cout << "Ending tag test()\n";
    }

}


