# Vienna Game Engine Job System
The Vienna Game Engine Job System (VGJS) is a C++20 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Full support for C++20 coroutines
* Can be run with coroutines (better convenience) or without (better performance)
* Enables a data oriented, data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine) implementing a game engine using the VGJS

## Library Usage
VGJS is a 2-header library that should be included in C++ source files where it is needed:

    #include "VEGameJobSystem.h"

If you additionally want coroutines then include

    #include "VECoro.h"

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs them. Additionally there is one central queue, and workers try to grab jobs from it concurrently.

## Using the Job system
The job system is started by accessing its singleton pointer. See the main() function provided:

	using namespace vgjs;

    void printData(int i) {
        std::cout << "Print Data " << i << std::endl;
	}

	void loop( int N ) {
		for( int i=0; i< N; ++i>) {
			schedule( [=](){ printData(i); } );	//all jobs are scheduled to run in parallel
		}

		continuation( terminate() );	//after all children have finished, this function will be scheduled
	}

    int main()
    {
    	JobSystem::instance();		//create the job system, start as many threads as there are hardware threads
		schedule( F(loop(10)) ); 	//Macros F() or FUNCTION() pack loop(10) into a lambda [=](){ loop(10); }
		wait_for_termination();		//wait for the last thread to terminate
    	return 0;
    }



## Functions



## Coroutines

## Scheduling Jobs

## Enforcing Specific Threads

## Dependencies

## Never use Pointers and References to Local Variables in Functions!
It is important to notice that running Functions is completely decoupled. When running a parent, its children do not have the guarantee that the parent will continue running during their life time. Instead it is likely that a parent stops running and all its local variables go out of context, while its children are still running. Thus, parent Functions should NEVER pass pointers or references to variables that are LOCAL to them. Instead, in the dependency tree, everything that is shared amongst jobs and especially passed to children as parameter must be either passed by value, or points or refers to GLOBAL data structures or heaps.

When sharing global variables in Functions that might be changed by several jobs in parallel, e.g. counters counting something up or down, you should consider using std::atomic<> or std::mutex, in order to avoid unpredictable runtime behavior. In a job, never wait for anything for long, use polling instead and finally return. Waiting will block the thread that runs the job and take away overall processing efficiency.

This does NOT apply to coroutines, since coroutines do not go out of context when running children. So coroutines CAN pass references or pointers to local varaiables!

## Data Parallelism instead of Task Parallelism
VGJS enables data parallel thinking since it enables focusing on data structures rather than tasks. The system assumes the use of many data structures that might or might not need computation. Data structures can be either global, or are organized as data streams that flow from one system to another system and get transformed in the process.

## Performance and Granularity
Since the VGJS incurs some overhead, jobs should not bee too small in order to enable some speedup. Depending on the CPU, job sizes in te order
of 1-2 us seem to be enough to result in noticable speedups on a 4 core Intel i7 with 8 hardware threads. Smaller job sizes are course
possible but should not occur too often.

## Logging Jobs


