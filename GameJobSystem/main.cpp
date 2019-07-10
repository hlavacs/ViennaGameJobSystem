
#include <iostream>
#include <stdlib.h>
#include <functional>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace gjs;
using namespace std;

class A {
public:
	A() {};
	~A() {};

	void printA( float f1, int i1, int i2 ) {
		cout << "print " << f1 << " " << i1 << " " << i2 << " " << ThreadPool::getInstance()->getJobPointer() << "\n";
	};

	void spawn(float f1, int i1, int i2 ) {
		cout << "spawn " << f1 << " " << i1 << " " << i2 << " " << ThreadPool::getInstance()->getJobPointer() << "\n";

		ThreadPool::getInstance()->addJob(std::bind(&A::printA, this, 0.1f, i1, i1));
	};

};


void case1( A& theA) {
	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < 20; i++) {
			ThreadPool::getInstance()->addJob( std::bind( &A::printA, theA, 0.1f, i, i), 1 );
		}
		JobMemory::getInstance()->reset( 0 );
	}
}


void case2(A& theA) {
	for (uint32_t i = 0; i < 5000; i++) {
		ThreadPool::getInstance()->addJob( std::bind( &A::spawn, theA, 0.1f, i, i), 1 ); 
	}

}



int main()
{
	ThreadPool pool;

	A theA;

	case2(theA);
	JobMemory::getInstance()->reset(0);
	case2(theA);

    return 0;
}

