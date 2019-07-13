
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
	std::string s = std::string(depth, ' ') + "print " + " depth " + std::to_string(depth) + " " + std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::getInstance()->getJobPointer()) + "\n";
	JobSystem::getInstance()->printDebug(s);
};


class A {
public:
	A() {};
	~A() {};

	//a class member function requires reference/pointer to the instance when scheduled
	void spawn( int depth, int loopNumber ) {
		std::string s = std::string(depth, ' ') + "spawn " + " depth " + std::to_string(depth) + " loops left " + std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::getInstance()->getJobPointer()) + "\n";
		JobSystem::getInstance()->printDebug(s);

		if (loopNumber == 0) {
			JobSystem::getInstance()->onFinishedAddJob(std::bind(&printA, depth + 1, loopNumber), "printA " + std::to_string(depth + 1));
			return;
		}

		JobSystem::getInstance()->addChildJob(std::bind(&A::spawn, this, depth + 1, loopNumber - 1), "spawn " + std::to_string(depth + 1));
		JobSystem::getInstance()->addChildJob(std::bind(&A::spawn, this, depth + 1, loopNumber - 1), "spawn " + std::to_string(depth + 1));
	};
};

//-----------------------------------------------

//a global function does not require a class instance when scheduled
void case1( A& theA, uint32_t loopNumber) {
	JobSystem::getInstance()->printDebug("case 1 number of loops left " + std::to_string(loopNumber) + "\n");

	for (uint32_t i = 0; i < loopNumber; i++) {
		JobSystem::getInstance()->addChildJob(std::bind(&printA, 0, loopNumber), "printA " + std::to_string(i));
	}
}

//a global function does not require a class instance when scheduled
void case2( A& theA, uint32_t loopNumber ) {
	JobSystem::getInstance()->printDebug("case 2 number of loops left " + std::to_string(loopNumber) + "\n");
	JobSystem::getInstance()->addChildJob( std::bind( &A::spawn, theA, 0, loopNumber), "spawn " );
}


//-----------------------------------------------

void playBack(A& theA, uint32_t loopNumber) {
	if (loopNumber == 0) return; 
	JobSystem::getInstance()->printDebug("\nplaying loop " + std::to_string(loopNumber) + "\n");
	JobSystem::getInstance()->playBackPool(1);
	JobSystem::getInstance()->onFinishedAddJob(std::bind(&playBack, theA, loopNumber - 1), "playBack " + std::to_string(loopNumber - 1));
}

void record(A& theA, uint32_t loopNumber) {
	JobSystem::getInstance()->printDebug("recording number of loops " + std::to_string(loopNumber) + "\n");
	JobSystem::getInstance()->resetPool(1);
	JobSystem::getInstance()->addChildJob( std::bind(&A::spawn, theA, 0, loopNumber ), 1, "spawn " + std::to_string(loopNumber) );
	JobSystem::getInstance()->onFinishedAddJob( std::bind( &playBack, theA, 3), "playBack " + std::to_string(loopNumber) );
}


//the main thread starts a child and waits forall jobs to finish by calling wait()
int main()
{
	JobSystem jobsystem(0);

	A theA;

	//jobsystem.addJob( std::bind( &case1, theA, 3 ), "case1" );
	//jobsystem.addJob( std::bind( &case2, theA, 3 ), "case2");
	jobsystem.addJob( std::bind( &record, theA, 3 ), "record");
	jobsystem.wait();

	jobsystem.terminate();
	jobsystem.waitForTermination();

	return 0;
}
