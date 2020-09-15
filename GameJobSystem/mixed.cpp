#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>

#define VGJS_TRACE true
#include "VEGameJobSystem.h"
#include "VECoro.h"


using namespace std::chrono;


namespace mixed {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    Coro<int> compute(int i) {

        //co_await 1;

        //std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    void printData(int i, int id);

    Coro<int> printDataCoro(int i, int id) {
        //std::cout << "Print Data Coro " << i << std::endl;
        if (i >0 ) {
            //co_await compute(i);
            co_await FUNCTION( printData( i - 1, i+1 ) );
        }
        co_return i;
    }

    void printData(int i, int id ) {
        //std::cout << "Print Data " << i << std::endl;
        if (i > 0) {
            auto f1 = printDataCoro(i-1, -(i - 1))(-1, 2,1);
            auto f2 = printDataCoro(i-1, i + 1 )(-1, 2, 1);

            schedule( f1 );
            schedule( f2 );

            //schedule(FUNCTION(printData(i-1, 0)));
        }
    }

    void driver(int i, std::string id) {
        std::cout << "Driver " << i << std::endl;
        if (i == 0) {
            return;
        }

        //schedule( Function( FUNCTION( printData(i, -1) ), -1, 1, 0)  );

        schedule( FUNCTION(printData(i, -1)));

        //continuation( Function( FUNCTION( vgjs::terminate() ), -1, 3, 0 ) );
        continuation( FUNCTION(vgjs::terminate()) );
    }


    void test() {
        std::cout << "Starting test()\n";

        JobSystem::instance();
        auto& types = JobSystem::instance()->types();
        types[0] = "Driver";
        types[1] = "printData";
        types[2] = "printDataCoro";
        types[3] = "terminate";

        //JobSystem::instance()->enable_logging();

        //schedule( Function( FUNCTION(driver( 100 , "Driver")), -1, 0, 0 ) );
        schedule( FUNCTION( driver(30, "Driver") ) );

        std::cout << "Ending test()\n";

    }

}


