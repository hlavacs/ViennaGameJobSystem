
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
std::atomic<uint64_t> sum = 0;
std::atomic<uint64_t> sum2 = 0;
using namespace std::chrono;

double relTime;

void sleep(uint64_t max_count) {
	for (uint64_t i = 0; i < max_count; i++) {
		if (i % 2 == 0) {
			sum2 += atan((double)sum2);
		}
		else {
			sum2 -= atan(i*sum2);
		}
	}
}


void spawn( uint32_t depth, uint32_t workDepth, double sleepTime) {
	counter++;
	if (depth % 2 == 0)
		sum++;
	else
		sum--;
	//JobSystem::pInstance->printDebug( "spawn " + std::to_string(counter) + "\n");
	if (depth > 0 && !JobSystem::pInstance->isPlayedBack()) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
	}
	if (depth < workDepth && sleepTime!=0.0) {
		uint64_t loops = sleepTime / relTime;
		double sum3 = sum2;
		for (uint64_t i = 0; i < loops; i++) {
			if (i % 2 == 0) {
				sum3 += atan((double)sum3);
			}
			else {
				sum3 -= atan(i*sum3);
			}
		}
		sum2 = sum3;
	}
}

void spawn2(uint32_t depth, uint32_t workDepth, double sleepTime ) {
	counter++;
	if (depth % 2 == 0)
		sum++;
	else
		sum--;
	if (depth > 0) {
		spawn2(depth - 1, workDepth, sleepTime);
		spawn2(depth - 1, workDepth, sleepTime);
	}
	if (depth < workDepth && sleepTime!=0.0) {
		uint64_t loops = sleepTime / relTime;
		double sum3 = sum2;
		for (uint64_t i = 0; i < loops; i++) {
			if (i % 2 == 0) {
				sum3 += atan((double)sum3);
			}
			else {
				sum3 -= atan(i*sum3);
			}
		}
		sum2 = sum3;
	}
}


void loop(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	for (uint32_t i = 0; i < numberLoops; i++) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth, workDepth, sleepTime)), 1);
	}
}

void loopSingle(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	for (uint32_t i = 0; i < numberLoops; i++) {
		spawn2( depth, workDepth, sleepTime);
	}
}


double singleThread(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	uint32_t counter2 = counter;
	JobSystem::pInstance->resetPool(1);
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob(std::move(std::bind(&loopSingle, numberLoops, depth, workDepth, sleepTime)), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	counter2 = counter - counter2;
	std::cout << "Single Th took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f/counter2 << " us sum " << sum << "\n";
	return time_span.count();
}


double warmUp(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	uint32_t counter2 = counter;
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob(std::move(std::bind(&loop, numberLoops, depth, workDepth, sleepTime)), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	counter2 = counter - counter2;
	std::cout << "Warm up   took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f / counter2 << " us sum " << sum << "\n";
	return time_span.count();
}

double work(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	uint32_t counter2 = counter;
	JobSystem::pInstance->resetPool(1);
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob(std::move(std::bind(&loop, numberLoops, depth, workDepth, sleepTime)), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	counter2 = counter - counter2;
	std::cout << "Work      took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f/counter2 << " us sum " << sum << "\n";
	return time_span.count();
}

double play(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	uint32_t counter2 = counter;
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->playBackPool(1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	counter2 = counter - counter2;
	std::cout << "Play      took me " << time_span.count()*1000.0f << " ms counter2 " << counter << " per Call " << time_span.count()*1000000.0f / counter2 << " us sum " << sum << "\n";
	return time_span.count();
}


void performanceSingle( ) {
	uint32_t statLoops = 10;
	uint32_t numberLoops = 1;
	uint32_t depth = 21;
	uint32_t workDepth = depth + 1;

	double C, W, P;

	counter = 0;
	sum = 0.0;
	C = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		C += singleThread(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Single Th took avg " << C*1000.0f/statLoops << " ms for " << counter << " children (" << 1.0E9*C / counter << " ns/child)\n";
	C /= counter;

	warmUp(numberLoops, depth, workDepth, 0);

	counter = 0;
	sum = 0.0;
	W = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		W += work(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Work      took avg " << W*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*W / counter << " ns/child)\n";
	W /= counter;

	counter = 0;
	sum = 0.0;
	P = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		P += play(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Play      took avg " << P*1000.0f/statLoops << " ms for " << counter << " children (" << 1000000000.0f*P / counter << " ns/child)\n";
	P /= counter;
}



void speedUp( ) {
	uint32_t numberLoops = 1;
	uint32_t depth = 18;
	uint32_t workDepth = depth + 1;

	counter = 0;
	sum = 0;
	warmUp(2*numberLoops, depth, workDepth, 0);

	double fac = 1.0E-6;
	std::vector<double> A = { 0.0, 0.1, 0.25, 0.5, 0.75, 1.0, 2.5, 5.0, 7.5, 10.0 };

	for (auto a : A) {
		a = a*fac;
		counter = 0;
		sum = 0;
		double Ct = singleThread(numberLoops, depth, workDepth, a);
		counter = 0;
		sum = 0;
		double Wt = work(numberLoops, depth, workDepth, a);
		counter = 0;
		sum = 0;
		double Pt = play(numberLoops, depth, workDepth, a);

		std::cout << "A " << a*1000000.0 << " us C(At) " << Ct << " W(At) " << Wt << " P(At) " << Pt << " SpeedUp W " << Ct/Wt << " E(W) " << (Ct / Wt) / JobSystem::pInstance->getThreadCount() << " SpeedUp P "<< Ct/Pt <<  " E(P) " << (Ct / Pt) / JobSystem::pInstance->getThreadCount() << "\n";
	}
}


//the main thread starts a child and waits forall jobs to finish by calling wait()
int main()
{

	uint64_t steps = 100000000;
	high_resolution_clock::time_point t1, t2;
	t1 = high_resolution_clock::now();
	sleep( steps );
	t2 = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
	relTime = time_span.count() / steps;
	std::cout << relTime << "\n";

	JobSystem jobsystem(0);
	A theA;
	//jobsystem.addJob( std::bind( &case1, theA, 3 ), "case1" );
	//jobsystem.addJob( std::bind( &case2, theA, 3 ), "case2");
	//jobsystem.addJob( std::bind( &record, theA, 3 ), "record");

	//performanceSingle();

	speedUp( );

	jobsystem.wait();
	jobsystem.terminate();
	jobsystem.waitForTermination();

	return 0;
}
