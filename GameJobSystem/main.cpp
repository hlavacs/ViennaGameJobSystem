
#include <iostream>


#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace gjs;
using namespace std;

class A {
public:
	A() {};
	~A() {};

	void print( float f1, int i1, int i2, Job *pJob ) {
		cout << " " << f1 << " " << i1 << " " << i2 << " " << pJob << "\n";
	};
};



#include <stdlib.h>

int main()
{
	A theA;

	for (uint32_t j = 0; j < 2; j++) {
		for (uint32_t i = 0; i < 20; i++) {
			Job *pJob = JobMemory::getInstance()->allocatePermanentJob();
			pJob->bindTask(&A::print, theA, 0.1f, j, i, pJob);
			ThreadPool::getInstance()->addJob(pJob);

			(*pJob)();

			if (i<10)
				JobMemory::getInstance()->deallocatePermanentJob(pJob);
		}
		JobMemory::getInstance()->reset();
	}

    return 0;
}

