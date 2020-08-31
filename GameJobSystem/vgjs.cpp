

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

        unique_ptr_vector<task<int>> tv;

        for (int i = 0; i < count; ++i) {

            auto t = make_unique_ptr<task<int>>(mr, compute(std::allocator_arg, &g_global_mem4, i));
            auto u = make_unique_ptr<task<int>>(mr, compute(std::allocator_arg, &g_global_mem4, 10 * i));
            co_await std::pmr::vector<task_base*>{ t.get(), u.get() };

            std::cout << "Before loop " << i << " " << t->get() << std::endl;

            tv.emplace_back(make_unique_ptr<task<int>>(mr, compute(std::allocator_arg, &g_global_mem4, i)) );

            std::cout << "After loop " << t->get() << std::endl;
        }

        co_await tv;

        std::cout << "Ending loop " << tv[tv.size()-1]->get() << std::endl;
        co_return sum;
    }

    task<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        co_return tk1.get();
    }

	void test() {

		JobSystem::instance();

        auto lf = loop(std::allocator_arg, &g_global_mem4, 100);
        schedule(lf);

        //auto doco = do_compute(std::allocator_arg, &g_global_mem4 );
        //doco.resume();

        //join task pool here or setup callbacks from UI

        JobSystem::instance()->wait_for_termination();
	}

}
