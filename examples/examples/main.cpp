
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VGJS.h"
#include "VGJSCoro.h"


using namespace vgjs;

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

void run_examples(int i) {
	std::cout << "Loop " << i << "\n";
	vgjs::schedule(coro::test);
	vgjs::schedule(func::test);
	vgjs::schedule(mixed::test);
	vgjs::schedule(tags::test);

	if (i <= 1) {
		vgjs::continuation(vgjs::terminate);
	}
	else {
		vgjs::continuation([=]() { run_examples(i - 1); });
	}
}


int main( int argc, char* argv[])
{
	int num = argc > 1 ? std::stoi(argv[1]) : 0;
	JobSystem js(thread_count_t{num});
	
	schedule([]() { run_examples(100); });

	wait_for_termination();
	std::cerr << "Press Any Key + Return to Exit\n";
	std::string str;
	std::cin >> str;
	return 0;
}


