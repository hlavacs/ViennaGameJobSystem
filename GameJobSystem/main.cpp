
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>

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


void driver() {

	coro::test();
	func::test();
	mixed::test();

	vgjs::continuation(F( std::cout << "terminate()\n";  vgjs::terminate(); ));

}

int main()
{
	using namespace vgjs;

	JobSystem::instance();
	schedule( F(driver()) );

	wait_for_termination();
	std::cout << "Exit\n";

	std::string str;
	std::cin >> str;
}


