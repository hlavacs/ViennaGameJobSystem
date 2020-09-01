# Vienna Game Engine Job System
The Vienna Game Engine Job System (VGJS) is a C++17 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Lock-free operations
* Full support for C++20 coroutines
* Can be run with coroutines (better convenience) or without (better performance) them 
* Enables a data oriented, data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine) implementing a game engine using the VGJS

## Library Usage
VGJS is a 2-header library that should be included in C++ source files where it is needed:

    #include "VEGameJobSystem.h"

If you additionally want coroutines then include

    #include "VETask.h"

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs them. Additionally there is one central queue, and workers try to grab jobs from it concurrently.

## Using the Job system
The job system is started by accessing its singleton pointer. See the main() function provided:



    int main()
    {
    	init();

    	vgjs::JobSystem::instance(0, 1);   //create pool without thread 0

    	JADD(runGameLoop());               //schedule the game loop

    	#ifdef VE_ENABLE_MULTITHREADING
    	vgjs::JobSystem::getInstance()->threadTask(0);  //put main thread as first thread into pool
    	JWAITTERM;
    	#endif

    	return 0;
    }


## Adding and Finishing Jobs
Functions can be scheduled by calling JADD(). Each function that is scheduled is internally represented by a Job structure, pointing also to the function that it represents. Jobs creating other jobs using JADD() establish a parent-child relationship.

The parent can finish only if all its children have finished. A job that finishes automatically notifies its own parent that one of its children has finished. If the parent has exited its function and this is the last child that finishes, the parent job also finishes. 

A job can schedule a follow-up job to be executed upon finishing by using JDEP(). This establishes a wait-operation, since this follow-up job will be scheduled only of all children have finished. Follow-up jobs have the same parent as the job that scheduled them. Thus their parents must aso wait for all such follow-up jobs have finished.

Parents waiting for jobs implicitly span a dependecy graph, of jobs depending on subtrees of jobs. This easily anables any kind of dependency structure to be created just by calling JADD() and JDEP().

## Enforcing Specific Threads
The macros JADDT() and JDEPT() enable a second parameter that lets users specify the thread that the job should go to.
Some systems like GLFW need to be handled by the main thread, and thus e.g. JADDT( updateGLFW(), 0 ); schedules the function updateGLFW() to run on
thread 0.

## Dependencies
JDEP() or JDEPT() emable to schedule jobs after other jobs have finished. Since these are seperate function calls, the previous job must return before the follow-ups can run. Follow-up functions can be different functions than the caller, or the same function can be rescheduled as follow-up. However, this requires some gotos at the start to differentiate between different stages. Essentially, this way the functionality of co-routines can be emulated.
Consider as an exampe this function:

    constexpr uint32_t epoch_duration = 1000000 / 60;                                       //1/60 seconds
    duration<int, std::micro> time_delta = duration<int, std::micro>{ epoch_duration };	//the duration of an epoch
    time_point<high_resolution_clock> now_time = high_resolution_clock::now();              //now time
    time_point<high_resolution_clock> current_update_time = now_time;                      //start of the current epoch
    time_point<high_resolution_clock> next_update_time = current_update_time + time_delta; //end of the current epoch
    time_point<high_resolution_clock> reached_time = current_update_time;                 //time the simulation has reached

    //acts like a co-routine
    void computeOneFrame(uint32_t step) {

    	if (step == 1) goto step1;
    	if (step == 2) goto step2;
    	if (step == 3) goto step3;
    	if (step == 4) goto step4;

    	now_time = std::chrono::high_resolution_clock::now();

    	if (now_time < next_update_time) {		//still in the same time epoch
    		return;
    	}

    step1:
    	forwardTime();
    	JDEP(computeOneFrame(2));		//wait for finishing, then do step2
    	return;

    step2:
    	swapTables();
    	JDEP(computeOneFrame(3));		//wait for finishing, then do step3
    	return;

    step3:
    	updateClock.start();
    	update();
    	JDEP(updateClock.stop();  computeOneFrame(4));		//wait for finishing, then do step4
    	return;

    step4:
    	reached_time = next_update_time;

    	if (now_time > next_update_time) {	//if now is not reached yet
    		JDEP(computeOneFrame(1); );	//move one epoch further
    	}
    }

This function drives the whole game functionality. Time is divided into slots called epochs. Epochs should mimic e.g. monitor frequencies like 60 Hz.
Game state is only changed at epoch borders, epochs start at current_update_time and end at next_update_time.
So once a new epoch is entered because "now" (now_time) is inbetween some current_update_time and next_update_time, the game state for the next time point next_update_time is computed as a function of the state at current_update_time, and all inputs that occured before current_update_time.
In parallel, the renderer can begin drawing the next frame as soon as its state has been computed.

The above example shows how to use VGJS to emulate the functionlity of co-routines. The function computeOneFrame() keeps scheulding itself as
follow-up job (of itself). The parameter step determines where it should pick up its operations. In singlethreaded operations, the function simply calls itself recursively.

Not also how the clock is used in the update step to measure the time. JADD() and JDEP() can accept arbitrary sequences of functions, since they
put everything into a lambda function that is eventually scheduled as job.

## Never use Pointers and References to Local Variables!
It is important to notice that running functions is completely decoupled. When running a parent, its children do not have the guarantee that the parent will continue running during their life time. Instead it is likely that a parent stops running and all its local variables go out of context, while its children are still running. Thus, parents should NEVER pass pointers or references to variables that are LOCAL to them. Instead, in the dependency tree, everything that is shared amongst jobs and especially passed to children as parameter must be either passed by value, or points or refers to GLOBAL data structures or heaps.

When sharing global variables that might be changed by several jobs in parallel, e.g. counters counting something up or down, you should consider using std::atomic<> or std::mutex, in order to avoid unpredictable runtime behavior. In a job, never wait for anything for long, use polling instead and finally return. Waiting will block the thread that runs the job and take away overall processing efficiency.

## Data Parallelism instead of Task Parallelism
VGJS enables data parallel thinking since it enables focusing on data structures rather than tasks. The system assumes the use of many data structures that might or might not need computation. Data structures can be either global, or are organized as data streams that flow from one system
to another system and get transformed in the process.

## Performance and Granularity
Since the VGJS incurs some overhead, jobs should not bee too small in order to enable some speedup. Depending on the CPU, job sizes in te order
of 1-2 us seem to be enough to result in noticable speedups on a 4 core Intel i7 with 8 hardware threads. Smaller job sizes are course
possible but should not occur too often.

## Lockfree Operations
Lockfree operations can be enforced by defining a specific thread for specific data structures.
So every time someone wnats to write to a data structure, the job is scheduled to this specific thread, and all write operations
get sequentialized inside this thread. If several structures should be written to, then the co-routine functionality can be exploited to progress writes and transport intermediary results between the writes.

Reading can be enforced by defining all state at current_update_time to be readonly, and state at next_update_time to be writeonly.
State is thus present in two versions, and every time the epoch is progressed, the write state is copied to the read state.
In the above example this is symbolized by the function swapTables().
