
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
std::atomic<double> duration_spawn;


void sleep(uint32_t sleepTime) {
	high_resolution_clock::time_point t1, t2;
	t1 = high_resolution_clock::now();
	do {
		t2 = high_resolution_clock::now();
	} while ((double)duration_cast<duration<double>>(t2 - t1).count()*1000000000.0 < sleepTime);
}

void spawn( uint32_t depth, uint32_t workDepth, uint32_t sleepTime) {
	//high_resolution_clock::time_point t1, t2;
	//t1 = high_resolution_clock::now();
	counter++;
	//JobSystem::pInstance->printDebug( "spawn " + std::to_string(counter) + "\n");
	if (depth > 0 && !JobSystem::pInstance->isPlayedBack()) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
	}
	if (depth<workDepth)
		sleep(sleepTime);

	//t2 = high_resolution_clock::now();
	//duration<double> dur = 	duration_cast<duration<double>>(t2 - t1);
	//duration_spawn = duration_spawn + (double)dur.count() ;
}

void spawn2(uint32_t depth, uint32_t workDepth, uint32_t sleepTime ) {
	//high_resolution_clock::time_point t1, t2;
	//t1 = high_resolution_clock::now();
	counter++;
	if (depth > 0) {
		spawn2(depth - 1, workDepth, sleepTime);
		spawn2(depth - 1, workDepth, sleepTime);
	}
	if (depth<workDepth)
		sleep(sleepTime);

	//t2 = high_resolution_clock::now();
	//duration<double> dur = duration_cast<duration<double>>(t2 - t1);
	//duration_spawn = duration_spawn + (double)dur.count();
}

void loop(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, uint32_t sleepTime) {
	for (uint32_t i = 0; i < numberLoops; i++) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth, workDepth, sleepTime)), 1);
	}
}



void performance( JobSystem & jobsystem ) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;
	uint32_t loopNumber = 1000;
	uint32_t depth = 10;
	uint32_t workDepth = 10;
	uint32_t sleepTime = 0;

	//---------------------------------------------------------------------
	counter = 0;
	duration_spawn = 0.0;
	jobsystem.resetPool(1);
	t1 = high_resolution_clock::now();
	jobsystem.addJob( std::move(std::bind(&loop, loopNumber, depth, workDepth, sleepTime)), 1);
	jobsystem.wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Warm up   took me " << time_span.count()*1000.0f << " ms or " + std::to_string(duration_spawn*1000.0) + " ms for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

	//---------------------------------------------------------------------
	counter = 0;
	duration_spawn = 0.0;
	jobsystem.resetPool(1);
	t1 = high_resolution_clock::now();
	jobsystem.addJob(std::move(std::bind(&loop, loopNumber, depth, workDepth, sleepTime)), 1);
	jobsystem.wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Work      took me " << time_span.count()*1000.0f << " ms or " + std::to_string(duration_spawn*1000.0) + " ms for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

	//---------------------------------------------------------------------
	counter = 0;
	duration_spawn = 0.0;
	t1 = high_resolution_clock::now();
	jobsystem.playBackPool(1);
	jobsystem.wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Play back took me " << time_span.count()*1000.0f << " ms or " + std::to_string(duration_spawn*1000.0) + " ms for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

	//---------------------------------------------------------------------
	counter = 0;
	duration_spawn = 0.0;
	t1 = high_resolution_clock::now();
	for (uint32_t i = 0; i < loopNumber; i++) {
		spawn2(depth, workDepth, sleepTime);
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Single Th took me " << time_span.count()*1000.0f << " ms or " + std::to_string(duration_spawn*1000.0) + " ms for " + std::to_string(counter) + " children (" + std::to_string(1000000.0f*time_span.count() / counter) + " us/child)\n";

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
