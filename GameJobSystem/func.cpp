#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#include "VEGameJobSystem.h"


using namespace std::chrono;


namespace func {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 100000, .largest_required_pool_block = 1 << 22 }, std::pmr::new_delete_resource());

    auto computeF(int i) {
        //std::this_thread::sleep_for(std::chrono::microseconds(1));

        return i * 10.0;
    }

    auto compute( int i ) {
        volatile auto x = 2 * i;
        schedule(FUNCTION(computeF(i)));
        return x;
    }

    void printData( int i ) {
        //std::cout << "Print Data " << i << std::endl;
        if (i > 0) {
            //schedule( FUNCTION( compute(i)) );
            schedule( [=]() { printData(i-1); } );
            schedule( [=]() { printData(i-1); });
        }
    }

    void loop(int N) {
        for (int i = 0; i < N; ++i) {
            schedule(FUNCTION(compute(i)));
        }
    }

    void term() {
        std::cout << "terminate()\n";
        vgjs::terminate();
    }

    void driver(int i) {
        std::cout << "Driver " << i << std::endl;

        schedule(FUNCTION(printData(i)));

        //schedule(FUNCTION(loop(100000)));

        continuation( Function( FUNCTION( term() ), -1, 10, 0) );
    }


    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance(0, 0); // , & g_global_mem5);

        schedule( FUNCTION(driver(24)) );

        std::cout << "Ending test()\n";
    }

}

