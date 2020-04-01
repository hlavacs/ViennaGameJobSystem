# Vienna Game Engine Job System
The Vienna Game Engine Job System (VGJS) is a C++17 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Work stealing paradigm, lock-free queues
* Enables dynamic dependencies, polling
* Enables a data oriented, data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine) implementing a game engine using the VGJS

## Library Usage
VGJS is a single header library that should be included in C++ source files where it is needed:

    #define VE_ENABLE_MULTITHREADING
    #include "VEUtilClock.h"
    #include "VEGameJobSystem.h"

In one of the cpp files, additionally the following statement should precede the include directive:

    #define VE_IMPLEMENT_GAMEJOBSYSTEM

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs them. If a thread runs out of jobs it will start stealing jobs from other queues (aka work stealing). The library can be run as tool for the main thread that continuously calls the library for running tasks and then waits for the result. However, the main intention is to have a job only system, i.e. there is no main thread, and the whole program is a sequence of jobs that are run, and create other jobs.

## Macros
At the start the library defines a number of macros. If VE_ENABLE_MULTITHREADING is not defined, then the macros are actually empty, and the whole program runs in singlethread mode. The macros are:

#define JIDX vgjs::JobSystem::getInstance()->getThreadIndex()

    /**
    * \brief Add a function as a job to the jobsystem.
    *
    * In multithreaded operations, this macro adds a function as job to the job system.
    * In singlethreaded mode, this macro simply calls the function.
    *
    * \param[in] f The function to be added or called.
    */
    #define JADD( f )	vgjs::JobSystem::getInstance()->addJob( [=](){ f; } )

    /**
    * \brief Add a dependent job to the jobsystem. The job will run only after all previous jobs have ended.
    *
    * In multithreaded operations, this macro adds a function as job to the job system after all other child jobs have finished.
    * In singlethreaded mode, this macro simply calls the function.
    *
    * \param[in] f The function to be added or called.
    */
    #define JDEP( f )	vgjs::JobSystem::getInstance()->onFinishedAddJob( [=](){ f; } )

    /**
    * \brief Add a job to the jobsystem, schedule to a particular thread.
    *
    * In multithreaded operations, this macro adds a function as job to the job system and schedule it to the given thread.
    * In singlethreaded mode, this macro simply calls the function.
    *
    * \param[in] f The function to be added or called.
    * \param[in] t The thread that the job should go to.
    */
    #define JADDT( f, t )	vgjs::JobSystem::getInstance()->addJob( [=](){ f; }, t )

    /**
    * \brief Add a dependent job to the jobsystem, schedule to a particular thread. The job will run only after all previous jobs have ended.
    *
    * In multithreaded operations, this macro adds a function as job to the job system and schedule it to the given thread.
    * The job will run after all other child jobs have finished.
    * In singlethreaded mode, this macro simply calls the function.
    *
    * \param[in] f The function to be added or called.
    * \param[in] t The thread that the job should go to.
    */
    #define JDEPT( f, t )	vgjs::JobSystem::getInstance()->onFinishedAddJob( [=](){ f; }, t )

    /**
    * \brief After the job has finished, reschedule it.
    */
    #define JREP vgjs::JobSystem::getInstance()->onFinishedRepeatJob()

    /**
    * \brief Wait for all jobs in the job system to have finished and that there are no more jobs.
    */
    #define JWAIT vgjs::JobSystem::getInstance()->wait()

    /**
    * \brief Reset the memory for allocating jobs. Should be done once every game loop.
    */
    #define JRESET vgjs::JobSystem::getInstance()->resetPool()

    /**
    * \brief Tell all threads to end. The main thread will return to the point where it entered the job system.
    */
    #define JTERM vgjs::JobSystem::getInstance()->terminate()

    /**
    * \brief Wait for all threads to have terminated.
    */
    #define JWAITTERM vgjs::JobSystem::getInstance()->waitForTermination()

    /**
  	* \brief A wrapper over return, is empty in singlethreaded use
  	*/
    #define JRET return

## Using the Job system
The job system is started by accessing its singleton pointer. See the main() function provided:

    int main()
    {
    	init();

    	#ifdef VE_ENABLE_MULTITHREADING
    	vgjs::JobSystem::getInstance(0, 1);					//create pool without thread 0
    	#endif

    	JADD(runGameLoop());								//schedule the game loop

    	#ifdef VE_ENABLE_MULTITHREADING
    	vgjs::JobSystem::getInstance()->threadTask(0);		//put main thread as first thread into pool
    	JWAITTERM;
    	#endif

    	return 0;
    }

The first parameter of the first call to vgjs::JobSystem::getInstance is the number of threads to be spawned. If this is 0 then
the number of available hardware threads is used (either number of CPU cores, or 2x is SMT is used). The second parameter is either
0 or 1 and denotes the startindex of the new threads. Use 0 if the main threads does not join the system. Use 1 if the
main thread will join the job system.
In the example given, the main threads schedules the first job - the game loop - and then joins the job system.

    ///the main game loop
    void runGameLoop() {
    	while (go_on) {
    		JRESET;							//reset the thread pool!!!
    		JADDT(computeOneFrame2(0),0);	//run on main thread for GUI polling!
    		JREP;							//repeat the loop
    		JRET;							//if multithreading, return, else stay in loop
    	}
    	JTERM;
    }

The main game loop is simply a while() loop that continuously calls computeOneFrame2(). Note that this child is always
scheduled to task 0 since some GUIs like GLFW must be polled through the main thread.


## Adding and Finishing Jobs
Functions can be scheduled by calling JADD(). Each function that is scheduled is internally represented by a Job structure, pointing also to the function that it represents. Jobs creating other jobs using JADD() establish a parent-child relationship.

The parent can finish only if all its children have finished. A job that finishes automatically notifies its own parent that one of its children has finished. If the parent has exited its funciton and this is the last child that finishes, the parent job also finishes. A job can schedule a follow-up job to be executed upon finishing by usig JDEP(). This establishes a wait-operation, since this follow-up job will be scheduled only of all children have finished. Follow-up jobs have the same parent as the job that scheduled them. Thus their parents must aso wait for all such follow-up jobs have finished.

Parents waiting for jobs implicitly span a dependecy graph, of jobs depending on subtrees of jobs. This easily anables any kind of dependency structure to be created just by calling JADD() and JDEP().

## Enforcing Specific Threads



## Never use Pointers and References to Local Variables!
It is important to notice that running functions is completely decoupled. When running a parent, its children do not have the guarantee that the parent will continue running during their life time. Instead it is likely that a parent stops running and all its local variables go out of context, while its children are still running. Thus, parents should NEVER pass pointers or references to variables that are LOCAL to them. Instead, in a DAG, everything that is shared amongst jobs and especially passed to children as parameter must be either passed by value, or points or refers to GLOBAL data structures or heaps.

When sharing global variables that might be changed by several jobs in parallel, e.g. counters counting something up or down, you should consider using std::atomic<> or std::mutex, in order to avoid unpredictable runtime behavior. In a job, never wait for anything for long, use polling instead and finally return. Waiting will block the thread that runs the job and take away overall processing efficiency.

## Data Parallelism instead of Task Parallelism
VGJS enables data parallel thinking since it enables focusing on data structures rather than tasks. The system assumes the use of many global data structures that might or might not need computation. So one use case would be to create a job for each data structure, using the mechanisms of VGJS to honor dependencies between computations. Everything that can run in parallel eventually will, and if the STRUCTURE of the data structures does not change (the VALUES might and should change though), previous computations can be simply replayed, thus speeding up the computations significantly.
