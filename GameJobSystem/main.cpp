
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <string>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace vgjs;
using namespace std;


//a global function does not require a class instance when scheduled
void printA(int depth, int loopNumber) {
	std::string s = std::string(depth, ' ') + "print " + " depth " + std::to_string(depth) + " " + std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::pInstance->getJobPointer()) + "\n";
	JobSystem::pInstance->printDebug(s);
};


class A {
public:
	A() {};
	~A() {};

	//a class member function requires reference/pointer to the instance when scheduled
	void spawn( int depth, int loopNumber ) {
		std::string s = std::string(depth, ' ') + "spawn " + " depth " + std::to_string(depth) + " loops left " + std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::pInstance->getJobPointer()) + "\n";
		JobSystem::pInstance->printDebug(s);

		if (loopNumber == 0) {
			JobSystem::pInstance->onFinishedAddJob(std::bind(&printA, depth + 1, loopNumber), 
				"printA " + std::to_string(depth + 1));
			return;
		}

		JobSystem::pInstance->addChildJob(std::bind(&A::spawn, this, depth + 1, loopNumber - 1), 
			"spawn " + std::to_string(depth + 1));
		JobSystem::pInstance->addChildJob(std::bind(&A::spawn, this, depth + 1, loopNumber - 1), 
			"spawn " + std::to_string(depth + 1));
	};
};

//-----------------------------------------------

//a global function does not require a class instance when scheduled
void case1( A& theA, uint32_t loopNumber) {
	JobSystem::pInstance->printDebug("case 1 number of loops left " + std::to_string(loopNumber) + "\n");

	for (uint32_t i = 0; i < loopNumber; i++) {
		JobSystem::pInstance->addChildJob(std::bind(&printA, 0, loopNumber), "printA " + std::to_string(i));
	}
}

//a global function does not require a class instance when scheduled
void case2( A& theA, uint32_t loopNumber ) {
	JobSystem::pInstance->printDebug("case 2 number of loops left " + std::to_string(loopNumber) + "\n");
	JobSystem::pInstance->addChildJob( std::bind( &A::spawn, theA, 0, loopNumber), "spawn " );
}


//-----------------------------------------------

void playBack(A& theA, uint32_t loopNumber) {
	if (loopNumber == 0) return; 
	JobSystem::pInstance->printDebug("\nplaying loop " + std::to_string(loopNumber) + "\n");
	JobSystem::pInstance->playBackPool(1);
	JobSystem::pInstance->onFinishedAddJob(std::bind(&playBack, theA, loopNumber - 1), 
		"playBack " + std::to_string(loopNumber - 1));
}

void record(A& theA, uint32_t loopNumber) {
	JobSystem::pInstance->printDebug("recording number of loops " + std::to_string(loopNumber) + "\n");
	JobSystem::pInstance->resetPool(1);
	JobSystem::pInstance->addChildJob( std::bind(&A::spawn, theA, 0, loopNumber ), 1, 
		"spawn " + std::to_string(loopNumber) );
	JobSystem::pInstance->onFinishedAddJob( std::bind( &playBack, theA, 3), 
		"playBack " + std::to_string(loopNumber) );
}



//-----------------------------------------------


std::atomic<uint32_t> counter = 0;
using namespace std::chrono;


void spawn( uint32_t depth) {
	counter++;
	if (depth == 0) return;
	if (JobSystem::pInstance->isPlayedBack(1)) return;
	JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1)), 1);
	JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1)), 1);
	JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1)), 1);
	JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1)), 1);
}

void spawn2(uint32_t depth) {
	counter++;
	if (depth == 0) return;
	spawn2(depth - 1);
	spawn2(depth - 1);
	spawn2(depth - 1);
	spawn2(depth - 1);
}


void performance( JobSystem & jobsystem ) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;
	uint32_t loopNumber = 2;
	uint32_t depth = 11;

	//---------------------------------------------------------------------
	counter = 0;
	t1 = high_resolution_clock::now();
	jobsystem.addJob(std::bind(&spawn, depth), 1);
	jobsystem.wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "It took me " << time_span.count() << " seconds for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

	//---------------------------------------------------------------------
	counter = 0;
	t1 = high_resolution_clock::now();

	for (uint32_t i = 0; i < loopNumber; i++) {
		jobsystem.resetPool(1);
		jobsystem.addJob(std::bind(&spawn, depth), 1);
		jobsystem.wait();
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "It took me " << time_span.count() << " seconds for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

	//---------------------------------------------------------------------
	counter = 0;
	t1 = high_resolution_clock::now();
	for (uint32_t i = 0; i < loopNumber; i++) {
		jobsystem.playBackPool(1);
		jobsystem.wait();
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "It took me " << time_span.count() << " seconds for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";


	//---------------------------------------------------------------------
	counter = 0;
	t1 = high_resolution_clock::now();
	for (uint32_t i = 0; i < loopNumber; i++) {
		spawn2(depth);
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "It took me " << time_span.count() << " seconds for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

}



//the main thread starts a child and waits forall jobs to finish by calling wait()
int main()
{
	JobSystem jobsystem(1);

	A theA;
	//jobsystem.addJob( std::bind( &case1, theA, 3 ), "case1" );
	//jobsystem.addJob( std::bind( &case2, theA, 3 ), "case2");
	//jobsystem.addJob( std::bind( &record, theA, 3 ), "record");
	performance( jobsystem );
	jobsystem.wait();


	jobsystem.terminate();
	jobsystem.waitForTermination();

	return 0;
}
