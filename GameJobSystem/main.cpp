
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VEGameJobSystem.h"
#include "VECoro.h"


using namespace vgjs;

namespace test {
	Coro<> start_test();
}

namespace examples {
	void run_examples(int i);
}


int main()
{
	JobSystem::instance();
	
	schedule( test::start_test() );

	//schedule( [](){ examples::run_examples(10); } );

	wait_for_termination();
	std::cerr << "Press Any Key + Return to Exit\n";
	std::string str;
	std::cin >> str;
}


