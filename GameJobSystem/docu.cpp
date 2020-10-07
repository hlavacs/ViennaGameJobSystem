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


namespace docu {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    namespace docu1 {
        void printData(int i) {
            std::cout << "Print Data " << i << std::endl;
        }

        void loop(int N) {
            for (int i = 0; i < N; ++i) {
                schedule([=]() { printData(i); });	//all jobs are scheduled to run in parallel
            }

            //after all children have finished, this function will be scheduled to thread 0
            continuation(Function{ std::bind(vgjs::terminate), 0 });
        }
    }


    void test(int N) {
        schedule(std::bind(docu1::loop, N));
    }


};


