

#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#include "VEGameJobSystem.h"
#include "VECoro.h"

using namespace std::chrono;


namespace coro {

    using namespace vgjs;

    auto g_global_mem4 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    Coro<int> recursive(std::allocator_arg_t, std::pmr::memory_resource* mr, int i, int N) {
        std::cout << "Recursive " << i << " of " << N << std::endl;

        if (i < N) {
            co_await recursive(std::allocator_arg, mr, i + 1, N);
            co_await recursive(std::allocator_arg, mr, i + 1, N);
        }
        co_return 0;
    }

    Coro<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 0;
        float f = i + 0.5f;
        std::cout << "ComputeF " << f << std::endl;

        co_return 10.0f * i;
    }


    Coro<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //co_await 1;

        std::cout << "Compute " << i << std::endl;

        co_return 2 * i;
    }

    Coro<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        //std::cout << "DO Compute " << std::endl;
        co_return tk1.get();
    }

    void FCompute( int i ) {
        std::cout << "FCompute " << i << std::endl;
    }

    void FuncCompute(int i) {
        std::cout << "FuncCompute " << i << std::endl;
    }

    Coro<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
        int sum = 0;
        std::cout << "Starting loop\n";

        auto tv = std::pmr::vector<Coro<int>>{mr};

        auto tk = std::make_tuple(std::pmr::vector<Coro<int>>{mr}, std::pmr::vector<Coro<float>>{mr});
        
        auto fv = std::pmr::vector<std::function<void(void)>>{ mr };

        std::pmr::vector<Function> jv{ mr };

        for (int i = 0; i < count; ++i) {
            tv.emplace_back( compute(std::allocator_arg, &g_global_mem4, i)(1, 0, 0) );

            get<0>(tk).emplace_back(compute(std::allocator_arg, &g_global_mem4, i));
            get<1>(tk).emplace_back(computeF(std::allocator_arg, &g_global_mem4, i));

            fv.emplace_back( FUNCTION( FCompute(i) ) );

            Function f( FUNCTION(FuncCompute(i)), -1, 0, 0 );
            jv.push_back( f );

            jv.push_back( Function( FUNCTION(FuncCompute(i)), -1, 0, 0) );
        }
        
        std::cout << "Before loop " << std::endl;

        co_await tv;

        co_await tk;

        co_await recursive(std::allocator_arg, &g_global_mem4, 1, 5)( 1, 0, 0);

        co_await FUNCTION( FCompute(999) );

        co_await Function( FUNCTION(FCompute(999)) );

        co_await fv;

        co_await jv;

        std::cout << "Ending loop " << std::endl;

        vgjs::terminate();

        co_return sum;
    }


    void driver() {
        schedule( loop(std::allocator_arg, &g_global_mem4, 90) );

    }

	void test() {
        std::cout << "Starting test()\n";

		JobSystem::instance();

        schedule( FUNCTION( driver()) );

        std::cout << "Ending test()\n";

	}

}
