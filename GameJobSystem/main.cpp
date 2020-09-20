
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


int main()
{
	using namespace vgjs;

	func::test();

	wait_for_termination();
	std::cout << "Exit\n";
}


