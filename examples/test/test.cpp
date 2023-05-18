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

using namespace vgjs;


template<int BITS = 64>
struct TagSchedule {
    int32_t m_offset = 0;

    struct access_t {
        std::bitset<BITS> m_reads;
        std::bitset<BITS> m_writes;
    };

    std::vector<access_t> m_access;

    int32_t get_tag( std::bitset<BITS> reads, std::bitset<BITS> writes ) {
        for (int32_t i = 0; i < m_access.size(); ++i) {
            if ((m_access[i].m_reads & writes) == 0 && (m_access[i].m_writes & reads) == 0 && (m_access[i].m_writes & writes) == 0) {
                m_access[i].m_reads |= reads;
                m_access[i].m_writes |= writes;
                return i + m_offset;
            }
        }
        m_access.emplace_back( reads, writes );
        return (int32_t)m_access.size() - 1 + m_offset;
    }

    int32_t& offset() { return m_offset; }
    void reset() { m_access.clear(); }
    int32_t size() { return (int32_t)m_access.size(); }
};


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

    VgjsCoroReturn<int> coro4() {
        std::cout << "coro4\n";
        co_return 10;
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
        auto res3 = co_await parallel(vec, VgjsJob{ F2 }, F2);
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

    VgjsCoroReturn<> coro_system() {
        TagSchedule tag{100};

        std::cout << "coro - system \n";

        co_await parallel(tag_t{ tag.get_tag(1,2) }, coro2()(thread_index_t{0}));
        co_await parallel(tag_t{ tag.get_tag(1,4) }, coro3());
        co_await parallel(tag_t{ tag.get_tag(2,4) }, coro4());

        for (auto i = 0; i < tag.size(); ++i) {
            co_await tag_t{i + tag.offset()};
        }
        tag.reset();

        std::cout << "coro - system end\n";
    }

};




int main(int argc, char* argv[])
{
    VgjsJobSystem().schedule(test::coro_system());

    //std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::seconds>(10s));

    std::cout << "Enter any string: \n";
    std::string str;
    std::cin >> str;
    VgjsJobSystem().terminate();

	return 0;
}


