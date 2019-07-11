
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <string>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace gjs;
using namespace std;

class A {
public:
	A() {};
	~A() {};

	void printA( float f1, int depth, int i2 ) {
		std::string s = std::string(depth, ' ') + "print " + std::to_string(f1) + " depth " + std::to_string(depth) + " " + std::to_string(i2) + " " + std::to_string( (uint32_t)ThreadPool::getInstance()->getJobPointer()) + "\n";
		ThreadPool::getInstance()->printDebug(s);
	};

	void spawn(float f1, int depth, int i2 ) {
		std::string s = std::string(depth, ' ') + "spawn " + std::to_string(f1) + " depth " + std::to_string(depth) + " " + std::to_string(i2) + " " + std::to_string((uint32_t)ThreadPool::getInstance()->getJobPointer()) + "\n";
		ThreadPool::getInstance()->printDebug(s);

		if ( depth<2 ) { //std::rand() % 100 < 50) {
			uint32_t n = 2; // std::rand() % 2;
			for (uint32_t i = 0; i < n; i++) {
				ThreadPool::getInstance()->addChildJob(std::bind(&A::spawn, this, 0.1f, depth + 1, i2), 0, "spawn" );
			}
		}
		else {
			ThreadPool::getInstance()->addChildJob(std::bind(&A::printA, this, 0.1f, depth + 1, i2), 0, "printA" );
		}

	};

};



void case1( A& theA, uint32_t loopNumbers) {
	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < loopNumbers; i++) {
			ThreadPool::getInstance()->addJob( std::bind( &A::printA, theA, 0.1f, 0, i), 0, "printA" );
		}
		JobMemory::getInstance()->resetPool( j );
	}
}


void case2( A& theA, uint32_t loopNumber ) {
	ThreadPool::getInstance()->printDebug("case 2 " + std::to_string(loopNumber) + "\n");

	for (uint32_t i = 0; i < 1; i++) {
		ThreadPool::getInstance()->addChildJob( std::bind( &A::spawn, theA, 0.1f, 0, i), "spawn" ); 
	}

	if (loopNumber > 20) {
		ThreadPool::getInstance()->onFinishedJob( std::bind(&ThreadPool::terminate, ThreadPool::getInstance()), "terminate" );
		return;
	}
	ThreadPool::getInstance()->onFinishedJob( std::bind(&case2, theA, loopNumber+1), "case2 "+ std::to_string(loopNumber+1) );
}


int main()
{
	ThreadPool pool(0);

	A theA;

	pool.addJob( std::bind( &case1, theA, 50 ), 0, "case1 0" );
	pool.wait();

    return 0;
}

