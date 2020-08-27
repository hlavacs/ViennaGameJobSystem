

#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#define VE_IMPLEMENT_GAMEJOBSYSTEM
#define VE_IMPLEMENT_GAMEJOBSYSTEM
#include "VEUtilClock.h"
#include "VEGameJobSystem2.h"

using namespace std::chrono;


namespace vgjs {


	void test() {

		JobSystem::instance();

		std::string str;
		std::cin >> str;
	}

}
