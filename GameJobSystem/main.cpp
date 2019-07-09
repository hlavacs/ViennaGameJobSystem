
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

int main()
{
	A theA;

	Job *pJob = JobMemory::getInstance()->allocateJob();
	pJob->bindTask(&A::print, theA, 0.1f, 45, pJob);
	ThreadPool::getInstance()->addJob(pJob);

	(*pJob)();

    return 0;
}

