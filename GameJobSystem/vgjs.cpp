

#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#define VE_IMPLEMENT_GAMEJOBSYSTEM
#define VE_IMPLEMENT_GAMEJOBSYSTEM
#include "VEUtilClock.h"
#include "VEGameJobSystem2.h"

using namespace std::chrono;


namespace vgjs {

    auto g_global_mem4 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    task<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
        co_return 2 * i;
    }

    task<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
        int sum = 0;
        std::cout << "Starting loop\n";

        for (int i = 0; i < count; ++i) {
            sum += co_await compute(std::allocator_arg, &g_global_mem4, i);
        }
        co_return sum;
    }

    task<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        std::vector<task<int>*> tasks;
        auto tk1 = compute( std::allocator_arg, mr, 1 );
        tasks.push_back(&tk1);

        auto re = tasks[0]->resume();
        auto res = tasks[0]->get();

        co_return 0;
    }

	void test() {

		JobSystem<task_promise_base>::instance();

        auto lf = loop(std::allocator_arg, &g_global_mem4, 10);
        schedule(lf.promise());

        auto doco = do_compute(std::allocator_arg, &g_global_mem4 );
        doco.resume();

        //join task pool here or setup callbacks from UI

        //JobSystem::instance()->wait_for_termination();
	}

}
