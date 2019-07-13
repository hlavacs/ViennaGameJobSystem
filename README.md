# Vienna Game Engine Job System
The Vienna Game Engine Job System (VGJS) is a C++11 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Work stealing paradigm (lock-free queues are planned to be included soon)
* Directed acyclic graphs (DAGs) are automatically created and recorded
* Recorded DAGs can be replayed, respecting parent-child dependencies, but fully in parallel
* Enables a data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine), which will be ported to run on VGJS.

## Library Usage
VGJS is a single header library that should be included in C++ source files where it is needed:

    #include "GameJobSystem.h"

In one of the C++ files, additionally the following statement should precede the include directive:

    #define IMPLEMENT_GAMEJOBSYSTEM
    #include "GameJobSystem.h"

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs them. If a thread runs out of jobs it will start stealing jobs from other queues (aka work stealing). The library can be run as tool for the main thread that continuously calls the library for running tasks and then waits for the result. However, the main intention is to have a job only system, i.e. there is no main thread, and the whole program is a sequence of jobs that are run, and create other jobs. Typically a program might look like this:

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
        std::string s = std::string(depth, ' ') + "print " + " depth " + std::to_string(depth) + " " +
            std::to_string(loopNumber) + " " + std::to_string((uint32_t)JobSystem::getInstance()->getJobPointer()) + "\n";
        JobSystem::getInstance()->printDebug(s);
    };

    //a global function does not require a class instance when scheduled
    void case1( A& theA, uint32_t loopNumber) {
        JobSystem::getInstance()->printDebug("case 1 number of loops left " + std::to_string(loopNumber) + "\n");
        for (uint32_t i = 0; i < loopNumber; i++) {
            JobSystem::getInstance()->addChildJob(std::bind(&printA, 0, loopNumber), "printA " + std::to_string(i));
        }
    }

    //the main thread starts a child and waits forall jobs to finish by calling wait()
    int main() {
        JobSystem jobsystem(0);

        A theA;

        jobsystem.resetPool(1);
        jobsystem.addJob( std::bind( &case1, theA, 3 ), "case1" );
        jobsystem.wait();

        jobsystem.terminate();
        jobsystem.waitForTermination();

        return 0;
    }

In the above code the main threads schedules the global function case1(), which calls a second global function printA() for loopNumber times. printA() prints out some debug information on the console (printing is serialized by the job system). The main thread waits until all jobs have finished, then terminates the system. This results in the output:

    case 1 number of loops left 3
    print  depth 0 3 2387627984
    print  depth 0 3 2387959776
    print  depth 0 3 2386964448

## Job Pools
The efficiency of the system depends on the peculiar way of how Job structures are allocated from pools.
Each scheduled function internally is shadowed by an instance of the Job class. Instances are allocated from job pools. A job pool has a unique index and a list of memory segments holding Job structures. Each pool has its own index number jobIndex pointing to the next Job structure to allocate, and is simply increased by one upon allocation. A pool theoretically can by made arbitrarily large, but since games and other systems typically have time periods like one frame, one simulation epoch, one physics step, etc., after one such period each pool should simply be reset so that jobIndex points to 0 again, and job structures are reused in the next run. This is done by

    JobSystem::getInstance()->resetPool( poolNumber );

When started VGJS by default contains only pool 0, but pools are automatically created if they are referred to. Pools can be used to handle different tasks like basic management, physics simulation, AI, handling certain data structures etc.

## Adding and Finishing Jobs
Functions can be scheduled by calling addJob() or addChildJob(). As described above, each function that is scheduled is internally represented by a Job structure from one of the pools, pointing also to the function that it represents. Jobs creating other jobs using addChildJob() establish a parent-child relationship. The exception being the main thread. If the main thread calls addChildJob(), then this is equivalent to addJob(), i.e., no parent-child relationship is established for the main thread.

The parent can finish only if all its children have finished. A job that finishes automatically notifies its own parent (if there is one) that one of its children has finished. If this is the last child that finishes, the parent job also finishes. A job can schedule a follow-up job to be executed upon finishing. This establishes a wait-operation, since this follow-up job will be scheduled only of all children have finished. Follow-up jobs are set by calling the onFinishedAddJob() function, and have the same parent as the job that scheduled them.

## Directed Acyclic Graph (DAG)
A DAG describes dependencies amongst jobs. There is a dependency between init1() and func2_1(), if init1() must start running before func2_1(). In fact init1() decides when to start func2_1(), and can carry out work before starting it, and afterwards. However, when starting a child, the child is immediately runnable and does not have to wait for the parent to stop running. Even though init1() eventually returns and stops running, it is not automatically said to have finished. As described above, a job finishes only if it stopped running and all of its children have finished.
As a consequence, in a DAG, since all children have to finish before the parent, the finishing order is reverse to the running order. First all children finish, then the parent.

![](dag.jpg "Example DAG")

The diagram shows dependencies (solid lines) between function calls. A solid fat line means calling addJob() or addChildJob(). For instance main() calls addJob() to schedule init1(). init1() is thus the first function to actually run. init1() calls addChildJob() to schedule func2_1(), ..., funcX_1(), establishing a parent-child relationship. init1() also calls onFinishedAddJob() (represented by a dotted line) to schedule final5(). At this moment, all child functions of init1() can run in parallel to init1()! For example, when func2_1() runs it also creates two children func3_1() and func3_2(), and also calls onFinishedAddJob() to schedule func4_1() as its follow-up job.

After func3_1() and func3_2() have finished, they notify func2_1(), which schedules its follow-up job func4_1() and then finishes. Note that the parent of func4_1() is the same as the one of func2_1(), i.e. init1(). After func4_1() finishes, it therefore notifies init1() that is has finished.

init1() additionally schedule jobs in pools 1 and 2. There is a dependency between init1() and funcX_1(), so only after funcX_2() and funcX_1() have finished, init1() finally finishes and schedules final5(). The main thread can wait either for explicit system termination by calling waitForTermination(), or until there are no more active jobs in the queues by calling wait().

## Recording and Replaying Pools
After a pool is initialized (either after start or after calling resetPool()), it will start recording job DAGs automatically that are scheduled in it. At any time, the jobs of a pool can be replayed by calling playBackPool(), which might result in faster computation since many management tasks will not be redone by VGJS during playback.

Since recording preserves the parent-child relationships and follow-up jobs, this will schedule the recorded jobs to the thread pool, but preserving dependencies. In fact, when replaying a pool, a parent must actually stop running before its children are started.

Playback means that the very first job job[0] in the pool is scheduled, and that this job[0] is a child of the calling job parentJob. So once job[0] finishes, the parent job parentJob will be notified and can also finish. Therefore, playback can only work correctly if there is one and only one starting job job[0], that subsequently schedules child jobs. Scheduling of child jobs can be done across pools, but care must be taken that none of the involved pools is reset during playback, since this might result in unexpected behavior.

It also must be noted that the function calls in playback are exactly the same as at recording time. This is especially true for the function parameters. Any call to addJob(), addChildJob() or onFinishedAddJob() during playback is simply ignored. So in order to make new work instead of just recomputing the old work, functions must deal with pointers to data structures that they work upon. One example is the updating of a scene graph of a scene in a computer game, if the graph has not changed since the last frame. This involves the multiplication of transform matrices in a hierarchical data structure. Transforms of parent objects are multiplied onto the transforms of their children. This can be recorded by calling a function for each object, with a pointer to the object and the object's parent. Replaying then will run the same sequence, and using a pointer to a (global) boolean flag the calls to addChildJob() can even be prevented during plabyack since they would be ignored anyways.

The following shows an example of how pools can be replayed. The main thread schedules the function record(), which first starts a Job in pool 1 calling the memberfunction A::spawn(). On finishing, record() then schedules function playBack(), which plays back pool 1, then reschedules itself for three more times.

    void playBack(A& theA, uint32_t loopNumber) {
        if (loopNumber > 3) return;
        JobSystem::getInstance()->playBackPool(1);

        JobSystem::getInstance()->onFinishedAddJob(std::bind(&playBack, theA, loopNumber + 1),
            "playBack " + std::to_string(loopNumber + 1));
    }

    void record(A& theA, uint32_t loopNumber) {
        JobSystem::getInstance()->addChildJob( std::bind(&A::spawn, theA, 0.1f, loopNumber, 0 ),
            1, "spawn " + std::to_string(loopNumber) );

        JobSystem::getInstance()->onFinishedAddJob( std::bind( &playBack, theA, loopNumber),
            "playBack " + std::to_string(loopNumber) );
    }

    int main()
    {
        JobSystem jobsystem(0);
        A theA;

        jobsystem.resetPool(1);
        jobsystem.addJob( std::bind( &record, theA, 0 ), "record" );
        jobsystem.wait();

        jobsystem.terminate();
        jobsystem.waitForTermination();

        return 0;
    }


## Never use Pointers and References to Local Variables!
It is important to notice that running functions is completely decoupled. When running a parent, its children do not have the guarantee that the parent will continue running during their life time. Instead it is likely that a parent stops running and all its local variables go out of context, while its children are still running. Thus, parents should NEVER pass pointers or references to variables that are LOCAL to them. Instead, in a DAG, everything that is shared amongst jobs and especially passed to children as parameter must be either passed by value, or points or refers to GLOBAL data structures. The only exception here is the main thread, which may pass pointers/references to its own local variables to functions, since they will not go out of context while running.

When sharing global variables that might be changed by several jobs in parallel, e.g. counters counting something up or down, you should consider using std::atomic<> or std::mutex, in order to avoid unpredictable runtime behavior. In a job, never wait for anything for long, use polling instead and finally return. Waiting will block the thread that runs the job and take away overall processing efficiency.

## Data Parallelism instead of Task Parallelism
VGJS enables data parallel thinking since it enables focusing on data structures rather than tasks. The system assumes the use of many global data structures that might or might not need computation. So one use case would be to create a job for each data structure, using the mechanisms of VGJS to honor dependencies between computations. Everything that can run in parallel eventually will, and if the STRUCTURE of the data structures does not change (the VALUES might and should change though), previous computations can be simply replayed, thus speeding up the computations significantly.

## Library Functions
The system supports the following functions API.

    //class constructor
    //threadCount Number of threads to start. If 0 then the number of hardware threads is used.
    //numPools Number of job pools to create upfront
    JobSystem::JobSystem(std::size_t threadCount = 0, uint32_t numPools = 1);

    //returns a pointer to the only instance of the class
    static JobSystem * getInstance();

    //sets a flag to terminate all running threads
    void terminate();

    //returns total number of jobs in the system
    uint32_t getNumberJobs();

    //can be called by the main thread to wait for the completion of all jobs in the system
    //returns as soon as there are no more jobs in the job queues
    void wait();

    //can be called by the main thread to wait for all threads to terminate
    //returns as soon as all threads have exited
    void waitForTermination();

    // returns number of threads in the thread pool
    std::size_t getThreadCount();

    //each thread has a unique index between 0 and numThreads - 1
    //returns index of the thread that is calling this function
    //can e.g. be used for allocating command buffers from command buffer pools in Vulkan
    uint32_t getThreadNumber();

    //wrapper for resetting a job pool in the job memory
    //poolNumber Number of the pool to reset
    void resetPool( uint32_t poolNumber );

    //returns a pointer to the job of the current task
    Job * getJobPointer();

    //this replays all jobs recorded into a pool
    //playPoolNumber Number of the job pool that should be replayed
    void playBackPool( uint32_t playPoolNumber );

    //add a job to a queue
    //pJob Pointer to the job to schedule
    void addJob( Job * pJob );

    //add a job to a queue
    //create a new job in a job pool
    //func The function to schedule
    //poolNumber Optional number of the pool, or 0
    void addJob(Function func, uint32_t poolNumber = 0);

    //add a job to a queue
    //func The function to schedule
    //id A name for the job for debugging
    void addJob(Function func, std::string id);

    //add a job to a queue
    //func The function to schedule
    //poolNumber Optional number of the pool, or 0
    //id A name for the job for debugging
    void addJob(Function func, uint32_t poolNumber, std::string id );

    //create a new child job in a job pool
    //func The function to schedule
    void addChildJob(Function func );

    //create a new child job in a job pool
    //func The function to schedule
    //id A name for the job for debugging
    void addChildJob( Function func, std::string id);

    //create a new child job in a job pool
    //func The function to schedule
    //poolNumber Number of the pool
    //id A name for the job for debugging
    void addChildJob(Function func, uint32_t poolNumber, std::string id );

    //create a successor job for this job, will be added to the queue after
    //the current job finished (i.e. all children have finished)
    //func The function to schedule
    //id A name for the job for debugging
    void onFinishedAddJob(Function func, std::string id );

    //wait for all children to finish and then terminate the pool
    void onFinishedTerminatePool();

    //Print debug information, this is synchronized so that text is not confused on the console
    //s The string to print to the console
    void printDebug(std::string s);
