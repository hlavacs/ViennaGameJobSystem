
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



void spawn( uint32_t depth, uint32_t workDepth, double sleepTime) {
	counter++;
	//JobSystem::pInstance->printDebug( "spawn " + std::to_string(counter) + "\n");
	if (depth > 0 && !JobSystem::pInstance->isPlayedBack()) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
	}
	if (depth < workDepth) {
		high_resolution_clock::time_point t1, t2;
		t1 = high_resolution_clock::now();
		do {
			t2 = high_resolution_clock::now();
		} while ((double)duration_cast<duration<double>>(t2 - t1).count() < sleepTime);
	}
}

void spawn2(uint32_t depth, uint32_t workDepth, double sleepTime ) {
	counter++;
	if (depth > 0) {
		spawn2(depth - 1, workDepth, sleepTime);
		spawn2(depth - 1, workDepth, sleepTime);
	}
	if (depth < workDepth) {
		high_resolution_clock::time_point t1, t2;
		t1 = high_resolution_clock::now();
		do {
			t2 = high_resolution_clock::now();
		} while ((double)duration_cast<duration<double>>(t2 - t1).count() < sleepTime);
	}
}


void spawn2WO(uint32_t depth, uint32_t workDepth, double sleepTime) {
	counter++;
	if (depth > 0) {
		spawn2(depth - 1, workDepth, sleepTime);
		spawn2(depth - 1, workDepth, sleepTime);
	}
}

void loop(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	for (uint32_t i = 0; i < numberLoops; i++) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth, workDepth, sleepTime)), 1);
	}
}


double singleThread(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	t1 = high_resolution_clock::now();
	for (uint32_t i = 0; i < numberLoops; i++) {
		spawn2(depth, workDepth, sleepTime);
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Single Th took me " << time_span.count()*1000.0f << " ms counter " << counter << "\n";
	return time_span.count();
}


double singleThreadWO(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	t1 = high_resolution_clock::now();
	for (uint32_t i = 0; i < numberLoops; i++) {
		spawn2WO(depth, workDepth, sleepTime);
	}
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Single WO took me " << time_span.count()*1000.0f << " ms counter " << counter << "\n";
	return time_span.count();
}


double warmUp(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	JobSystem::pInstance->resetPool(1);
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob(std::move(std::bind(&loop, numberLoops, depth, workDepth, sleepTime)), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Warm up   took me " << time_span.count()*1000.0f << " ms counter " << counter << "\n";
	return time_span.count();
}

double work(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	JobSystem::pInstance->resetPool(1);
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob(std::move(std::bind(&loop, numberLoops, depth, workDepth, sleepTime)), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Work      took me " << time_span.count()*1000.0f << " ms counter " << counter << "\n";
	return time_span.count();
}

double play(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	t1 = high_resolution_clock::now();
	JobSystem::pInstance->playBackPool(1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	std::cout << "Play      took me " << time_span.count()*1000.0f << " ms counter " << counter << "\n";
	return time_span.count();
}


void performanceSingle( double &C, double &W, double &P) {
	uint32_t statLoops = 10;
	uint32_t numberLoops = 1;
	uint32_t depth = 21;
	uint32_t workDepth = 21;

	counter = 0;
	C = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		C += singleThread(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Single Th took avg " << C*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*C / counter << " ns/child)\n";
	C /= counter;

	counter = 0;
	double CWO = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		CWO += singleThreadWO(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Single WO took avg " << CWO*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*CWO / counter << " ns/child)\n";
	CWO /= counter;

	warmUp(numberLoops, depth, workDepth, 0);

	counter = 0;
	W = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		W += work(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Work      took avg " << W*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*W / counter << " ns/child)\n";
	W /= counter;

	counter = 0;
	P = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		P += play(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Play      took avg " << P*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*P / counter << " ns/child)\n";
	P /= counter;
}



void speedUp( ) {
	uint32_t numberLoops = 200;
	uint32_t depth = 10;
	uint32_t workDepth = 10;

	warmUp(numberLoops, depth, workDepth, 0);

	double C = singleThread(numberLoops, depth, workDepth, 0);
	C = C / counter;

	std::vector<double> A = {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0 };

	for (auto a : A) {
		double At = a*C;
		counter = 0;
		double Ct = singleThread(numberLoops, depth, workDepth, At);
		counter = 0;
		double Wt = work(numberLoops, depth, workDepth, At);
		counter = 0;
		double Pt = play(numberLoops, depth, workDepth, At);

		std::cout << "C " << C*1000000.0 << " us A " << a << " C(At) " << Ct << " W(At) " << Wt << " P(At) " << Pt << " SpeedUp W " << Ct/Wt << " SpeedUp P "<< Ct/Pt <<  "\n";
	}
}


//the main thread starts a child and waits forall jobs to finish by calling wait()
int main()
{
	JobSystem jobsystem(0);

	A theA;
	//jobsystem.addJob( std::bind( &case1, theA, 3 ), "case1" );
	//jobsystem.addJob( std::bind( &case2, theA, 3 ), "case2");
	//jobsystem.addJob( std::bind( &record, theA, 3 ), "record");
	double C, W, P;
	//performanceSingle( C, W, P );

	speedUp();

	jobsystem.wait();
	jobsystem.terminate();
	jobsystem.waitForTermination();

	return 0;
}
