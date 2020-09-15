#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>

#define VGJS_TRACE true
#include "VEGameJobSystem2.h"
#include "VECoro.h"


using namespace std::chrono;


namespace mixed {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    Coro<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 1;

        std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    void printData(int i, int id);

    Coro<int> printDataCoro(int i, int id) {
        std::cout << "Print Data Coro " << i << std::endl;
        if (i >0 ) {
            co_await FUNCTION( printData( i - 1, i+1 ) );
        }
        co_return i;
    }

    void printData(int i, int id ) {
        std::cout << "Print Data " << i << std::endl;
        if (i > 0) {
            auto f1 = printDataCoro(i-1, -(i - 1) );
            //auto f2 = printDataCoro(i-1, i + 1 );

            schedule( f1 );
            //schedule( f2 );
        }
    }

    void driver(int i, std::string id) {
        std::cout << "Driver " << i << std::endl;
        if (i == 0) {
            return;
        }

        schedule( FUNCTION( printData(i, -1) ) );

        continuation( FUNCTION( vgjs::terminate() ) );
    }


    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance();
        JobSystem::instance()->set_logging(true);

        schedule(FUNCTION(driver( 20000 , "Driver")));

        std::cout << "Ending test()\n";

    }

}


