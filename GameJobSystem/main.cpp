
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>

#include <glm.hpp>

#define IMPLEMENT_GAMEJOBSYSTEM
#include "GameJobSystem.h"


using namespace vgjs;
using namespace std;


//a global function does not require a class instance when scheduled
void printA(int depth, int loopNumber) {
	std::string s = std::string(depth, ' ') + "print " + " depth " + std::to_string(depth) + " " + std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::pInstance->getJobPointer()) + "\n";
	JobSystem::pInstance->printDebug(s);
};


//---------------------------------------------------------------------------------------------------

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

//---------------------------------------------------------------------------------------------------

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


//---------------------------------------------------------------------------------------------------

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



//---------------------------------------------------------------------------------------------------


std::atomic<uint32_t> g_counter = 0;
std::atomic<uint64_t> g_sum = 0;
std::atomic<uint64_t> g_sum2 = 0;
using namespace std::chrono;

double relTime;

void sleep(uint64_t max_count) {
	double sum3 = g_sum2;
	for (uint64_t i = 0; i < max_count; i++) {
		if (i % 2 == 0) {
			sum3 += atan((double)sum3);
		}
		else {
			sum3 -= atan(i*sum3);
		}
	}
	g_sum2 = sum3;
}


void spawn( uint32_t depth, uint32_t workDepth, double sleepTime) {
	g_counter++;
	if (depth % 2 == 0)
		g_sum++;
	else
		g_sum--;
	//JobSystem::pInstance->printDebug( "spawn " + std::to_string(counter) + "\n");
	if (depth > 0 && !JobSystem::pInstance->isPlayedBack()) {
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
		JobSystem::pInstance->addChildJob(std::move(std::bind(&spawn, depth - 1, workDepth, sleepTime)), 1);
	}
	if (depth < workDepth && sleepTime!=0.0) {
		uint64_t loops = sleepTime / relTime;
		double sum3 = g_sum2;
		for (uint64_t i = 0; i < loops; i++) {
			if (i % 2 == 0) {
				sum3 += atan((double)sum3);
			}
			else {
				sum3 -= atan(i*sum3);
			}
		}
		g_sum2 = sum3;
	}
}

void spawn2(uint32_t depth, uint32_t workDepth, double sleepTime ) {
	g_counter++;
	if (depth % 2 == 0)
		g_sum++;
	else
		g_sum--;
	if (depth > 0) {
		spawn2(depth - 1, workDepth, sleepTime);
		spawn2(depth - 1, workDepth, sleepTime);
	}
	if (depth < workDepth && sleepTime!=0.0) {
		uint64_t loops = sleepTime / relTime;
		double sum3 = g_sum2;
		for (uint64_t i = 0; i < loops; i++) {
			if (i % 2 == 0) {
				sum3 += atan((double)sum3);
			}
			else {
				sum3 -= atan(i*sum3);
			}
		}
		g_sum2 = sum3;
	}
}



double minOrMedian(vector<double>& values)
{
	double median;
	std::sort(values.begin(), values.end(), [](double a, double b) {
		return a < b;
	});

	uint32_t size = values.size();
	uint32_t idx = floor(size / 2);
	if ((size % 2) == 0)	{
		median = (values[idx] + values[idx + 1])/2;
	}
	else {
		median = values[idx + 1];
	}

	return values[0];
}


//---------------------------------------------------------------------------------------------------


double singleThread(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	std::vector<double> values;

	for (uint32_t i = 0; i < numberLoops; i++) {
		uint32_t counter2 = g_counter;
		JobSystem::pInstance->resetPool(1);
		t1 = high_resolution_clock::now();
		JobSystem::pInstance->addJob( std::bind(&spawn2, depth, workDepth, sleepTime) , 1);
		JobSystem::pInstance->wait();
		t2 = high_resolution_clock::now();
		time_span = duration_cast<duration<double>>(t2 - t1);
		counter2 = g_counter - counter2;
		//std::cout << "Single Th took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f/counter2 << " us sum " << sum << "\n";
		values.push_back(time_span.count());
	}
	return minOrMedian( values);
}


double warmUp(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	uint32_t counter2 = g_counter;
	t1 = high_resolution_clock::now();
	JobSystem::pInstance->addJob( std::bind(&spawn, depth, workDepth, sleepTime), 1);
	JobSystem::pInstance->wait();
	t2 = high_resolution_clock::now();
	time_span = duration_cast<duration<double>>(t2 - t1);
	counter2 = g_counter - counter2;
	//std::cout << "Warm up   took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f / counter2 << " us sum " << sum << "\n";
	return time_span.count();
}

double work(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;
	std::vector<double> values;

	for (uint32_t i = 0; i < numberLoops; i++) {
		uint32_t counter2 = g_counter;
		JobSystem::pInstance->resetPool(1);
		t1 = high_resolution_clock::now();
		JobSystem::pInstance->addJob( std::bind(&spawn, depth, workDepth, sleepTime), 1);
		JobSystem::pInstance->wait();
		t2 = high_resolution_clock::now();
		time_span = duration_cast<duration<double>>(t2 - t1);
		counter2 = g_counter - counter2;
		//std::cout << "Work      took me " << time_span.count()*1000.0f << " ms counter " << counter2 << " per Call " << time_span.count()*1000000.0f/counter2 << " us sum " << sum << "\n";
		values.push_back(time_span.count());
	}
	return minOrMedian(values);
}

double play(uint32_t numberLoops, uint32_t depth, uint32_t workDepth, double sleepTime) {
	high_resolution_clock::time_point t1, t2;
	duration<double> time_span;

	std::vector<double> values;

	for (uint32_t i = 0; i < numberLoops; i++) {
		uint32_t counter2 = g_counter;
		t1 = high_resolution_clock::now();
		JobSystem::pInstance->playBackPool(1);
		JobSystem::pInstance->wait();
		t2 = high_resolution_clock::now();
		time_span = duration_cast<duration<double>>(t2 - t1);
		counter2 = g_counter - counter2;
		//std::cout << "Play      took me " << time_span.count()*1000.0f << " ms counter2 " << counter << " per Call " << time_span.count()*1000000.0f / counter2 << " us sum " << sum << "\n";
		values.push_back(time_span.count());
	}
	return minOrMedian(values);
}

//---------------------------------------------------------------------------------------------------


void performanceSingle( ) {
	uint32_t statLoops = 10;
	uint32_t numberLoops = 1;
	uint32_t depth = 21;
	uint32_t workDepth = depth + 1;

	double C, W, P;

	g_counter = 0;
	g_sum = 0.0;
	C = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		C += singleThread(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Single Th took avg " << C*1000.0f/statLoops << " ms for " << g_counter << " children (" << 1.0E9*C / g_counter << " ns/child)\n";
	C /= g_counter;

	warmUp(numberLoops, depth, workDepth, 0);

	g_counter = 0;
	g_sum = 0.0;
	W = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		W += work(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Work      took avg " << W*1000.0f/statLoops << " ms for " << g_counter << " children (" << 1000000000.0f*W / g_counter << " ns/child)\n";
	W /= g_counter;

	g_counter = 0;
	g_sum = 0.0;
	P = 0.0;
	for (uint32_t i = 0; i < statLoops; i++) {
		P += play(numberLoops, depth, workDepth, 0);
	}
	std::cout << "Play      took avg " << P*1000.0f/statLoops << " ms for " << g_counter << " children (" << 1000000000.0f*P / g_counter << " ns/child)\n";
	P /= g_counter;
}


//---------------------------------------------------------------------------------------------------


void speedUp( ) {
	uint32_t numberLoops = 20;
	uint32_t depth = 18;
	uint32_t workDepth = depth + 1;

	g_counter = 0;
	g_sum = 0;
	warmUp(2*numberLoops, depth, workDepth, 0);

	std::vector<double> At		 = { 1.0, 10.0, 25.0, 50.0, 75.0, 100.0, 150.0, 250.0, 300, 400, 500, 600.0, 700, 800, 900, 1000 };
	std::vector<uint32_t> depthA = {  18,   18,   18,   18,   18,    18,    18,    17,  17,  17,  17,    16,  16,  16,  16,   16};
	double C = 15e-9;
	uint32_t i = 0;
	for (auto A : At) {
		double ac = A*C;
		g_counter = 0;
		g_sum = 0;
		double Ct = singleThread(numberLoops, depthA[i], depthA[i]+1, ac);
		g_counter = 0;
		g_sum = 0;
		double Wt = work(numberLoops, depthA[i], depthA[i] + 1, ac);
		g_counter = 0;
		g_sum = 0;
		double Pt = play(numberLoops, depthA[i], depthA[i] + 1, ac);
		i++;

		//std::cout << (Ct / Pt) / JobSystem::pInstance->getThreadCount() << ",";
		std::cout << "A " << A << " AC " << ac*1e6 << " us C(At) " << Ct << " s W(At) " << Wt << " s P(At) " << Pt << " s SpeedUp W " << Ct/Wt << " E(W) " << (Ct / Wt) / JobSystem::pInstance->getThreadCount() << " SpeedUp P "<< Ct/Pt <<  " E(P) " << (Ct / Pt) / JobSystem::pInstance->getThreadCount() << "\n";
	}
}


//---------------------------------------------------------------------------------------------------

struct Transform {
	glm::mat4 localTransform;
	glm::mat4 worldTransform;
	Transform() {
		localTransform = glm::mat4(1.0f); 
		worldTransform = localTransform;
	};

	void randomLocalTransform() {
		for (uint32_t i = 0; i < 4; i++) {
			localTransform[i][0] = (std::rand() % 1000) / 500.0 - 1.0f;
			localTransform[i][1] = (std::rand() % 1000) / 500.0 - 1.0f;
			localTransform[i][2] = (std::rand() % 1000) / 500.0 - 1.0f;
			localTransform[i][3] = 0.0f;
		}
		localTransform[3][3] = 1.0f;
	};
};


struct TreeNode {
	bool					visible;
	bool					recompute;
	struct Transform		transform;
	uint32_t				numTotalChildren;
	std::vector<TreeNode*>	children;

	TreeNode() {
		visible = true;
		recompute = true;
		transform.randomLocalTransform();
		numTotalChildren = 0;
	};
};

TreeNode g_rootNode;


void insertTreeNode(  TreeNode *pParent, TreeNode *pNode, float probChild ) {
	bool isChild = ((std::rand() % 1000) / 1000.0) < probChild;

	if ( pParent->children.size()>0 && isChild) {
		insertTreeNode( pParent->children[std::rand() % pParent->children.size()], pNode, probChild);
		return;
	} 
	pParent->children.push_back(pNode);
}

void insertTreeNodes(  uint32_t maxNodes, float probChild) {
	TreeNode *pNode = new TreeNode();
	insertTreeNode( &g_rootNode, pNode, probChild);

	for (uint32_t i = 0; i < maxNodes-1; i++) {
		TreeNode *pChild = new TreeNode();
		insertTreeNode( pNode, pChild, probChild);
	}
}

void deleteNodeAndAllChildren() {

}


using TransformVector = std::vector<Transform>;
TransformVector g_transformArray;

std::vector<TransformVector> g_transformBuckets;



//---------------------------------------------------------------------------------------------------





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
