
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

	void printA( float f1, int i1, int i2, Job *pJob ) {
		cout << "print " << f1 << " " << i1 << " " << i2 << " " << pJob << "\n";
	};

	void spawn(float f1, int i1, int i2, Job *pJob) {
		cout << "spawn " << f1 << " " << i1 << " " << i2 << " " << pJob << "\n";

		Job *pChildJob = JobMemory::getInstance()->allocateTransientJob( pJob );
		pChildJob->bindTask(&A::printA, this, 0.1f, i1, i1, pChildJob);
		ThreadPool::getInstance()->addJob(pChildJob);

		(*pChildJob)();
	};

};


void case1( A& theA) {
	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < 20; i++) {

			Job *pJob = JobMemory::getInstance()->allocatePermanentJob();
			pJob->bindTask(&A::printA, theA, 0.1f, j, i, pJob);
			ThreadPool::getInstance()->addJob(pJob);

			(*pJob)();

			if (i<10)
				JobMemory::getInstance()->deallocatePermanentJob(pJob);
		}
		JobMemory::getInstance()->reset();
	}
}


void case2(A& theA) {

	for (uint32_t i = 0; i < 20; i++) {

		Job *pJob = JobMemory::getInstance()->allocateTransientJob();
		pJob->bindTask(&A::spawn, theA, 0.1f, i, i, pJob);
		ThreadPool::getInstance()->addJob(pJob);

		(*pJob)();
	}

}



int main()
{
	A theA;

	case2(theA);

    return 0;
}

