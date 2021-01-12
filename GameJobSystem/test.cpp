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
		std::cout << "Test " << std::right << std::setw(3) << N << "  " << std::left << std::setw(22) << S << " " << ( B ? "PASSED":"FAILED" ) << std::endl;\
		C;

	Coro<> start_test() {
		int number = 0;
		std::atomic<int> counter = 0;

		TESTRESULT(++number, "Single function",		co_await[&]() { func(&counter); }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "10 functions",		co_await[&]() { func(&counter, 10); }, counter.load() == 10, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel([&]() { func(&counter); }, [&]() { func(&counter); }), counter.load() == 2, counter = 0);
		TESTRESULT(++number, "Parallel functions",	co_await parallel([&]() { func(&counter, 10); }, [&]() { func(&counter, 10); }), counter.load() == 20, counter = 0);

		TESTRESULT(++number, "Vector function",		co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func(&counter); } }, counter.load() == 1, counter = 0);
		TESTRESULT(++number, "Vector 10 functions", co_await std::pmr::vector<std::function<void(void)>>{ [&]() { func(&counter, 10); } }, counter.load() == 10, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf1{ [&]() { func(&counter); }, [&]() { func(&counter); } };
		TESTRESULT(++number, "Vector 2 functions",  co_await vf1, counter.load() == 2, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		TESTRESULT(++number, "Vector 2x10 functions", co_await vf2, counter.load() == 20, counter = 0);
		std::pmr::vector<std::function<void(void)>> vf2_1{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		std::pmr::vector<std::function<void(void)>> vf2_2{ [&]() { func(&counter, 10); }, [&]() { func(&counter, 10); } };
		TESTRESULT(++number, "Vector Par functions", co_await parallel( vf2_1, vf2_2) , counter.load() == 40, counter = 0);

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

		vgjs::terminate();
		co_return;
	}
}


