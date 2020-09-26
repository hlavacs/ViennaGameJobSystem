
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
//#include <glm.hpp>

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


void driver( int i ) {

	vgjs::schedule( F(coro::test()) );
	//vgjs::schedule ( F(func::test()) );
	//vgjs::schedule( F(mixed::test()) );

	if (i <= 1) {
		vgjs::continuation(F(std::cout << "terminate()\n";  vgjs::terminate(); ));
	}
	else {
		vgjs::continuation(F(std::cout << "driver(" << i << ")\n";  driver( i-1 ); ));
	}
}

int main()
{
	using namespace vgjs;

	JobSystem::instance();

	schedule( F(driver(1)) );

	wait_for_termination();
	std::cout << "Exit\n";

	std::string str;
	std::cin >> str;
}


