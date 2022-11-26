#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <bitset>

#include "VGJS.h"

using namespace std::chrono;

using namespace simple_vgjs;

namespace test {

    VgjsJob FF{ []() {}, thread_index_t{-1}};

    void F1(int v) {
        std::cout << "F1\n";
    }

    void F2() {
        std::cout << "F2\n";
        F1(1);
        F1(2);
    }

    VgjsCoroReturn<int> coro3() {
        std::cout << "coro3\n";
        co_return 10;
    }

    VgjsCoroReturn<int> coro2() {
        std::cout << "coro2\n";
        //co_await []() { F2(); };
        co_return 100;
    }

    VgjsCoroReturn<> coro() {
        std::cout << "coro\n";
        /*int res = co_await coro2();

        std::cout << "coro - 2 \n";
        auto f1 = []() {F1(1); };
        auto c3 = coro3();
        auto res2 = co_await parallel(coro2(), coro3(), c3, f1, []() {F2(); });
        std::cout << "coro - 3 \n";*/

        std::vector<VgjsCoroReturn<int>> vec;
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        vec.emplace_back(coro2());
        auto res3 = co_await parallel(vec, F2, F2);
        std::cout << "coro - 4 \n";

        std::vector<void(*)()> vec2;
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        vec2.emplace_back(F2);
        co_await parallel(vec2);
        std::cout << "coro - 5 \n";

        co_return;
    }
};

template<int BITS = 64>
struct TagSchedule {

    struct jobs_tag_t {
        std::bitset<BITS> m_reads;
        std::bitset<BITS> m_writes;
        VgjsQueue<VgjsJobParent> m_jobs;
    };

    std::vector<jobs_tag_t> m_job_queues;

    void schedule(VgjsJobParent&& job, std::bitset<BITS> reads, std::bitset<BITS> writes) {
        for (auto &j : m_job_queues ) {
            if ( (j.m_reads & writes) != 0 || (j.m_writes & reads) != 0 || (j.m_writes & writes) != 0 ) {
                j.m_reads |= reads;
                j.writes |= writes;
                j.m_jobs.push_back( std::move(job) );
                return;
            }
        }
        m_job_queues.emplace_back({ reads, writes });
        m_job_queues[m_job_queues.size()-1].m_jobs.push_back(job);
    }
};


int main(int argc, char* argv[])
{
    VgjsJobSystem().schedule(test::coro());

    //std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::seconds>(10s));

    std::string str;
    std::cin >> str;
    VgjsJobSystem().terminate();

	return 0;
}


