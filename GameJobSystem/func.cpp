#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#include "VEGameJobSystem2.h"


using namespace std::chrono;


namespace func {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    void printData( int i ) {
        std::cout << "Print Data " << i << std::endl;
        if (i < 5) {
            schedule( [=]() { printData(i+1); } );
            schedule([=]() { printData(i + 1); });
        }
    }

    void driver(int i) {
        std::cout << "Driver " << i << std::endl;

        if (i == 0) {
            return;
        }
        schedule(FUNCTION(printData(i)));

        continuation(FUNCTION(driver(i - 1)));
    }


    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance(1);

        schedule(FUNCTION(driver(5)));

        std::cout << "Ending test()\n";
    }

}

