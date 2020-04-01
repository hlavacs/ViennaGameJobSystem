
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>
#include <glm.hpp>


#define VE_ENABLE_MULTITHREADING
#define VE_IMPLEMENT_GAMEJOBSYSTEM
#include "VEUtilClock.h"
#include "VEGameJobSystem.h"

using namespace std::chrono;

constexpr uint32_t epoch_duration = 1000000 / 60;												//1/60 seconds
duration<int, std::micro>			time_delta = duration<int, std::micro>{ epoch_duration };	//the duration of an epoch
time_point<high_resolution_clock>	now_time = high_resolution_clock::now();					//now time
time_point<high_resolution_clock>	current_update_time = now_time;								//start of the current epoch
time_point<high_resolution_clock>	next_update_time = current_update_time + time_delta;		//end of the current epoch
time_point<high_resolution_clock>	reached_time = current_update_time;							//time the simulation has reached

///initialize
void init() {
	now_time				= std::chrono::high_resolution_clock::now();
	current_update_time		= now_time;
	next_update_time		= current_update_time + time_delta;
	reached_time			= current_update_time;
}

///burn some nanoseconds
void burn_ns(glm::mat4 m1, duration<int, std::nano> &dur) {
	auto start = std::chrono::high_resolution_clock::now();
	auto now = start;
	auto goal = start + dur;

	do {
		for( uint32_t i=0; i<10; ++i )
			m1 = m1 * m1;
		now = std::chrono::high_resolution_clock::now();
	} while (now - start < dur);
}

constexpr uint32_t BURN_NS = 1000;

///make game simulations
 glm::mat4 simulate(int32_t loops) {
	glm::mat4 m1((float)loops);

	if (loops == 0) {
		burn_ns(m1, duration<int, std::nano>{ BURN_NS });
		return m1;
	}

	for (int32_t i = 0; i < loops; ++i) {
		JADD(simulate(0));
	}

	return m1;
}


bool go_on = true;	//stay in the game loop
constexpr uint64_t max_loops = 300*60;

vve::VeClock updateClock("Update Clock", 200);

///update the simulation state
void update() {
	simulate( 1000 );

	static uint64_t counter = 0;
	++counter;
	if (counter >= max_loops)
		go_on = false;
}

///prepare data structures
void swapTables() {
}

///forward simulation time to the next epoch
void forwardTime() {
	current_update_time = next_update_time;		//move one epoch further
	next_update_time = current_update_time + time_delta;
}

//acts like a co-routine
void computeOneFrame(uint32_t step) {

	if (step == 1) goto step1;
	if (step == 2) goto step2;
	if (step == 3) goto step3;
	if (step == 4) goto step4;

	now_time = std::chrono::high_resolution_clock::now();

	if (now_time < next_update_time) {		//still in the same time epoch
		return;
	}

step1:
	forwardTime();
	JDEP(computeOneFrame(2));		//wait for finishing, then do step2
	return;

step2:
	swapTables();
	JDEP(computeOneFrame(3));		//wait for finishing, then do step3
	return;

step3:
	updateClock.start();
	update();
	JDEP(updateClock.stop();  computeOneFrame(4));		//wait for finishing, then do step4
	return;

step4:
	reached_time = next_update_time;

	if (now_time > next_update_time) {	//if now is not reached yet
		JDEP(computeOneFrame(1); );	//move one epoch further
	}
}


vve::VeClock loopClock("Loop Clock", 1);

///the main game loop
void runGameLoop() {
	//loopClock.tick();

	while (go_on) {
		JRESET;							//reset the thread pool!!!
		JADD(computeOneFrame(0));		//compute the next frame
		JREP;							//repeat the loop
		JRET;							//if multithreading, return, else stay in loop
	}
	JTERM;
}



int main()
{
	init();

	#ifdef VE_ENABLE_MULTITHREADING
	vgjs::JobSystem::getInstance(0, 1);					//create pool without thread 0
	#endif

	JADD(runGameLoop());								//schedule the game loop

	#ifdef VE_ENABLE_MULTITHREADING
	vgjs::JobSystem::getInstance()->threadTask(0);		//put main thread as first thread into pool
	JWAITTERM;
	#endif

	return 0;
}
