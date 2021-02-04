#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>

#include "VGJS.h"
#include "VGJSCoro.h"

using namespace std::chrono;


namespace test {

	using namespace vgjs;

	const int num_blocks = 50000;
	const int block_size = 1 << 10;

	auto				g_global_mem = n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = num_blocks, .largest_required_pool_block = block_size }, n_pmr::new_delete_resource());

	auto				g_global_mem_f = n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = num_blocks, .largest_required_pool_block = block_size }, n_pmr::new_delete_resource());
	thread_local auto	g_local_mem_f = n_pmr::unsynchronized_pool_resource({ .max_blocks_per_chunk = num_blocks, .largest_required_pool_block = block_size }, n_pmr::new_delete_resource());

	auto				g_global_mem_c = n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = num_blocks, .largest_required_pool_block = block_size }, n_pmr::new_delete_resource());
	thread_local auto	g_local_mem_c = n_pmr::unsynchronized_pool_resource({ .max_blocks_per_chunk = num_blocks, .largest_required_pool_block = block_size }, n_pmr::new_delete_resource());

	thread_local auto	g_local_mem_m = n_pmr::monotonic_buffer_resource( 1<<20, n_pmr::new_delete_resource());

	void func(std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) schedule([=]() { func(atomic_int, i - 1); });
		if (i > 0) (*atomic_int)++;
	}

	void func2(std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) continuation([=]() { func(atomic_int, i - 1); });
		if (i > 0) (*atomic_int)++;
	}

	void func_perf( int micro, int i = 1) {
		volatile unsigned int counter = 1;
		volatile double root = 0.0f;

		if (i > 1) schedule([=]() { func_perf( micro, i - 1); });

		auto start = high_resolution_clock::now();
		auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);

		while (duration.count() < micro) {
			for (int i = 0; i < 10; ++i) {
				counter += counter;
				root = sqrt( (float)counter );
			}
			duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
		}
		//std::cout << duration.count() << std::endl;
	}

	int g_micro;
	int g_i = 1;
	void func_perf2() {
		func_perf( g_micro, g_i );
	}

	Coro<> Coro_perf(std::allocator_arg_t, n_pmr::memory_resource* mr, int micro) {
		func_perf(micro, 1);
		co_return;
	}

	Coro<> coro_void(std::allocator_arg_t, n_pmr::memory_resource* mr, std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) co_await coro_void(std::allocator_arg, mr, atomic_int, i - 1);
		if (i > 0) (*atomic_int)++;
		co_return;
	}

	Coro<int> coro_int(std::allocator_arg_t, n_pmr::memory_resource* mr, std::atomic<int>* atomic_int, int i = 1) {
		int ret = i == 0 ? 0 : 1;
		if (i > 0) { (*atomic_int)++; if (i > 1) ret += co_await coro_int(std::allocator_arg, mr, atomic_int, i - 1); };
		co_return ret;
	}

	Coro<float> coro_float(std::atomic<int>* atomic_int, float f = 1.0f) {
		while (true) {
			(*atomic_int)++;
			co_yield f;
		}
		co_return f;
	}

	class CoroClass {
	public:
		std::atomic<int> counter = 0;

		void func() {
			++counter;
			return;
		}

		Coro<> coro_void() {
			++counter;
			co_return;
		}

		Coro<int> coro_int() {
			++counter;
			co_return 1;
		}

		Coro<float> coro_float( float f) {
			while (true) {
				counter++;
				co_yield f;
			}
			co_return f;
		}
	};


	Coro<> test_utilization_drop( int sec) {
		auto start = high_resolution_clock::now();
		auto duration = duration_cast<seconds>(high_resolution_clock::now() - start);
		JobSystem js;
		auto num = js.get_thread_count().value / 3;
		num = 1; // std::max(num, 1);
		std::pmr::vector<Function> perfv{};
		for (int i = 0; i < num; ++i) {
			perfv.push_back(Function{ []() {func_perf(10000); }, thread_index_t{i} });
		}
		std::pmr::vector<Function> perfv2{};
		for (int i = 0; i < js.get_thread_count().value; ++i) {
			perfv2.push_back(Function{ []() {func_perf(10000); }, thread_index_t{i} });
		}

		do {		
			co_await perfv;
			duration = duration_cast<seconds>(high_resolution_clock::now() - start);
		} while (duration.count() < sec);

		co_await perfv2;		//wake up threads

		co_return;
	}


	template<bool WITHALLOCATE = false, typename FT1 = Function, typename FT2 = std::function<void(void)>>
	Coro<std::tuple<double,double>> performance_function(bool print = true, bool wrtfunc = true, int num = 1000, int micro = 1, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) {
		JobSystem js;

		std::pmr::vector<FT2> perfv2{mr};
		if constexpr (!WITHALLOCATE) {
			if constexpr (std::is_same_v<FT1, Function>) {
				perfv2.resize(num, std::function<void(void)>{[&]() { func_perf(micro); }});
			}
			else {
				if constexpr (std::is_same_v<FT1, pfvoid> ) {
					g_micro = micro;
					for (int i = 0; i < num; ++i) perfv2.push_back(func_perf2);
				}
				else {
					perfv2.reserve(num);
					for (int i = 0; i < num; ++i) perfv2.emplace_back(Coro_perf(std::allocator_arg, std::pmr::new_delete_resource(), micro));
				}
			}
		}

		auto start0 = high_resolution_clock::now();
		for (int i = 0; i < num; ++i) func_perf(micro);
		auto duration0 = duration_cast<microseconds>(high_resolution_clock::now() - start0);
		//std::cout << "Time for " << num << " function calls on SINGLE thread " << duration0.count() << " us" << std::endl;

		auto start2 = high_resolution_clock::now();
		if constexpr (WITHALLOCATE) {
			if constexpr (std::is_same_v<FT1, Function>) {
				perfv2.resize(num, std::function<void(void)>{ [&]() { func_perf(micro); }});
			}
			else {
				if constexpr (std::is_same_v<FT1, pfvoid>) {
					g_micro = micro;
					for (int i = 0; i < num; ++i) perfv2.push_back(func_perf2);
				}
				else {
					perfv2.reserve(num);
					for (int i = 0; i < num; ++i) perfv2.emplace_back(Coro_perf(std::allocator_arg, mr, micro));
				}
			}
		}
		co_await std::move(perfv2);
		auto duration2 = duration_cast<microseconds>(high_resolution_clock::now() - start2);
		//std::cout << "Time for " << num << " calls on " << js.get_thread_count().value << " threads " << duration2.count() << " us" << std::endl;

		double speedup0 = (double)duration0.count() / (double)duration2.count();
		double efficiency0 = speedup0 / js.get_thread_count().value;
		if (print && efficiency0 > 0.85) {
			std::cout << "Wrt function calls: Work/job " << std::right << std::setw(3) << micro << " us Speedup " << std::left << std::setw(8) << speedup0 << " Efficiency " << std::setw(8) << efficiency0 << std::endl;
		}

		co_return std::make_tuple( speedup0, efficiency0 );
	}


	template<bool WITHALLOCATE = false, typename FT1, typename FT2>
	Coro<> performance_driver(std::string text, std::pmr::memory_resource* mr = std::pmr::new_delete_resource(), int runtime = 400000) {
		int num = runtime;
		const int st = 0;
		const int mt = 100;
		const int dt1 = 1;
		const int dt2 = 1;
		const int dt3 = 1;
		const int dt4 = 10;
		int mdt = dt1;
		bool wrt_function = true; //speedup wrt to sequential function calls w/o JS

		JobSystem js;

		std::cout << "\nPerformance for " << text << " on " << js.get_thread_count().value << " threads\n\n";
		int step = 0;
		co_await performance_function<WITHALLOCATE, FT1, FT2>(false, wrt_function, (int)(num), 0); //heat up, allocate enough jobs
		for (int us = st; us <= mt; us += mdt) {
			int loops = (us == 0 ? num : (runtime / us));
			auto [speedup, eff] = co_await performance_function<WITHALLOCATE,FT1,FT2>(true, wrt_function, loops, us, mr);
			if (eff > 0.95) co_return;
			if (us >= 15) mdt = dt2;
			if (us >= 20) mdt = dt3;
			if (us >= 50) mdt = dt4;
		}
		co_return;
	}


	Coro<> start_test() {
		int number = 0;
		std::atomic<int> counter = 0;
		JobSystem js;

		std::cout << "\n\nTest utilization drop\n";
		co_await test_utilization_drop(4);		

		std::cout << "\n\nPerformance: min work (in microsconds) per job so that efficiency is >0.85 or >0.95\n";

		co_await performance_driver<false,pfvoid, pfvoid>("void(*)() calls (w / o allocate)");
		//co_await performance_driver<true, pfvoid, pfvoid>("void(*)() calls (with allocate new/delete)", std::pmr::new_delete_resource());
		co_await performance_driver<true, pfvoid, pfvoid>("void(*)() calls (with allocate synchronized)", &g_global_mem_f);
		//co_await performance_driver<true, pfvoid, pfvoid>("void(*)() calls (with allocate unsynchronized)", &g_local_mem_f);
		//co_await performance_driver<true, pfvoid, pfvoid>("void(*)() calls (with allocate monotonic)", &g_local_mem_m);
		//g_local_mem_m.release();

		co_await performance_driver<false,Function, std::function<void(void)>>("std::function calls (w / o allocate)" );
		//co_await performance_driver<true, Function, std::function<void(void)>>("std::function calls (with allocate new/delete)", std::pmr::new_delete_resource());
		co_await performance_driver<true, Function, std::function<void(void)>>("std::function calls (with allocate synchronized)", &g_global_mem_f);
		//co_await performance_driver<true, Function, std::function<void(void)>>("std::function calls (with allocate unsynchronized)", &g_local_mem_f);
		//co_await performance_driver<true, Function, std::function<void(void)>>("std::function calls (with allocate monotonic)", &g_local_mem_m);
		//g_local_mem_m.release();

		co_await performance_driver<false,Coro<>, Coro<>>("Coro<> calls (w / o allocate)");
		//co_await performance_driver<true, Coro<>, Coro<>>("Coro<> calls (with allocate new/delete)", std::pmr::new_delete_resource());
		co_await performance_driver<true, Coro<>, Coro<>>("Coro<> calls (with allocate synchronized)", &g_global_mem_c);
		//co_await performance_driver<true, Coro<>, Coro<>>("Coro<> calls (with allocate unsynchronized)", &g_local_mem_c);
		//co_await performance_driver<true, Coro<>, Coro<>>("Coro<> calls (with allocate monotonic)", &g_local_mem_m);
		//g_local_mem_m.release();

		std::cout << "\n\nTest utilization drop\n";
		co_await test_utilization_drop(4);

		vgjs::terminate();

		co_return;
	}
};



using namespace vgjs;

int main(int argc, char* argv[])
{
	int num = argc > 1 ? std::stoi(argv[1]) : 0;
	JobSystem js(thread_count_t{ num });

	schedule(test::start_test());

	wait_for_termination();
	std::cerr << "Press Any Key + Return to Exit\n";
	std::string str;
	std::cin >> str;
	return 0;
}


