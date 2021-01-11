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

    auto g_global_mem = ::n_pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 10 }, n_pmr::new_delete_resource());

    namespace docu1 {
        void printData(int i) {
            std::cout << "Print Data " << i << std::endl;
        }

        void loop(int N) {
            for (int i = 0; i < N; ++i) {
                schedule([=]() { printData(i); }); //all jobs are scheduled to run in parallel
            }
        }
    }

    namespace docu1_5 {
        //a coroutine that uses a given memory resource to allocate its promise.
        //the coro calls itself to compute i!
        Coro<int> factorial(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
            if (i == 0) co_return 1;
            auto j = co_await factorial(std::allocator_arg, mr, i - 1);   //call itself
            std::cout << "Fact " << i*j << std::endl;
            co_return i * j;   //return the promised value;
        }

        void other_fun(int i ) {
            auto f = factorial(std::allocator_arg, &docu::g_global_mem, i);
            schedule(f); //schedule the coroutine
            while (!f.ready()) { //wait for the result
                std::this_thread::sleep_for(std::chrono::microseconds(1)); 
            };
            std::cout << "Result " << f.get() << std::endl;
        }
    }

    namespace docu2 {
        //the coro do_compute() uses g_global_mem to allocate its promise!
        Coro<int> do_compute(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
            co_await thread_index{ 0 };     //move this job to the thread with number 0
            co_return i;    //return the promised value;
        }

        //the coro loop() uses g_global_mem to allocate its promise!
        Coro<> loop(std::allocator_arg_t, n_pmr::memory_resource* mr, int N) {
            for (int i = 0; i < N; ++i) {
                auto f = do_compute(std::allocator_arg, mr, i);
                co_await f;     //call do_compute() to create result
                std::cout << "Result " << f.get() << std::endl;
            }
            co_return;
        }
    }


    namespace docu3 {

        //A coro returning a float
        Coro<int> coro_int(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
            std::cout << "coro_int " << i << std::endl;
            co_return i;
        }

        //A coro returning a float
        Coro<float> coro_float(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
            float f = 1.5f*(float)i;
            std::cout << "coro_float " << f << std::endl;
            co_return f;
        }

        //A coro returning nothing
        Coro<> coro_void(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
            std::cout << "coro_void " << i << std::endl;
            co_return;
        }

        //a function
        void func(int i) {
            std::cout << "func " << i << std::endl;
        }

        Coro<int> test(std::allocator_arg_t, n_pmr::memory_resource* mr, int count) {

            n_pmr::vector<Coro<>> tv{ mr };  //vector of Coro<>
            tv.emplace_back(coro_void(std::allocator_arg, &g_global_mem, 1));

            n_pmr::vector<Coro<int>> ti{ mr };
            ti.emplace_back(coro_int(std::allocator_arg, &g_global_mem, 2));

            n_pmr::vector<Coro<float>> tf{mr};
            tf.emplace_back(coro_float(std::allocator_arg, &g_global_mem, 3));

            n_pmr::vector<std::function<void(void)>> fv{ mr }; //vector of C++ functions
            fv.emplace_back([=]() {func(4); });

            n_pmr::vector<Function> jv{ mr };                         //vector of Function{} instances
            jv.emplace_back(Function{ [=]() {func(5); }, thread_index{}, thread_type{ 0 }, thread_id{ 0 } });

            auto [ret1, ret2] = co_await parallel(tv, ti, tf, fv, jv);

            std::cout << "ret1 " << ret1[0] << " ret2 " << ret2[0] << std::endl;

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
                auto ret = co_await yt; //call the fiber and wait for it to complete
                std::cout << "Yielding " << ret << "\n";
            }
            co_return 0;
        }
    }

    namespace docu5 {

        void printPar(int i) { //print something
            std::cout << "i: " << i << std::endl;
        }

        Coro<int> tag1() {
            std::cout << "Tag 1" << std::endl;
            co_await parallel(tag{ 1 }, [=]() { printPar(4); }, [=]() { printPar(5); }, tag{ 1 }, [=]() { printPar(6); }, tag{ 1 });
            co_await tag{ 1 }; //runt jobs with tag 1
            co_return 0;
        }

        void tag0() {
            std::cout << "Tag 0" << std::endl;
            schedule([=]() { printPar(1); }, tag{ 0 });
            schedule([=]() { printPar(2); }, tag{ 0 });
            schedule([=]() { printPar(3); }, tag{ 0 });
            schedule(tag{ 0 });   //run jobs with tag 0
            continuation(tag1()); //continue with tag1()
        }

        void test() {
            std::cout << "Starting tag test()\n";
            schedule([=]() { tag0(); });
            std::cout << "Ending tag test()\n";
        }

    }

    void test(int N) {
        //schedule( [=]() { docu1::loop(N);});
        //schedule([=]() { docu1_5::other_fun(N); });
        //schedule( docu2::loop(std::allocator_arg, &docu::g_global_mem, N) );
        //schedule( docu::docu3::test(std::allocator_arg, &docu::g_global_mem, 1));
        //schedule( docu::docu4::loop(N));
        schedule([=]() { docu::docu5::test(); });

        vgjs::continuation([=]() { vgjs::terminate(); });

    }


};


