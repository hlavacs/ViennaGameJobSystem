
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VEGameJobSystem.h"
#include "VECoro.h"

namespace coro {
	void test();
}

namespace func {
	void test();
}

namespace mixed {
	void test();
}

namespace docu {
	void test(int);
}

namespace tags {
	void test();
}


void driver( int i ) {

	vgjs::schedule( std::bind(coro::test) );
	vgjs::schedule (std::bind(func::test) );
	vgjs::schedule(std::bind(mixed::test) );
	vgjs::schedule(std::bind(tags::test));

	if (i <= 1) {
		vgjs::continuation([]() { std::cout << "terminate()\n";  vgjs::terminate(); } );
	}
	else {
		vgjs::continuation([=]() { std::cout << "driver(" << i << ")\n";  driver(i - 1); } );
	}
}


using namespace vgjs;

namespace test {
	Coro<> start_test();
}

int main()
{

	JobSystem::instance();

	//schedule( [](){ driver(10); });

	//schedule([=]() {docu::test(5); });

	schedule( test::start_test() );


	wait_for_termination();
	std::cout << "Exit\n";
	std::string str;
	std::cin >> str;
}


