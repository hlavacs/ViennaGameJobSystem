

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
#include "VETask.h"

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
            auto t = new task<int> (compute(std::allocator_arg, &g_global_mem4, i));

            std::cout << "Before loop " << i << std::endl;
            co_await std::pmr::vector<task_base*>{ t };
            std::cout << "After loop " << t->get() << std::endl;
            delete t;
        }
        std::cout << "Ending loop\n";
        co_return sum;
    }

    task<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        co_return tk1.get();
    }

	void test() {

		JobSystem::instance();

        auto lf = loop(std::allocator_arg, &g_global_mem4, 10);
        schedule(lf);

        //auto doco = do_compute(std::allocator_arg, &g_global_mem4 );
        //doco.resume();

        //join task pool here or setup callbacks from UI

        JobSystem::instance()->wait_for_termination();
	}

}
