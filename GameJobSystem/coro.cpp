

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


namespace coro {

    using namespace vgjs;

    auto g_global_mem4 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    task<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 0;
        std::cout << "ComputeF " << (float)i << std::endl;

        co_return 10.0f * i;
    }


    task<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 1;

        std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    task<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
        int sum = 0;
        std::cout << "Starting loop\n";

        auto tk = std::make_tuple(std::pmr::vector<task<int>>{mr}, std::pmr::vector<task<float>>{mr});
        
        for (int i = 0; i < count; ++i) {
            get<0>(tk).emplace_back(compute(std::allocator_arg, &g_global_mem4, i));
            get<1>(tk).emplace_back(computeF(std::allocator_arg, &g_global_mem4, i));
        }
        
        std::cout << "Before loop " << std::endl;

        co_await tk;

        std::cout << "Ending loop " << std::endl;
        co_return sum;
    }

    task<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        co_return tk1.get();
    }

	void test() {
        std::cout << "Starting test()\n";

		JobSystem::instance();

        auto lf = loop(std::allocator_arg, &g_global_mem4, 3000);
        schedule(lf);

        //auto doco = do_compute(std::allocator_arg, &g_global_mem4 );
        //doco.resume();

        //join task pool here or setup callbacks from UI

        std::cout << "Ending test()\n";

        wait_for_termination();

	}

}
