# Vienna Game Job System
The Vienna Game Job System (VGJS) is a C++11 library for parallelizing arbitrary tasks, as for example are typically found in game engines. Important features are:
* Work stealing paradigm (lock-free queues are planned to be included soon)
* Directed acyclic graphs (DAGs) are created automatically and implicitly
* Recorded DAGs can be played

VGJS is a single header library that should be included in C++ source files where it is needed:
    #include "GameJobSystem.h"

In one of the C++ files, additionally the following statement should precede the include directive:
    #define IMPLEMENT_GAMEJOBSYSTEM
    #include "GameJobSystem.h"

VGJS runs a number of n worker threads, each having its own work queue. Each thread grabs jobs entered into its queue and runs it. If a thread runs out of jobs it will start stealing jobs from other queues (aka work stealing). The library can be run as tool for one main thread that continuously calls the library for running tasks and then waits for the result. However, the main intention is to have a job only system, i.e. there is no main thread, and the whole program is a sequence of jobs that are run, and create other jobs.
