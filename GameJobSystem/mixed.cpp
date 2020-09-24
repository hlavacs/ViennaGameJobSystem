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


namespace mixed {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    Coro<int> compute(int i) {

        //co_await 1;

        //std::cout << "Compute " << i << std::endl;

        //std::this_thread::sleep_for(std::chrono::microseconds(1));

        co_return 2 * i;
    }

    void printData(int i, int id);

    std::atomic<uint32_t> pdc = 0;
    Coro<int> printDataCoro(int i, int id) {
        //std::cout << "Print Data Coro " << i << " id " << ++pdc << std::endl;
        if (i >0 ) {
            //co_await compute(i)(-1, 5, 0);
            co_await Function(F( printData( i - 1, i+1 ) ), -1, i, ++pdc);
            //std::cout << "After Print Data A " << i-1 << std::endl;
            co_await Function(F(printData(i - 1, i + 1)), -1, i, ++pdc);
            //std::cout << "After Print Data B " << i - 1 << std::endl;
        }
        co_return i;
    }

    //std::atomic<uint32_t> pd = 0;
    void printData(int i, int id ) {
        //std::cout << "Print Data " << i << " id " << ++pdc << std::endl;
        uint32_t idl = pdc;
        if (i > 0) {
            auto f1 = printDataCoro(i, -(i - 1))(-1, i, ++pdc);
            //auto f2 = printDataCoro(i, i + 1 )(-1, i, pdc++);

            schedule( f1 );
            //schedule( f2 );

            //schedule(F(printData(i-1, 0)));
            return;
        }
        //std::cout << "Bottom Print Data " << i << " id " << idl << std::endl;

    }


    void loop( int i) {

        if (i == 0) return;

        //std::this_thread::sleep_for(std::chrono::milliseconds(i));

        std::cout << "Loop " << i << std::endl;

        auto f = printDataCoro(5,10)(-1,2,0);
        schedule( f );
        //std::this_thread::sleep_for(std::chrono::microseconds(1));

        continuation(Function(F(loop(i-1)), i-1, 4, 0));

    }

    void driver(int i, std::string id) {
        //std::cout << "Driver " << i << std::endl;
        if (i == 0) {
            return;
        }

        //schedule( Function( F( printData(i, -1) ), -1, 1, 0)  );


        //schedule( F(printData(i, -1)));

        schedule( Function( F(loop(i)), i, 4, 0 ));

        //continuation( Function( F( vgjs::terminate() ), -1, 3, 0 ) );
    }


    void test() {
        std::cout << "Starting mixed test()\n";

        auto& types = JobSystem::instance()->types();
        types[0] = "Driver";
        types[1] = "printData";
        types[2] = "printDataCoro";
        types[3] = "terminate";
        types[4] = "loop";
        types[5] = "compute";

        //JobSystem::instance()->enable_logging();

        schedule( Function( F(driver( 4000 , "Driver")), -1, 0, 0 ) );
        //schedule( F( driver(4, "Driver") ) );

        std::cout << "Ending mixed test()\n";

    }

}


