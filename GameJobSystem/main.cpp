
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VEGameJobSystem.h"

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
	//vgjs::schedule (std::bind(func::test) );
	//vgjs::schedule(std::bind(mixed::test) );
	vgjs::schedule(std::bind(tags::test));

	if (i <= 1) {
		vgjs::continuation([]() { std::cout << "terminate()\n";  vgjs::terminate(); } );
	}
	else {
		vgjs::continuation([=]() { std::cout << "driver(" << i << ")\n";  driver(i - 1); } );
	}
}

int main()
{
	using namespace vgjs;

	JobSystem::instance();

	schedule( [](){ driver(1); });

	//schedule([=]() {docu::test(5); });
	
	wait_for_termination();
	std::cout << "Exit\n";
	std::string str;
	std::cin >> str;
}


