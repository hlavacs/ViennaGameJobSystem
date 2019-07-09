
#include <iostream>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace gjs;
using namespace std;

class A {
public:
	A() {};
	~A() {};

	void print( float f1, int i1, Job *pJob ) {
		cout << " " << f1 << " " << i1 << " " << pJob << "\n";
	};
};



#include <stdlib.h>

int main()
{
	A theA;

	for (uint32_t i = 0; i < 1000; i++) {
		Job *pJob = JobMemory::getInstance()->allocatePermanentJob();
		pJob->bindTask(&A::print, theA, 0.1f, i, pJob);
		ThreadPool::getInstance()->addJob(pJob);

		(*pJob)();

		if( std::rand() % 100 < 50)
			JobMemory::getInstance()->deallocatePermanentJob(pJob);
	}

    return 0;
}

