
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
		std::string s(depth, ' ');
		cout << s.c_str() << "print " << f1 << " depth " << depth << " " << i2 << " " << ThreadPool::getInstance()->getJobPointer() << "\n";
	};

	void spawn(float f1, int depth, int i2 ) {
		std::string s(depth, ' ');
		cout << s.c_str() << "spawn " << f1 << " depth " << depth << " " << i2 << " " << ThreadPool::getInstance()->getJobPointer() << "\n";

		if (std::rand() % 100 < 0) {
			uint32_t n = 2; // std::rand() % 2;
			for (uint32_t i = 0; i < n; i++) {
				ThreadPool::getInstance()->addJob(std::bind(&A::spawn, this, 0.1f, depth + 1, i2));
			}
		}
		else {
			ThreadPool::getInstance()->addJob(std::bind(&A::printA, this, 0.1f, depth + 1, i2));
		}

	};

};



void case1( A& theA) {
	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < 20; i++) {
			ThreadPool::getInstance()->addJob( std::bind( &A::printA, theA, 0.1f, 0, i) );
		}
		JobMemory::getInstance()->resetPool( j );
	}
}


void case2( A& theA, uint32_t loopNumber ) {
	cout << "case 2 " << loopNumber << " \n";

	for (uint32_t i = 0; i < 1; i++) {
		//ThreadPool::getInstance()->addJob( std::bind( &A::printA, theA, 0.1f, 0, i) ); 
	}

	if (loopNumber > 2) {
		ThreadPool::getInstance()->onFinishedJob( std::bind(&ThreadPool::terminate, ThreadPool::getInstance()));
		return;
	}
	ThreadPool::getInstance()->onFinishedJob( std::bind(&case2, theA, loopNumber+1), "case2 "+ std::to_string(loopNumber+1) );
}





int main()
{
	ThreadPool pool;

	A theA;

	pool.addJob( std::bind( &case2, theA, 0 ), 0, "case2 0" );
	pool.wait();

    return 0;
}

