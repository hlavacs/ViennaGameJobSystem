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

    auto g_global_mem = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 10 }, std::pmr::new_delete_resource());

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
    }


    namespace docu3 {

        //A coro returning a float
        Coro<float> coro_float(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            float f = (float)i;
            std::cout << "coro_float " << f << std::endl;
            co_return f;
        }

        //A coro returning an int
        Coro<int> coro_int(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            std::cout << "coro_int " << i << std::endl;
            co_return i;
        }

        //a function
        void func(int i) {
            std::cout << "func " << i << std::endl;
        }

        Coro<int> test(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {

            auto tv = std::pmr::vector<Coro<int>>{ mr };  //vector of Coro<int>
            tv.emplace_back(coro_int(std::allocator_arg, &g_global_mem, 1));

            auto tk = std::make_tuple(                     //tuple holding two vectors - Coro<int> and Coro<float>
                std::pmr::vector<Coro<int>>{mr},
                std::pmr::vector<Coro<float>>{mr});
            get<0>(tk).emplace_back(coro_int(std::allocator_arg, &g_global_mem, 2));
            get<1>(tk).emplace_back(coro_float(std::allocator_arg, &g_global_mem, 3));

            auto fv = std::pmr::vector<std::function<void(void)>>{ mr }; //vector of C++ functions
            fv.emplace_back([=]() {func(4); });

            std::pmr::vector<Function> jv{ mr };                         //vector of Function{} instances
            Function f = Function([=]() {func(5); }, -1, 0, 0); //schedule to random thread, use type 0 and id 0
            jv.push_back(f);

            co_await tv; //await all elements of the Coro<int> vector
            co_await tk; //await all elements of the vectors in the tuples
            co_await fv; //await all elements of the std::function<void(void)> vector
            co_await jv; //await all elements of the Function{} vector

            vgjs::terminate();

            co_return 0;
        }
    }


    namespace docu4 {
        Coro<int> yield_test(int& input_parameter) {
            int value = 0;          //initialize the fiber here
            while (true) {          //a fiber never returns
                int res = value * input_parameter; //use internal and input parameters
                co_yield res;       //set std::pair<bool,T> value to indicate that this fiber is ready, and suspend
                //here its std::pair<bool,T> value is set to (false, T{}) to indicate thet the fiber is working
                //co_await other(value, input_parameter);  //call any child
                ++value;            //do something useful
            }
            co_return value; //have this only to satisfy the compiler
        }

        int g_yt_in = 0;                //parameter that can be set from the outside
        auto yt = yield_test(g_yt_in);  //create a fiber using an input parameter

        Coro<int> loop(int N) {
            for (int i = 0; i < N; ++i) {
                g_yt_in = i; //set input parameter
                co_await yt; //call the fiber and wait for it to complete
                std::cout << "Yielding " << yt.get().second << "\n";
            }
            vgjs::terminate();
            co_return 0;
        }
    }

    void test(int N) {
        //schedule([]() { docu1::loop(N);});
        //schedule( docu2::loop(std::allocator_arg, &docu::g_global_mem, N) );
        //schedule( docu::docu3::test(std::allocator_arg, &docu::g_global_mem, 1));
        schedule( docu::docu4::loop(N));
    }


};


