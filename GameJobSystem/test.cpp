#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>

#include "VEGameJobSystem.h"
#include "VECoro.h"

using namespace std::chrono;


namespace test {

	using namespace vgjs;

	auto g_global_mem = n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, n_pmr::new_delete_resource());

	void func(std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) schedule([=]() { func(atomic_int, i - 1); });
		if (i > 0) (*atomic_int)++;
	}

	void func2(std::atomic<int>* atomic_int, int i = 1) {
		if (i > 1) continuation([=]() { func(atomic_int, i - 1); });
		if (i > 0) (*atomic_int)++;
	}

	void func3( double micro) {
		volatile unsigned int counter = 0;

		auto start = high_resolution_clock::now();
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<microseconds>(stop - start);

		while (duration.count() < micro) {
			for (int i = 0; i < 1000; ++i) {
				++counter;
			}

			stop = high_resolution_clock::now();
			duration = duration_cast<microseconds>(stop - start);
		}

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


	Coro<> performance( bool wrtfunc = true, int num = 1000, double micro = 1.0f) {
		auto& js = JobSystem::instance();

		std::pmr::vector<Function> perfv1{};
		for (int i = 0; i < num; ++i) perfv1.emplace_back(Function{ [&]() { func3(micro); }, thread_index{0} });
		std::pmr::vector<Function> perfv2{};
		for (int i = 0; i < num; ++i) perfv2.emplace_back(Function{ [&]() { func3(micro); } });

		auto start0 = high_resolution_clock::now();
		for (int i = 0; i < num; ++i) func3(micro);
		auto stop0 = high_resolution_clock::now();
		auto duration0 = duration_cast<microseconds>(stop0 - start0);
		//std::cout << "Time for " << num << " function calls on SINGLE thread " << duration0.count() << " us" << std::endl;

		auto start1 = high_resolution_clock::now();
		co_await perfv1;
		auto stop1 = high_resolution_clock::now();
		auto duration1 = duration_cast<microseconds>(stop1 - start1);
		//std::cout << "Time for " << num << " calls on SINGLE thread " << duration1.count() << " us" << std::endl;

		auto start2 = high_resolution_clock::now();
		co_await perfv2;
		auto stop2 = high_resolution_clock::now();
		auto duration2 = duration_cast<microseconds>(stop2 - start2);
		//std::cout << "Time for " << num << " calls on " << js.get_thread_count().value << " threads " << duration2.count() << " us" << std::endl;

		if (wrtfunc) {
			double speedup0 = (double)duration0.count() / (double)duration2.count();
			double efficiency0 = speedup0 / js.get_thread_count().value;
			std::cout << "Wrt function calls: Work/job " << std::setw(3) << micro << " us Speedup " << std::setw(8) << speedup0 << " Efficiency " << std::setw(8) << efficiency0 << std::endl;
			return;
		}

		double speedup1 = (double)duration1.count() / (double)duration2.count();
		double efficiency1 = speedup1 / js.get_thread_count().value;
		    std::cout << "Wrt single thread:  Work/job " << std::setw(3) << micro << " us Speedup " << std::setw(8) << speedup1 << " Efficiency " << std::setw(8) << efficiency1 << std::endl;
	}

#define TESTRESULT(N, S, EXPR, B, C) \
		EXPR; \
		std::cout << "Test " << std::right << std::setw(3) << N << "  " << std::left << std::setw(24) << S << " " << ( B ? "PASSED":"FAILED" ) << std::endl;\
		C;

	Coro<> start_test() {
		int number = 0;
		std::atomic<int> counter = 0;

		//std::function<void(void)>
		TESTRESULT(++number, "Single function",		co_await[&]() { func(&counter); }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 functions",		co_await[&]() { func(&counter, 10); }, counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel([&]() { func(&counter); }, [&]() { func(&counter); }), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel([&]() { func(&counter, 10); }, [&]() { func(&counter, 10); }), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Single function c",	co_await[&]() { func2(&counter); }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 functions c",		co_await[&]() { func2(&counter, 10); }, counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel functions c", co_await parallel([&]() { func2(&counter); }, [&]() { func2(&counter); }), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel functions c", co_await parallel([&]() { func2(&counter, 10); }, [&]() { func2(&counter, 10); }), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Vector function",		co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func(&counter); } }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "Vector 10 functions", co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func(&counter, 10); } }, counter.load() == 10, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf1{ [&]() { func(&counter); }, [&]() { func(&counter); } };
		TESTRESULT(++number, "Vector 2 functions",  co_await vf1, counter.load() == 2, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		TESTRESULT(++number, "Vector 2x10 functions", co_await vf2, counter.load() == 20, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2_1{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		std::pmr::vector<std::function<void(void)>> vf2_2{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		TESTRESULT(++number, "Vector Par functions", co_await parallel( vf2_1, vf2_2) , counter.load() == 40, counter = 0);

		TESTRESULT(++number, "Vector function c", co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func2(&counter); } }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "Vector 10 functions c", co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func2(&counter, 10); } }, counter.load() == 10, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf1c{ [&]() { func2(&counter); }, [&]() { func2(&counter); } };
		TESTRESULT(++number, "Vector 2 functions c", co_await vf1c, counter.load() == 2, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2c{ [&]() { func2(&counter, 10); }, [&]() { func2(&counter, 10); } };
		TESTRESULT(++number, "Vector 2x10 functions c", co_await vf2c, counter.load() == 20, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2_1c{ [&]() { func2(&counter, 10); }, [&]() { func2(&counter, 10); } };
		std::pmr::vector<std::function<void(void)>> vf2_2c{ [&]() { func2(&counter, 10); }, [&]() { func2(&counter, 10); } };
		TESTRESULT(++number, "Vector Par functions c", co_await parallel(vf2_1c, vf2_2c), counter.load() == 40, counter = 0);

		//Function
		TESTRESULT(++number, "Single Function", co_await Function([&]() { func(&counter); }), counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 Functions", co_await Function([&]() { func(&counter, 10); }), counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel Functions", co_await parallel(Function([&]() { func(&counter); }), Function([&]() { func(&counter); })), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel Functions", co_await parallel(Function([&]() { func(&counter, 10); }), Function([&]() { func(&counter, 10); })), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Vector Function", co_await std::pmr::vector<Function>{ Function{ [&]() { func(&counter); } } }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "Vector 10 Functions", co_await std::pmr::vector<Function>{ Function{ [&]() { func(&counter, 10); } } }, counter.load() == 10, counter = 0);
		std::pmr::vector<Function> vf3{ Function{[&]() { func(&counter); }}, Function{[&]() { func(&counter); }} };
		TESTRESULT(++number, "Vector 2 Functions", co_await vf3, counter.load() == 2, counter = 0);
		std::pmr::vector<Function> vf4{ Function{[&]() { func(&counter, 10); }}, Function{[&]() { func(&counter, 10); }} };
		TESTRESULT(++number, "Vector 2x10 Functions", co_await vf4, counter.load() == 20, counter = 0);
		std::pmr::vector<Function> vf4_1{ Function{ [&]() { func(&counter, 10); }}, Function{ [&]() { func(&counter, 10); } } };
		std::pmr::vector<Function> vf4_2{ Function{ [&]() { func(&counter, 10); }}, Function{ [&]() { func(&counter, 10); } } };
		TESTRESULT(++number, "Vector Par Functions", co_await parallel(vf4_1, vf4_2), counter.load() == 40, counter = 0);

		//Coro
		TESTRESULT(++number, "Single Coro<>", co_await coro_void(std::allocator_arg, &g_global_mem, &counter), counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 Coro<>", co_await coro_void(std::allocator_arg, &g_global_mem, &counter, 10), counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel Coro<>", co_await parallel(coro_void(std::allocator_arg, &g_global_mem, &counter), coro_void(std::allocator_arg, &g_global_mem, &counter)), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel Coro<>", co_await parallel(coro_void(std::allocator_arg, &g_global_mem, &counter, 10), coro_void(std::allocator_arg, &g_global_mem, &counter, 10)), counter.load() == 20, counter = 0);

		std::pmr::vector<Coro<>> vf5;
		vf5.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Vector Coro<>", co_await vf5, counter.load() == 1, counter = 0);
		std::pmr::vector<Coro<>> vf6;
		vf6.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Vector 10 Coro<>", co_await vf6, counter.load() == 10, counter = 0);
		std::pmr::vector<Coro<>> vf7;
		vf7.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		vf7.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Vector Coro<>", co_await vf7, counter.load() == 2, counter = 0);
		std::pmr::vector<Coro<>> vf8;
		vf8.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter, 10));
		vf8.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Vector 10 Coro<>", co_await vf8, counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Single Coro<int>", auto ret1 = co_await coro_int(std::allocator_arg, &g_global_mem, &counter), ret1 == 1 && counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 Coro<int>", auto ret2 = co_await coro_int(std::allocator_arg, &g_global_mem, &counter, 10), ret2 == 10 && counter.load() == 10, counter = 0);
		auto [ret3, ret4] = co_await parallel(coro_int(std::allocator_arg, &g_global_mem, &counter), coro_int(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Parallel Coro<int>", , ret3 == 1 && ret4 == 1 && counter.load() == 2, counter = 0);
		auto [ret5, ret6] = co_await parallel(coro_int(std::allocator_arg, &g_global_mem, &counter, 10), coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Parallel Coro<int>", , ret5 == 10 && ret6 == 10 && counter.load() == 20, counter = 0);

		std::pmr::vector<Coro<int>> vci1;
		vci1.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Vector Coro<int>", auto rvci1 = co_await vci1, rvci1[0] == 1 && counter.load() == 1, counter = 0);
		std::pmr::vector<Coro<int>> vci2;
		vci2.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Vector 10 Coro<int>", auto rvci2 = co_await vci2, rvci2[0] == 10 && counter.load() == 10, counter = 0);
		std::pmr::vector<Coro<int>> vci3;
		vci3.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		vci3.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Vector Coro<int>", auto rvci3 = co_await vci3, std::accumulate(rvci3.begin(), rvci3.end(), 0) == 2 && counter.load() == 2, counter = 0);
		std::pmr::vector<Coro<int>> vci4;
		vci4.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		vci4.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Vector 10 Coro<int>", auto rvci4 = co_await vci4, std::accumulate(rvci4.begin(), rvci4.end(), 0) == 20 && counter.load() == 20, counter = 0);

		//Mixing
		auto rm1 = co_await parallel( Function{ [&]() { func(&counter); } }, [&]() { func(&counter); }, coro_void(std::allocator_arg, &g_global_mem, &counter), coro_int(std::allocator_arg, &g_global_mem, &counter));
		TESTRESULT(++number, "Mix Single", , rm1 == 1 && counter.load() == 4, counter = 0);
		auto rm2 = co_await parallel(Function{ [&]() { func(&counter,10); } }, [&]() { func(&counter, 10); }, coro_void(std::allocator_arg, &g_global_mem, &counter, 10), coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		TESTRESULT(++number, "Mix 10 Single", , rm2 == 10 && counter.load() == 40, counter = 0);

		std::pmr::vector<std::function<void(void)>> vmf1{ [&]() { func(&counter); }, [&]() { func(&counter); } };
		std::pmr::vector<Function> vmF1{ Function{ [&]() { func(&counter); }}, Function{ [&]() { func(&counter); } } };
		std::pmr::vector<Coro<>> vmv1;
		vmv1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		vmv1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		std::pmr::vector<Coro<int>> vmi1;
		vmi1.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		vmi1.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		auto rm3 = co_await parallel( vmf1, vmF1, vmv1, vmi1);
		TESTRESULT(++number, "Mix Vec Single", , std::accumulate(rm3.begin(), rm3.end(), 0) == 2 && counter.load() == 8, counter = 0);

		std::pmr::vector<std::function<void(void)>> vmf10{ [&]() { func(&counter,10); }, [&]() { func(&counter,10); } };
		std::pmr::vector<Function> vmF10{ Function{ [&]() { func(&counter,10); }}, Function{ [&]() { func(&counter,10); } } };
		std::pmr::vector<Coro<>> vmv10;
		vmv10.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter, 10));
		vmv10.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter, 10));
		std::pmr::vector<Coro<int>> vmi10;
		vmi10.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		vmi10.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter, 10));
		auto rm30 = co_await parallel(vmf10, vmF10, vmv10, vmi10);
		TESTRESULT(++number, "Mix Vec Single", , std::accumulate(rm30.begin(), rm30.end(), 0) == 20 && counter.load() == 80, counter = 0);

		//Class member functions
		CoroClass cc1;
		TESTRESULT(++number, "Class 1", auto rcc1 = co_await parallel([&]() {cc1.func(); }, cc1.coro_void(), cc1.coro_int()), rcc1 == 1 && cc1.counter.load() == 3, cc1.counter = 0);

		//Fibers using co_yield
		auto cf{ coro_float(&counter) };
		TESTRESULT(++number, "Yield 1", auto rf1 = co_await cf, rf1 == 1.0f && counter.load() == 1,);
		TESTRESULT(++number, "Yield 2", auto rf2 = co_await cf, rf2 == 1.0f && counter.load() == 2, );
		TESTRESULT(++number, "Yield 3", auto rf3 = co_await cf, rf3 == 1.0f && counter.load() == 3, );
		TESTRESULT(++number, "Yield 4", auto rf4 = co_await cf, rf4 == 1.0f && counter.load() == 4, counter = 0);

		auto cf10{ coro_float(&counter,10.5f) };
		TESTRESULT(++number, "Yield 5", auto rf5 = co_await cf10, rf5 == 10.5f && counter.load() == 1,);
		TESTRESULT(++number, "Yield 6", auto rf6 = co_await cf10, rf6 == 10.5f && counter.load() == 2, counter = 0);

		auto cff1{ cc1.coro_float(1.0f) };
		TESTRESULT(++number, "Class Yield 1", auto rf7 =  co_await cff1, rf7 == 1.0f && cc1.counter.load() == 1, );
		TESTRESULT(++number, "Class Yield 2", auto rf8 =  co_await cff1, rf8 == 1.0f && cc1.counter.load() == 2, );
		TESTRESULT(++number, "Class Yield 3", auto rf9 =  co_await cff1, rf9 == 1.0f && cc1.counter.load() == 3, );
		TESTRESULT(++number, "Class Yield 4", auto rf10 = co_await cff1, rf10 == 1.0f && cc1.counter.load() == 4, );

		auto cff2{ cc1.coro_float(10.5f) };
		TESTRESULT(++number, "Class Yield 5", auto rf11 = co_await cff2,                  rf11 == 10.5f && cc1.counter.load() == 5, );
		TESTRESULT(++number, "Class Yield 6", auto rf12 = co_await cff2,                  rf12 == 10.5f && cc1.counter.load() == 6, );
		TESTRESULT(++number, "Class Yield 7", auto rf13 = co_await cc1.coro_float(15.5f), rf13 == 15.5f && cc1.counter.load() == 7, );		

		auto [rf14, rf15] = co_await parallel(cc1.coro_float(1.5f), cff2);
		TESTRESULT(++number, "Class Yield 8", , rf14 == 1.5f && rf15 == 10.5f && cc1.counter.load() == 9, cc1.counter = 0);

		//changing threads
		auto& js = JobSystem::instance();

		co_await thread_index{0};
		TESTRESULT(++number, "Change to thread 0", , js.get_thread_index().value == 0, );

		thread_count tc{js.get_thread_count()};
		co_await thread_index{ tc.value - 1 };
		TESTRESULT(++number, "Change to last thread", , js.get_thread_index().value == tc.value - 1, );

		//scheduling tagged jobs
		counter = 0;
		std::pmr::vector<std::function<void(void)>> tagvf{ [&]() { func(&counter); }, [&]() { func(&counter); } };
		std::pmr::vector<Function> tagvF{ Function{[&]() { func(&counter); }}, Function{[&]() { func(&counter); }} };
		std::pmr::vector<Coro<int>> tagvci;
		tagvci.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		tagvci.emplace_back(coro_int(std::allocator_arg, &g_global_mem, &counter));
		std::pmr::vector<Coro<>> tagvc1;
		tagvc1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		tagvc1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		tagvc1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));
		tagvc1.emplace_back(coro_void(std::allocator_arg, &g_global_mem, &counter));

		co_await parallel(tag{ 1 }, tagvf);
		co_await parallel(tag{ 2 }, tagvF);
		co_await parallel(tag{ 3 }, tagvci, tagvc1);

		TESTRESULT(++number, "Tagged jobs 1", co_await tag{ 1 }, counter.load() == 2, );
		TESTRESULT(++number, "Tagged jobs 2", co_await tag{ 2 }, counter.load() == 4, );
		TESTRESULT(++number, "Tagged jobs 3", co_await tag{ 3 }, counter.load() == 10, counter = 0);

		const int num = 10000;
		std::cout << "\nPerformance for " << num << " function calls\n\n";

		for (double us = 1.0; us <= 8; us += 1.0) {
			co_await performance(true, num, us);
		}
		std::cout << "\n";
		for (double us = 1.0; us <= 8; us += 1.0) {
			co_await performance(false, num, us);
		}
		vgjs::terminate();
		co_return;
	}
}


