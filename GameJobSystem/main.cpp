
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <string>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace vgjs;
using namespace std;

class A {
public:
	A() {};
	~A() {};

	void printA( float f1, int depth, int i2 ) {
		std::string s = std::string(depth, ' ') + "print " + std::to_string(f1) + " depth " + std::to_string(depth) + " " + std::to_string(i2) + " " + std::to_string( (uint32_t)JobSystem::getInstance()->getJobPointer()) + "\n";
		JobSystem::getInstance()->printDebug(s);
	};

	void spawn(float f1, int depth, int i2 ) {
		std::string s = std::string(depth, ' ') + "spawn " + std::to_string(f1) + " depth " + std::to_string(depth) + " " + std::to_string(i2) + " " + std::to_string((uint32_t)JobSystem::getInstance()->getJobPointer()) + "\n";
		JobSystem::getInstance()->printDebug(s);

		if ( depth<5 ) { //std::rand() % 100 < 50) {
			uint32_t n = 2; // std::rand() % 2;
			for (uint32_t i = 0; i < n; i++) {
				JobSystem::getInstance()->addChildJob(std::bind(&A::spawn, this, 0.1f, depth + 1, i2), "spawn " + std::to_string(depth+1));
			}
		}
		else {
			JobSystem::getInstance()->addChildJob(std::bind(&A::printA, this, 0.1f, depth + 1, i2), "printA " + std::to_string(depth+1));
		}

	};

};

//-----------------------------------------------

void case1( A& theA, uint32_t loopNumbers) {
	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < loopNumbers; i++) {
			JobSystem::getInstance()->addChildJob( std::bind( &A::printA, theA, 0.1f, 0, i), 0, "printA " );
		}
	}
	JobSystem::getInstance()->onFinishedTerminatePool();
}

void case2( A& theA, uint32_t loopNumber ) {
	JobSystem::getInstance()->printDebug("case 2 " + std::to_string(loopNumber) + "\n");

	for (uint32_t i = 0; i < 1; i++) {
		JobSystem::getInstance()->addChildJob( std::bind( &A::spawn, theA, 0.1f, 0, i), "spawn " );
	}

	if (loopNumber > 5) {
		JobSystem::getInstance()->onFinishedTerminatePool();
		return;
	}
	JobSystem::getInstance()->onFinishedAddJob( std::bind( &case2, theA, loopNumber+1), "case 2 " + std::to_string(loopNumber+1) );
}


//-----------------------------------------------


void playBack(A& theA, uint32_t loopNumber) {

	if (loopNumber < 2) {
		JobSystem::getInstance()->playBackPool(1);
	}

	if (loopNumber < 1) {
		JobSystem::getInstance()->onFinishedAddJob(std::bind(&playBack, theA, loopNumber + 1), "playBack " + std::to_string(loopNumber + 1));
	}
}


void record(A& theA, uint32_t loopNumber) {
	JobSystem::getInstance()->addChildJob( std::bind(&A::spawn, theA, 0.1f, loopNumber, 0 ), 1, "spawn " + std::to_string(loopNumber) );	

	JobSystem::getInstance()->onFinishedAddJob( std::bind( &playBack, theA, loopNumber), "playBack " + std::to_string(loopNumber) );
}




int main()
{
	JobSystem jobsystem(1);

	A theA;

	jobsystem.resetPool(1);
	jobsystem.addJob( std::bind( &record, theA, 0 ), "record" );
	jobsystem.wait();

	jobsystem.terminate();
	jobsystem.waitForTermination();

	return 0;
}
