
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


Coro<> driver() {
	co_await test::start_test();
	co_await []() { examples::run_examples(100); };
	co_return;
}

void driver1() {
	schedule(driver());
	continuation( vgjs::terminate );
}


int main( int argc, char* argv[])
{
	int num = argc > 1 ? std::stoi(argv[1]) : 0;
	JobSystem::instance(thread_count_t{num});
	
	schedule(driver1);

	wait_for_termination();
	std::cerr << "Press Any Key + Return to Exit\n";
	std::string str;
	std::cin >> str;
}


