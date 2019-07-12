# Vienna Game Job System
The Vienna Game Job System (VGJS) is a C++11 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Work stealing paradigm (lock-free queues are planned to be included soon)
* Directed acyclic graphs (DAGs) are created automatically and implicitly
* Recorded DAGs can be re-played, respecting parent-child dependencies, but fully in parallel
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine), which will be ported to run on VGJS.

## Libaray Usage
VGJS is a single header library that should be included in C++ source files where it is needed:

    #include "GameJobSystem.h"

In one of the C++ files, additionally the following statement should precede the include directive:

    #define IMPLEMENT_GAMEJOBSYSTEM
    #include "GameJobSystem.h"

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs them. If a thread runs out of jobs it will start stealing jobs from other queues (aka work stealing). The library can be run as tool for the main thread that continuously calls the library for running tasks and then waits for the result. However, the main intention is to have a job only system, i.e. there is no main thread, and the whole program is a sequence of jobs that are run, and create other jobs. Typically a program might look like this:

    class A {
    public:
        A() {};
        ~A() {};

        //a class member function requires reference/pointer to the instance when scheduled
        void printA( uint32_t number ) {
            JobSystem::getInstance()->printDebug( std::to_string(number) );

            //run a child in the same pool - could also specify a different pool
            //std::bind() parameters for global function: address of the function, function parameters
            JobSystem::addChildJob( std::bind( &printB, number*2 ) );

            //call this after all children have finished
            //std::bind() parameters for memberfunction: address of the function, class instance, function parameters
            JobSystem::getInstance()->onFinishedAddJob( std::bind( &A::end, this, 0 ), "end" );
        };

        //a class member function requires reference/pointer to the instance when scheduled
        void end( uint32_t param ) {
            JobSystem::getInstance()->onFinishedTerminatePool();  
        }
    };

    //a global function does not require a class instance when scheduled
    void printB( uint32_t number ) {
        JobSystem::getInstance()->printDebug( std::to_string(number) );
    };

    int main() {
        JobSystem pool(0); //0 threads means that number of threads = number of CPU supported threads
        A theA;

        //std::bind() parameters for memberfunction: address of the function, class instance, function parameters
        //addJob() parameters: function, number of pool, description of the task as string
        pool.addJob( std::bind( &A::printA, theA, 50 ), 1, "printA" );  
        pool.waitForTermination();
    }

In the above code the main threads runs member function printA() of instance theA in pool 1, then waits for the termination of the pool. Memberfunction printA() first prints out some information on the console (printing is serialized by the job system), then schedules a job printB() that is run after the printA() job is finished. printB() also outputs some information and schedules the function end() of the pool after it is finished. end() schedules a termination of the pool afteer it finishes. After end() is finished, the pool is terminated, and the main thread continues and ends the program.

Each job internally is shadowed by an instance of the Job class. Instances are allocated from job pools. A job pool has a unique index and a list of memory segments holding Job structures. Each pool has its own job index pointing to the next Job structure to allocate, and is simply increased by one upon allocation. A pool theoretically can by made arbitrarily large, but since games and other systems typically have time periods like 1 frame, after one such period each pool can simply be reset so that jobIndex points to 0, and job structures are reused in the next run. This is done by

    JobSystem::getInstance()->resetPool( poolNumber );

When started VGJS by default contains one pool, but pools are automatically created if they are referred to. Each function that is added is internally represented by a Job structure from one of the pools. Jobs scheduling other jobs using addChildJob() establish a parent-child relationship. The parent can finish only if all its children have finished. A job that finishes automatically calls its own onFinished() function. In this function, the job calls its own parent's childFinished() function to notify the parent that one of its children has finished. If this was the last child, the parent job will also finish.
In the onFinished() function, the job can also schedule a follow-up job to be executed. This established a wait-operation, since this follow-up job will be scheduled only of all children have finished.

## Directed Acyclic Graph (DAG)
![](dag.tif "Example DAG")

## Recording and Replaying Pools


## Library Functions
The system supports the following functions:
....
