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

## Job pools
The efficiency of the system depends on the peculiar way of how Job structures are allocated from pools.
Each scheduled function internally is shadowed by an instance of the Job class. Instances are allocated from job pools. A job pool has a unique index and a list of memory segments holding Job structures. Each pool has its own job index pointing to the next Job structure to allocate, and is simply increased by one upon allocation. A pool theoretically can by made arbitrarily large, but since games and other systems typically have time periods like 1 frame, after one such period each pool should simply be reset so that jobIndex points to 0 again, and job structures are reused in the next run. This is done by

    JobSystem::getInstance()->resetPool( poolNumber );

When started VGJS by default contains only pool 0, but pools are automatically created if they are referred to.

## Adding Jobs
Functions can be scheduled by calling addJob() or addChildJob(). As described above, each function that is scheduled is internally represented by a Job structure from one of the pools, pointing also to the function that it represents. Jobs creating other jobs using addChildJob() establish a parent-child relationship. The exception being the main thread. If the main thread calls addChildJob(), then this is equivalent to addJob(), i.e., no parent-child relationship is established for the main thread.

The parent can finish only if all its children have finished. A job that finishes automatically notifies its own parent (if there is one) that one of its children has finished. If this is the last child that finishes, the parent job also finishes. A job can schedule a follow-up job to be executed upon finishing. This establishes a wait-operation, since this follow-up job will be scheduled only of all children have finished. Follow-up jobs are set by calling the onFinishedAddJob() function, and have the same parent as the job that scheduled them.

## Directed Acyclic Graph (DAG)
A DAG describes dependencies amongst jobs. There is a dependency between init1() and func2_1(), if init1() must start running before func2_1(). Even though init1() eventually returns and stops running, it is not automatically said to have finished. A job finishes only if it stopped running ad all children have finished.
As a consequence, since all children have to finish before the parent, the finishing order is reverse to the running order. First all children finish, then the parent.

![](dag.jpg "Example DAG")

The diagram shows dependencies (all solid lines) between function calls. A solid fat line means calling addJob() or addChildJob(). For instance main() calls addJob() to schedule init1(). init1() is thus the first function to actually run. init1() calls addChildJob() to schedule func2_1(), ..., funcX_1(), establishing a parent-child relationship. init1() also calls onFinishedAddJob() (repersented by a dotted line) to schedule final5(). At this moment, all child functions of init1() can run in parallel to init1()! For example, when func2_1() runs it also creates two children func3_1() and func3_2(), and also calls onFinishedAddJob() to schedule func4_1() as its follow-up job.

After func3_1() and func3_2() have finished, they notify func2_1(), which schedules its follow-up job func4_1() and then finishes. Note that the parent of func4_1() is the parent of func2_1(), i.e. init1(). After func4_1() finishes, it notifies init1().

init1() additionally schedule jobs in pools 1 and 2. There is a dependency between init1() and funcX_1(), so only after funcX_2() and funcX_1() have finished, init1() finally finishes and schedules final5(). The main thread can wait either for explicit system termination, or until there are no more active jobs in the queues.

## Recording and Replaying Pools
After a pool is initialized (either after start or after calling resetPool()), it will start recording job DAGs automatically that are scheduled in it. At any time, the jobs of a pool can be replayed by calling playBackPool(). Since recording preserves the parent-child relationships and follow-up jobs, this will schedule the recorded jobs to the thread pool, but preserving dependencies. Playback means that the very first job in the pool is scheduled, and that this job is a child of the calling job. So once it finishes, the parent job will be notified and can also finish. Therefore, playback can only work correctly if there is one and only one starting job, that subsequently schedules child jobs in the same (!) pool. In the above example, both pool 1 and 2 can be replayed, whereas pool 0 cannot because it has scheduled jobs in other pools. Note that this is not queried, and playing a pool like pool 0 might result in unexpected behavior, since jobs in the other pools in the mean time could be reused and do something completely different.

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
