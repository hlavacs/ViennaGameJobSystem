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


namespace test {

	using namespace vgjs;

	auto g_global_mem = n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, n_pmr::new_delete_resource());

	void func( std::atomic<int> * atomic_int, int i=1 ) {
		if (i > 1) schedule([=]() { func(atomic_int, i-1); });
		if (i>0) (*atomic_int)++;
	}

	Coro<> coro_void(std::allocator_arg_t, n_pmr::memory_resource* mr, std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) co_await coro_void(std::allocator_arg, mr, atomic_int, i - 1 );
		if (i > 0) (*atomic_int)++;
		co_return;
	}

	Coro<int> coro_int(std::allocator_arg_t, n_pmr::memory_resource* mr, std::atomic<int>* atomic_int, int i = 1) {
		int ret = i==0 ? 0 : 1;
		if (i > 0) { (*atomic_int)++; if (i > 1) ret += co_await coro_int(std::allocator_arg, mr, atomic_int, i - 1); };
		co_return ret;
	}

	Coro<float> coro_float(std::allocator_arg_t, n_pmr::memory_resource* mr, std::atomic<int>* atomic_int, int i = 1) {
		float ret = i == 0 ? 0.0f : 1.0f;
		if (i > 0) { (*atomic_int)++; if (i > 1) ret += co_await coro_float(std::allocator_arg, mr, atomic_int, i - 1); };
		co_return ret;
	}

	class CoroClass {
	public:
		std::atomic<int> atomic_int = 0;

		void func() {
			++atomic_int;
			return;
		}

		Coro<int> coro_int() {
			++atomic_int;
			co_return 1;
		}

		Coro<float> coro_float() {
			++atomic_int;
			co_return 1.0f;
		}
	};

	#define TESTRESULT(N, S, EXPR, B, C) \
		EXPR; \
		std::cout << "Test " << std::right << std::setw(3) << N << "  " << std::left << std::setw(18) << S << " " << ( B ? "PASSED":"FAILED" ) << std::endl;\
		C;

	Coro<> start_test() {
		int number = 0;
		std::atomic<int> counter = 0;

		TESTRESULT(++number, "Single function",		co_await [&](){func(&counter);}, counter.load()==1, counter=0);
		TESTRESULT(++number, "10 functions",		co_await [&]() { func(&counter, 10); }, counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel( [&]() { func(&counter); }, [&]() { func(&counter); }), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel([&]() { func(&counter,10); }, [&]() { func(&counter,10); }), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Single Function",		co_await Function([&]() { func(&counter); }), counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 Functions",		co_await Function([&]() { func(&counter, 10); }), counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel Functions",	co_await parallel(Function([&]() { func(&counter); }),	 Function([&]() { func(&counter); }) ),	  counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel Functions",	co_await parallel(Function([&]() { func(&counter, 10); }), Function([&]() { func(&counter, 10); }) ), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Single Coro",		co_await coro_void(std::allocator_arg, &g_global_mem, & counter), counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 Coros",		co_await coro_void(std::allocator_arg, &g_global_mem, &counter,10), counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel Coros",	co_await parallel(coro_void(std::allocator_arg, &g_global_mem, &counter), coro_void(std::allocator_arg, &g_global_mem, &counter)), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel Coros",	co_await parallel(coro_void(std::allocator_arg, &g_global_mem, &counter,10), coro_void(std::allocator_arg, &g_global_mem, &counter,10)), counter.load() == 20, counter = 0);

		vgjs::terminate();
		co_return;
	}
}


