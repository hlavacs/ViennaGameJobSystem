#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#include "VEGameJobSystem2.h"
#include "VETask.h"


using namespace std::chrono;


namespace mixed {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    task<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 1;

        std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    void printData(int i) {
        std::cout << i << std::endl;
        if (i < 4) {
            schedule([=]() { printData(i + 1); });
            schedule([=]() { printData(i + 1); });
        }
    }



    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance();

        schedule([=]() { printData(1); });

        std::cout << "Ending test()\n";
        wait_for_termination();
    }

}


