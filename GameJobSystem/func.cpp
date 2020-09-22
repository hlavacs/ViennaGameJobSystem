#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>


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
        schedule(F(computeF(i)));
        return x;
    }

    void printData( int i ) {
        //std::cout << "Print Data " << i << std::endl;
        if (i > 0) {
            Function r{ F(compute(i)) };
            schedule( r );
            schedule( F( printData(i-1); ) );
            schedule( F( printData(i-1); ));
        }
    }

    void loop(int N) {
        for (int i = 0; i < N; ++i) {
            schedule(F(compute(i)));
        }
    }

    void driver(int i) {
        std::cout << "Driver " << i << std::endl;

        schedule(Function{ F(printData(i)) });

        //schedule(FUNCTION(loop(100000)));
    }


    void test() {
        std::cout << "Starting func test()\n";

        schedule( F(driver(20)) );

        std::cout << "Ending func test()\n";
    }

}

