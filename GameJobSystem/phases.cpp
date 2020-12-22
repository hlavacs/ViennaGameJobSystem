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


namespace phases {

    using namespace vgjs;

    auto g_global_mem5 = ::n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, n_pmr::new_delete_resource());

    Coro<int> phase2() {
        std::cout << "Phase 2" << std::endl;
        co_await thread_index{ 1 };

        co_return 0;
    }

    void printPar( int i) {
        std::cout << "i: " << i << std::endl;
    }

    Coro<int> phase1() {
        std::cout << "Phase 1" << std::endl;

        auto fk4 = Function([]() { printPar(4); });

        co_await std::make_tuple( tag{ 2 }, fk4, std::bind( printPar, 4 ) );

        co_await tag{ 2 };

        co_return 0;
    }


    void phase0cont() {
        schedule(phase1());
    }

    void phase0() {
        std::cout << "Phase 0" << std::endl;
        schedule(tag{ 0 });

        schedule([=]() { printPar(0); });

        schedule([=]() { printPar(1); }, tag{ 1 });
        schedule([=]() { printPar(2); }, tag{ 1 });
        schedule([=]() { printPar(3); }, tag{ 1 });

        continuation([]() { phase0cont(); });
    }

    void test() {
        std::cout << "Starting phases test()\n";

        schedule([=](){ phase0(); });

        std::cout << "Ending phases test()\n";
    }

}


