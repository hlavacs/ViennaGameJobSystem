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


namespace docu {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

    namespace docu1 {
        void printData(int i) {
            std::cout << "Print Data " << i << std::endl;
        }

        void loop(int N) {
            for (int i = 0; i < N; ++i) {
                schedule([=]() { printData(i); }); //all jobs are scheduled to run in parallel
            }

            //after all children have finished, this function will be scheduled to thread 0
            continuation(Function{ std::bind(vgjs::terminate), 0 });
        }
    }


    namespace docu2 {
        //the coro do_compute() uses g_global_mem to allocate its promise!
        Coro<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            co_await 0;     //move this job to the thread with number 0
            co_return i;    //return the promised value;
        }

        //the coro loop() uses g_global_mem to allocate its promise!
        Coro<> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int N) {
            for (int i = 0; i < N; ++i) {
                auto f = do_compute(std::allocator_arg, mr, i);
                co_await f;     //call do_compute() to create result
                std::cout << "Result " << f.get().second << std::endl;
            }
            vgjs::terminate();
            co_return;
        }

        auto g_global_mem =      //my own memory pool
            std::pmr::synchronized_pool_resource(
                { .max_blocks_per_chunk = 1000, .largest_required_pool_block = 1 << 10 }, std::pmr::new_delete_resource());

    }


    namespace docu3 {
        auto g_global_mem4 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());

        //A recursive coro
        Coro<int> recursive(std::allocator_arg_t, std::pmr::memory_resource* mr, int i, int N) {

            std::pmr::vector<Coro<int>> vec{ mr }; //a vector holding Coro<int> instances
            int res = 0;

            if (i < N) {

                vec.emplace_back(recursive(std::allocator_arg, mr, i + 1, N)); //insert 2 instances
                vec.emplace_back(recursive(std::allocator_arg, mr, i + 1, N));

                co_await vec;  //await all of them at once

                res = std::get<1>(vec[0].get());
            }
            co_return res; //use the result of one of them
        }

        //A coro returning a float
        Coro<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            float f = i + 0.5f;
            co_return 10.0f * i;
        }

        //A coro returning an int
        Coro<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            co_return 2 * i;
        }

        //A coro awaiting an lvalue and using the return value
        Coro<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
            auto tk1 = compute(std::allocator_arg, mr, 1);
            co_await tk1;
            co_return std::get<1>(tk1.get());
        }

        void FCompute(int i) {
            std::cout << "FCompute " << i << std::endl;
        }

        void FuncCompute(int i) {
            std::cout << "FuncCompute " << i << std::endl;
        }

        Coro<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {
            std::cout << "Loop " << count << std::endl;

            auto tv = std::pmr::vector<Coro<int>>{ mr };  //vector of Coro<int>
            auto tk = std::make_tuple(                  //tuple holding two vectors - Coro<int> and Coro<float>
                std::pmr::vector<Coro<int>>{mr},
                std::pmr::vector<Coro<float>>{mr});
            auto fv = std::pmr::vector<std::function<void(void)>>{ mr }; //vector of C++ functions
            std::pmr::vector<Function> jv{ mr };        //vector of Function{} instances

            //loop adds elements to these vectors
            for (int i = 0; i < count; ++i) {
                tv.emplace_back(do_compute(std::allocator_arg, &g_global_mem4));

                get<0>(tk).emplace_back(compute(std::allocator_arg, &g_global_mem4, i));
                get<1>(tk).emplace_back(computeF(std::allocator_arg, &g_global_mem4, i));

                fv.emplace_back([=]() {FCompute(i); });

                Function f = Function([=]() {FuncCompute(i); }, -1, 0, 0); //schedule to random thread, use type 0 and id 0
                jv.push_back(f);

                jv.push_back(Function([=]() {FuncCompute(i); }, -1, 0, 0));
            }

            co_await tv; //await all elements of the Coro<int> vector
            co_await tk; //await all elements of the vectors in the tuples
            co_await recursive(std::allocator_arg, &g_global_mem4, 1, 10); //await the recursive calls for a Coro<int>
            co_await []() { FCompute(999); };            //await the function using std::function<void(void)>
            co_await Function([]() {FCompute(999); });  //await the function using Function{}
            co_await fv; //await all elements of the std::function<void(void)> vector
            co_await jv; //await all elements of the Function{} vector

            vgjs::terminate();

            co_return 0;
        }

    }

    void test(int N) {
        //schedule(std::bind(docu1::loop, N));
        //schedule( docu2::loop(std::allocator_arg, &docu2::g_global_mem, 5));
        schedule( docu::docu3::loop(std::allocator_arg, &docu::docu3::g_global_mem4, 1));
    }


};


