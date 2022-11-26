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


template<int BITS = 64>
struct TagSchedule {

    struct access_t {
        std::bitset<BITS> m_reads;
        std::bitset<BITS> m_writes;
    };

    std::vector<access_t> m_access;

    tag_t get_tag( std::bitset<BITS> reads, std::bitset<BITS> writes ) {
        for (int32_t i = 0; i < m_access.size(); ++i) {
            if ((m_access[i].m_reads & writes) != 0 || (m_access[i].m_writes & reads) != 0 || (m_access[i].m_writes & writes) != 0) {
                m_access[i].m_reads |= reads;
                m_access[i].m_writes |= writes;
                return tag_t{ i };
            }
        }
        m_access.emplace_back( reads, writes );
        return tag_t{ (int32_t)m_access.size() - 1 };
    }

    void reset() {
        m_access.clear();
    }

    int32_t size() {
        return (int32_t)m_access.size();
    }
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

    VgjsCoroReturn<> coro_system() {
        TagSchedule tag;

        co_await parallel( tag.get_tag(1,2), coro2());
        co_await parallel( tag.get_tag(1,3), coro2());
        co_await parallel( tag.get_tag(2,3), coro2());

        for (auto i = 0; i < tag.size(); ++i) {
            co_await parallel(tag_t{i});
        }
        tag.reset();
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


