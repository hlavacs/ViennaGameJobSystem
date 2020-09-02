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
        std::cout << i << std::endl;
        if (i < 5) {
            Job job([&]() { printData(i+1); });
            //schedule( &job );
        }
    }


    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance();

        Job job( [&]() { printData(1); } );

        schedule(&job);

        std::cout << "Ending test()\n";
        wait_for_termination();
    }

}
