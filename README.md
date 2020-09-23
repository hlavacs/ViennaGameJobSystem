# Vienna Game Engine Job System
The Vienna Game Engine Job System (VGJS) is a C++20 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science. Important features are:
* Supports C++20 (coroutines, concepts, polymorphic allocators and memory resources, ...)
* Can be run with coroutines (better convenience), without (better performance), or a mixture
* Enables a data oriented, data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine) implementing a game engine using the VGJS

## Library Usage
VGJS is a 2-header library that should be included in C++ source files where it is needed:

    #include "VEGameJobSystem.h"

If you additionally want coroutines then include

    #include "VECoro.h"

VGJS runs a number of N worker threads, EACH having TWO work queues, a local queue and a global queue. When scheduling jobs, a target thread can be specified or not. If the job is specified to run om thread K, then the job is put into thread K's LOCAL queue. Only thread K can take it from there. If no thread is specified or -1 is chosen, then a random thread J is chosen and the job is inserted into thread J's GLOBAL queue. Any thread can get it from there, if it runs out of local jobs. This paradigm is called work stealing. By using multple global queues, the amount of contention between threads is minimized.

Each thread grabs jobs entered into one of its queues and runs them. 

## Using the Job system
The job system is started by accessing its singleton pointer. See the main() function provided:

    #include "VEGameJobSystem.h"
    #include "VECoro.h"

	using namespace vgjs;

    void printData(int i) {
        std::cout << "Print Data " << i << std::endl;
	}

	void loop( int N ) {
		for( int i=0; i< N; ++i>) {
			schedule( [=](){ printData(i); } );	//all jobs are scheduled to run in parallel
		}

		//after all children have finished, this function will be scheduled to thread 0
		continuation( Function{ F(terminate()), 0 } );	
	}

    int main()
    {
    	JobSystem::instance();		//create the job system, start as many threads as there are hardware threads
		schedule( F(loop(10)) ); 	//Macros F() or FUNCTION() pack loop(10) into a lambda [=](){ loop(10); }
		wait_for_termination();		//wait for the last thread to terminate
    	return 0;
    }

In the above example we see the three main possibilies to schedule C++ functions: using the macros F() or FUNCTION(), using a lambda function [=](){} (use '=' for copying the parameters!), or using the class Function{}, which enables to pass on more parameters (see below). 
First a single function loop(10) is scheduled to run as a first job. Then the main thread waits for the system to terminate. Funtion loop(10) starts 10 jobs which simply print out numbers. Then it sets a continuation for itself. Only functions running as jobs can do this. So, neither the main() function, nor a coroutine may call this function (actually such a call would simply be ignored.)

The call to JobSystem::instance() first creates the job system, and afterwards retrieves a pointer to its singleton instance. It accepts three parameters, which can be provided or not. They are only used when the system is created:

  	/**
    * \brief JobSystem class constructor
    * \param[in] threadCount Number of threads in the system
    * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
    * \param[in] mr The memory resource to use for allocating Jobs
    */
    JobSystem(	uint32_t threadCount = 0, uint32_t start_idx = 0, 
				std::pmr::memory_resource *mr = std::pmr::new_delete_resource() ) noexcept

If threadCount = 0 then the number of threads to start is given be the call std::thread::hardware_concurrency(), which gives the number of hardware threads, NOT CPU cores. On modern hyperthreading architectures, the hardware concurrency is typicall twice the number of CPU cores.

If the second parameter start_idx is not 0, then the main thread should enter the job system as thread 0 instead of waiting for its termination:

    int main()
    {
    	JobSystem::instance(0, 1);	//start only N-1 threads, leave thread 0 for now
		schedule( F(loop(10)) ); 	//schedule the initial job
		JobSystem::instance()->thread_task(0);	//main thread enters the job system as thread 0
		wait_for_termination();		//wait for the last thread to terminate
    	return 0;
    }

Some GUIs like GLFW work only if they are running in the main thread, so use this and make sure that all GUI related stuff runs on thread 0. 

Finally, the third parameters specifies a memory resource to be used for allocating job memory.

    auto g_global_mem = 
		std::pmr::synchronized_pool_resource(
			{ .max_blocks_per_chunk = 1000, .largest_required_pool_block = 1 << 10 }, std::pmr::new_delete_resource());

    int main()
    {
    	JobSystem::instance(0, 0, &g_global_mem);	//use the memory resource g_global_mem to allocate job structures
		schedule( F(loop(10)) ); 	//schedule the initial job
		wait_for_termination();		//wait for the last thread to terminate
    	return 0;
    }

## Functions
There are two types of tasks that can be scheduled to the job system - C++ functions and coroutines. Scheduling is doen via a call to the vgjs::schedule() function wrapper, which in turn calls the job system to schedule the function.
Functions can be wrapped into macros F() or FUNCTION(), into a lambda of type [=](){}, or into the class Function{}, the latter allowing to specify more parameters. Of course, a function simply CALL another function any time without scheduling it. 

	void any_function() {
		schedule( F(loop(10)) ); 					//schedule function loop(10) to run on a random thread
		schedule( FUNCTION(loop(10); loop(100);) ); //schedule function loop(10) and loop(100) to run on a random thread
		schedule( [=](){ (loop(10); loop(100);}  ); //schedule functions loop(10) and loop(100) to run on a random thread

		Function func{ F(loop(10)), 1, 0, 999 }; 	//Function to run on thread 1, with type 0 and id 999 (for logging)
		schedule( func );	//lvalue, so do not move the function func, it can be reused afterwards

		schedule( Function{ F(loop(10)), 2 } );		//schedule to run on thread 2, use rvalue, so move semantics apply
	}

Functions scheduling other functions create a parent-child relationship. Functions are immediately scheduled to be run, schedule() can be called any number of times to start an arbitrary number of children to run in parallel.
Function parameters should always be copied (see below)! Functions can also me member-functions, since they are wrapped into lambdas anyway. Just make sure that the class instance does not go out of scope!

## Coroutines
The second type of task to be scheduled are coroutines. 
Coroutines can suspend their function body (returning to the caller), and later on resume them where they had left. Any function that uses the keywords co_await, co_yield, or co_return is a coroutine. 
In this case, in order to be compatible with the job system, coroutines must be of type Coro<T>, where T is any type to be computed. 

An instance of Coro<T> acts like a future, in that it allows to create the coro, schedule it, and later on retrieve the promised value by calling get(). Additionally to this future, also a promise of type Coro_promise<T> is allocated from the heap. The promise stores the coro's state, suspend points and return value. Since this allocation is more expensive than getting memory from the stack, it is possible to pass in a pointer to a memory resource to be used for allocation. A coro that ends is usually destroyed by its future Coro<T>. See below for more infos on this.

    class CoroClass {	//a dummy C++ class that has a coro as one of its member functions
        int number = 1;	//store a number
    public:
        CoroClass( int nr ) : number(nr) {};
        Coro<int> Number10() { co_return number * 10; } //member coro - return the number times 10
    };

	//the coro compute() uses normal new and delete to allocate its promise
    Coro<int> compute(int i) {
        CoroClass cc(99);			//class instance stores number 99
        auto mf = cc.Number(10);	//get an instance of the class coro
        co_await mf;				//run it
        co_return 2 * mf.get();		//get result and return it
    }

	//the coro do_compute() uses g_global_mem to allocate its promise!
    Coro<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        co_await 0;				//move this job to the thread with number 0
        auto tk1 = compute(1); 	//create the coro compute() with parameter 1- it initially suspends
        co_await tk1;			//run it and wait for it to finish
        co_return tk1.get();	//get the promised value and return it
    }

	//the coro loop() uses g_global_mem to allocate its promise! 
    Coro<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int N) {
		for( int i=0; i<N; ++i) {
            co_await do_compute(std::allocator_arg, mr );//call do_compute() N times, no need for its return value
		}
		co_return 0; //have to return a value
	}

    auto g_global_mem =  							//my own memory pool
		std::pmr::synchronized_pool_resource(
			{ .max_blocks_per_chunk = 1000, .largest_required_pool_block = 1 << 10 }, std::pmr::new_delete_resource());

	int main() {
		JobSystem::instance();
		//pass on the memory resource to be used to allocate the promise, precede it with std::allocator_arg
		schedule( loop(std::allocator_arg, &g_global_mem, 10) ); //schedule coro loop() from a function
		wait_for_termination();
	}

Since any program starts with the main() function, from a C++ function, a coro can be scheduled by calling schedule(). 
Coros should NOT call schedule() themselves! Instead they MUST use co_await and co_return for starting their own children and returning values.

Coros can co_await a number of different types. Single types include
* C++ function packed into lambdas [=](){} or F() / FUNCTION() 
* Function{} class
* Coro<T> for any type T

Since the coro suspends and awaits the finishing of its children, this would allow only 1 child to await. Thus, coros can additionally await std::pmr::vectors, or even std::tuples containing K std::pmr::vectors of the above types. This allows to start and await any number of children of arbitrary type.

    Coro<int> recursive(std::allocator_arg_t, std::pmr::memory_resource* mr, int i, int N) {
        if (i < N) {
            std::pmr::vector<Coro<int>> vec{ mr };
            vec.emplace_back( recursive(std::allocator_arg, mr, i + 1, N));
            vec.emplace_back( recursive(std::allocator_arg, mr, i + 1, N));
			co_await vec;
        }
        co_return 0;
    }

    Coro<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
        float f = i + 0.5f;
        co_return 10.0f * i;
    }

    Coro<int> compute(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
        co_return 2 * i;
    }

    Coro<int> do_compute(std::allocator_arg_t, std::pmr::memory_resource* mr) {
        auto tk1 = compute(std::allocator_arg, mr, 1);
        co_await tk1;
        co_return tk1.get();
    }

    void FCompute( int i ) {
        std::cout << "FCompute " << i << std::endl;
    }

    void FuncCompute(int i) {
        std::cout << "FuncCompute " << i << std::endl;
    }

    Coro<int> loop(std::allocator_arg_t, std::pmr::memory_resource* mr, int count) {

        auto tv = std::pmr::vector<Coro<int>>{mr};	//vector of Coro<int>
        auto tk = std::make_tuple(					//tuple holding two vectors - Coro<int> and Coro<float>
			std::pmr::vector<Coro<int>>{mr}, 
			std::pmr::vector<Coro<float>>{mr});
        auto fv = std::pmr::vector<std::function<void(void)>>{ mr }; //vector of C++ functions
        std::pmr::vector<Function> jv{ mr };		//vector of Function{} instances

		//loop adds elements to these vectors
        for (int i = 0; i < count; ++i) {
            tv.emplace_back( do_compute(std::allocator_arg, &g_global_mem4 ) );

            get<0>(tk).emplace_back(compute(std::allocator_arg, &g_global_mem4, i));
            get<1>(tk).emplace_back(computeF(std::allocator_arg, &g_global_mem4, i));

            fv.emplace_back( F( FCompute(i) ) );

            Function f( F(FuncCompute(i)), -1, 0, 0 );
            jv.push_back( f );

            jv.push_back( Function( F(FuncCompute(i)), -1, 0, 0) );
        }
        
        co_await tv; //await all elements of the vector
        co_await tk; //await all elements of the vectors in the tuples
        co_await recursive(std::allocator_arg, &g_global_mem4, 1, 10); //await the recursive calls
        co_await F( FCompute(999) );			//await the function
        co_await Function( F(FCompute(999)) );	//await the function
        co_await fv; //await all elements of the vector
        co_await jv; //await all elements of the vector

        co_return 0;
    }

    void driver() {
        schedule( loop(std::allocator_arg, &g_global_mem4, 90) );
    }

Coroutines are also "callable", and you can pass in parameters similar to the Function{} class, setting thread index, type and id:

	//schedule to thread 0, set type to 11 and id to 99
    co_await recursive(std::allocator_arg, &g_global_mem4, 1, 10)(0,11,99) ; 

Coroutines can also change their thread by awaiting a thread index number:

    Coro<float> computeF(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

		//do something until here ...

		co_await 0;				//move this job to thread 0

        float f = i + 0.5f;		//continue on thread 0
        co_return 10.0f * i;
    }


## Finishing and Continuing Jobs

## Never use Pointers and References to Local Variables in Functions - only in Coroutines!
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


