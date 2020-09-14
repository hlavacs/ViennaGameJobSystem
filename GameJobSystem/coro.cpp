

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


    task<int> recursive(std::allocator_arg_t, std::pmr::memory_resource* mr, int i, int N) {
        std::cout << "Recursive " << i << " of " << N << std::endl;

        if (i < N) {
            co_await recursive(std::allocator_arg, mr, i + 1, N);
            co_await recursive(std::allocator_arg, mr, i + 1, N);
        }
        co_return 0;
    }

    task<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 0;
        float f = i + 0.5f;
        std::cout << "ComputeF " << f << std::endl;

        co_return 10.0f * i;
    }


    task<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 1;

        std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    task<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        //std::cout << "DO Compute " << std::endl;
        co_return tk1.get();
    }


    void FCompute( int i ) {
        std::cout << "FCompute " << i << std::endl;
    }


    task<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
        int sum = 0;
        std::cout << "Starting loop\n";

        auto tv = std::pmr::vector<task<int>>{mr};

        auto tk = std::make_tuple(std::pmr::vector<task<int>>{mr}, std::pmr::vector<task<float>>{mr});
        
        auto fv = std::pmr::vector<std::function<void(void)>>{ mr };

        for (int i = 0; i < count; ++i) {
            tv.emplace_back(compute(std::allocator_arg, &g_global_mem4, i));

            get<0>(tk).emplace_back(compute(std::allocator_arg, &g_global_mem4, i));
            //get<0>(tk)[i].thread_index(0);
            get<1>(tk).emplace_back(computeF(std::allocator_arg, &g_global_mem4, i));
            //get<1>(tk)[i].thread_index(0);

            fv.emplace_back( VGJS_FUNCTION( FCompute(i) ) );
        }
        
        std::cout << "Before loop " << std::endl;

        co_await tv;

        co_await tk;

        co_await recursive(std::allocator_arg, &g_global_mem4, 1, 5);

        co_await VGJS_FUNCTION( FCompute(999) );

        co_await fv;

        std::cout << "Ending loop " << std::endl;
        co_return sum;
    }


	void test() {
        std::cout << "Starting test()\n";

		JobSystem::instance();

        schedule( loop(std::allocator_arg, &g_global_mem4, 9) );

        auto l2 = loop(std::allocator_arg, &g_global_mem4, 9);
        //schedule(l2);

        std::cout << "Ending test()\n";

	}

}
