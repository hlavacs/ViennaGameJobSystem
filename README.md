# Vienna Game Job System
The Vienna Game Job System (VGJS) is a C++20 library for parallelizing arbitrary tasks, as for example are typically found in game engines. It was designed and implemented by Prof. Helmut Hlavacs from the University of Vienna, Faculty of Computer Science (http://entertain.univie.ac.at/~hlavacs/).

Important features are:
* Supports C++20 (coroutines, concepts, polymorphic allocators and memory resources, ...)
* Can be run with coroutines (better convenience), with C++ functions (better performance), or both
* Use coroutines as fibers to improve on performance
* Schedule jobs with tags, run jobs with the same tag together (like a barrier)
* Enables a data oriented, data parallel paradigm
* Intended as partner project of the Vienna Vulkan Engine (https://github.com/hlavacs/ViennaVulkanEngine) implementing a game engine using the VGJS


## Library Usage
VGJS is a 2-header library that should be included in C++ source files where it is needed:

    #include "VGJS.h"

If you additionally want coroutines then also include

    #include "VGJSCoro.h"

When compiling your projects make sure to set the appropriate compiler flags to enable co-routines if you want to use them. With MSVC these are /await and /EHsc. VGJS also comes with a some examples showing how to use it. If you want to compile them, install the latest MS Visual Studio (2019+) and doxygen, then run *do_cmake.bat*, preferably in a Windows console to see possible errors. This creates a MSVC solution file VGJS.sln and the documentation in directory *Docs*.

VGJS runs a number of *N* worker threads, *each* having *two* work queues, a *local* queue and a *global* queue. When scheduling jobs, a target thread *K* can be specified or not. If the job is specified to run on thread *K* (using *vgjs\:\:thread_index_t{K}* ), then the job is put into thread *K*'s **local** queue. Only thread *K* can take it from there. If no thread is specified or an empty *vgjs\:\:thread_index_t{}* is chosen, then a random thread *J* is chosen and the job is inserted into thread *J*'s **global** queue. Any thread can steal it from there, if it runs out of local jobs. This paradigm is called *work stealing*. By using multiple global queues, the amount of contention between threads is minimized.

Each thread continuously grabs jobs from one of its queues and runs them. If the workload is split into a large number of small tasks then all CPU cores continuously do work and achieve a high degree of parallelism.

## Using the Job system
The job system is started by creating an instance of class *vgjs::JobSystem*.
The system is destroyed by calling *vgjs::terminate()*.
The main thread can wait for this termination by calling *vgjs::wait_for_termination()*.

    #include "VGJS.h"
    #include "VGJSCoro.h"

    using namespace vgjs;

    void printData(int i) {
        std::cout << "Print Data " << i << std::endl;
    }

    void loop( int N ) {
        for( int i=0; i< N; ++i) { //all jobs run in parallel
            schedule( [=](){ printData(i); } ); //schedule a lambda
        }

        //after all children have finished, this function will be scheduled to thread 0
        continuation( Function{ std::bind(vgjs::terminate), thread_index_t{0} } ); //schedule a Function
    }

    void test( int N ) {
          schedule( std::bind(loop, N ) ); //schedule a std::function
    }

    int main()
    {
        JobSystem js;               //create the job system
        schedule( [=](){test(5);} );//schedule a lambda function
        wait_for_termination();     //wait for the last thread to terminate
        return 0;
    }

In the above example we see the three main possibilities to schedule C++ functions:
* using a function pointer of type *void (\*)()*
* using *std::bind()* or *std::function<void(void)>*
* using a lambda function *\[=\](){}* (always use '=' for copying the local parameters!), or
* additionally using the class Function{}, which enables to pass on more parameters.

In the *main()* function, first the job system is initialized, then a single function *test(5)* is scheduled to run as a first job. Finally the main thread waits for the system to terminate. Function *test(5)* calls *schedule()* to run a function *loop(5)*. *loop(5)* starts 5 jobs which simply print out numbers. Then it sets a continuation for itself. Only functions running as jobs can schedule a continuation. So, neither the *main()* function, nor a coroutine may call this function (actually such a call would simply be ignored.) The output of the above example is something like this:

    Print Data 0
    Print Data Print Data 2
    3
    Print Data 4
    Print Data 1

The function *printData()* is called 5 times, all runs are concurrent to each other, mingling the output somewhat.

Instances of class *JobSystem* allow accessing the job system and are *monostate*. They accept three parameters, which can be provided or not. They are only used when the system is created, i.e. when the first instance is created. Afterwards, the parameters are ignored.

  	/**
    * \brief JobSystem class constructor
    * \param[in] threadCount Number of threads in the system
    * \param[in] start_idx Number of first thread, if 1 then the main thread should enter as thread 0
    * \param[in] mr The memory resource to use for allocating Jobs
    */
    JobSystem(  uint32_t threadCount = 0, uint32_t start_idx = 0,
                std::pmr::memory_resource *mr = std::pmr::new_delete_resource() )

If *threadCount* = 0 then the number of threads to start is given by the call *std\:\: thread \:\:hardware_concurrency()*, which gives the number of hardware threads, **not** CPU cores. On modern hyperthreading architectures, the hardware concurrency is typically twice the number of CPU cores.

If the second parameter *start_idx* is not 0, then the main thread should enter the job system as thread 0 instead of waiting for its termination:

    int main()
    {
        JobSystem js(0, 1);         //start only N-1 threads, leave thread 0 for now
        schedule( [=](){test(5);} );//schedule a lambda function
        js.thread_task(0);          //main thread enters the job system as thread 0
        wait_for_termination();     //wait for the last thread to terminate
        return 0;
    }

Some GUIs like GLFW work only if they are running in the main thread, so use this and make sure that all GUI related stuff runs on thread 0.

Finally, the third parameters specifies a memory resource to be used for allocating job memory and coroutine promises.

    auto g_global_mem =
        std::pmr::synchronized_pool_resource(
            { .max_blocks_per_chunk = 1000, .largest_required_pool_block = 1 << 10 }, std::pmr::new_delete_resource());

    int main()
    {
        JobSystem js(0, 0, &g_global_mem); //use the memory resource g_global_mem to allocate job structures
        schedule( [=](){test(5);} );//schedule a lambda function
        wait_for_termination();     //wait for the last thread to terminate
        return 0;
    }

If none is specified, the job system uses standard new and delete.

## Functions
There are two types of tasks that can be scheduled to the job system - C++ *functions* and *coroutines*. It is important to note that both functions and coroutines themselves can both schedule again functions and coroutines. However, how tasks are scheduled depends on the type of task that does this.
In a *function*, scheduling is done via a call to the *vgjs::schedule()* function wrapper, which in turn calls the job system to schedule the function. In a *coroutine*, scheduling is done with the *co_await* operator.

*Scheduled* C++ functions can be either of type *void (\*)()* or wrapped into *std::function<void(void)>* (e.g. create by using *std::bind()* or a lambda of type *\[=\](){})*, or into the class *Function*, the latter allowing to specify more parameters. Of course, a function can simply *call* another function any time without scheduling it.

    void any_function() { //this is a function, so we must use schedule()
        schedule( std::bind(loop, 10) ); //schedule function loop(10) to random thread
        schedule( [](){loop(10);} );     //schedule function loop(10) to random thread

        //schedule to run loop(10) and loop(100) sequentially on random thread
        schedule( [](){loop(10); loop(100);} );

        //Function to run on thread 1, with type 0 and id 999 (for logging)
        Function func{ [](){loop(10);}, thread_index_t{1}, thread_type_t{0}, thread_id_t{999} };
        schedule( func ); //lvalue, so do not move the function func, it can be reused afterwards

        //schedule to run on thread 2, use rvalue ref, so move semantics apply
        schedule( Function{ [](){loop(10);}, thread_index_t{2} } );
    }

Functions scheduling coroutines should simply call take the coroutine as parameter without packing it into a function wrapper.

    Coro<int> do_compute(int i) {
        //do something ...
        co_return i;   //return the promised value;
    }

    void other_fun() {
        schedule( do_compute(5) ); //schedule the coroutine
    }

Functions scheduling other functions or coroutines create a parent-child relationship. Functions are immediately scheduled to be run, *schedule()* can be called any number of times to start an arbitrary number of children to run in parallel.
If the parent is a C++ function, parameters of scheduled other functions should always be **called by value**, i.e., **copied** (see below)! Functions can also be *member-functions* of some class instance. In this case make sure that the class instance does not go out of scope while the function is running.

## Coroutines
The second type of task to be scheduled are *coroutines*.
Coroutines can suspend their function body (by returning to their caller), and later on resume them where they had left. Any function that uses the keywords *co_await*, *co_yield*, or *co_return* is a coroutine (see e.g. https://lewissbaker.github.io/).

In order to be compatible with the VGJS job system, coroutines must be of type *Coro\<T\>* (a.k.a. the "*future*"), where *T* is any type to be computed and returned using *co_return*. Return type *T* must be copyable or moveable, references can be wrapped e.g. into *std::ref*. Alternatively, a coroutine of type *Coro\<\>* or *Coro\<void\>* does not return anything, and must have an empty *co_return*.

From a C++ *function*, a child *coroutine* can be scheduled by calling *schedule( Coro\<T\> &&coro, ... )*. **Do not pack coro into a lambda!**
If you use coros, this must be done at least once, since any C++ program starts in the function *main()*.
On the other hand, coros should not call *schedule()*! Instead they should use *co_await* and *co_return* for starting their own children and returning values. A coro acting as *fiber* can also call *co_yield* to return an intermediate value, but remain to exist. This will be explained later.

Internally, additionally to the *future*, also a *promise* of type *Coro_promise\<T\>* is allocated from the same memory resource that is used by the job system instance to allocate job structures. The coro promise stores the coro's state, value and suspend points. It is possible to pass in a pointer to a different *std::pmr::memory_resource* to be used for allocation of coro promises (see the above example).

    //a memory resource
    auto g_global_mem = ::n_pmr::synchronized_pool_resource(
      { .max_blocks_per_chunk = 20
        , .largest_required_pool_block = 1 << 10 }
        , n_pmr::new_delete_resource());

    //a coroutine that uses a given memory resource to allocate its promise.
    //the coro calls itself through co_await recursively to compute i!
    Coro<int> factorial(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {
        if( i==0 ) co_return 1;
        int j = co_await factorial(std::allocator_arg_t, mr, i-1); //recurse
        std::cout << "Fact " << i*j << std::endl; //print intermediate results
        co_return i*j;   //return the promised value;
    }

    //no sync between other_fun() and factorial()
    //if you need the result be sure its is ready by calling ready()
    void other_fun(int i ) {
        auto f = factorial(std::allocator_arg, &docu::g_global_mem, i);
        schedule(f); //schedule coroutine directly, do not pack into lambda!
        while (!f.ready()) { //wait for the result
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        };
        std::cout << "Result " << f.get() << std::endl;
        vgjs::continuation([=](){ vgjs::terminate(); }); //continuation
    }

    //use schedule( [=](){...} ) from main()
    int main() {
      	using namespace vgjs;
      	JobSystem js;                        //create VGJS
      	schedule([=]() { other_fun(5); });   //schedule function with lambda
        wait_for_termination();             //wait until vgjs::terminate() was called
    }

In the above example, main() schedules a C++ function other_fun() using a lambda, and then waits until VGJS shuts down. The function other_fun() itself schedules a coro that calls itself to compute the factorial of a given parameter. other_fun() also schedules a continuation that is run once all children of other_fun() have finished. This continuation calls vgjs::terminate() to shut down VGJS. The output is

    Fact 1
    Fact 2
    Fact 6
    Fact 24
    Fact 120
    Result 120

Coroutines should **not** call *vgjs::continuation()*, since they are their own continuation automatically. They wait until all children from a *co_await* call are finished, and then continue on with the next statement.

### Return Values

An instance of *Coro\<T\>* acts like a *std\:\:future*, in that it allows to create the coro, schedule it, and later on retrieve the promised value by calling *get()* on it. Alternatively, the return value can be retrieved directly as return value from *co_await* (see the above example). If there is only one coro that is awaited and that returns a value, then *co_await* only returns this value. If there are more than one coros returning a value (i.e., *parallel()* is used), then the *co_await* returns a *tuple* holding all return values, and the individual return values can be retrieved e.g. through structured binding.

If the *parent* is a *function*, the parent might return any time and a *Coro_promise\<T\>* that reaches its end point *automatically destroys*. If the parent is still running it can access the child's return value by calling *get()* on the future *Coro\<T\>* because this value is kept in a *std::shared_ptr<std::pair<bool,T>>*, not in the *Coro_promise\<T\>* itself. The parent can check whether the result is available by calling *ready()*.

If the *parent* is *also* a *coroutine* then the *Coro_promise\<T\>* only suspends at its end (does not destroy automatically), and thus its future *Coro\<T\>* (living in the *parent* coro) destroys the promise (being the child) in the future's destructor. As long as the future lives, the promise also lives. In this case the *std::pair<bool,T>* is kept in the *Coro_promise\<T\>* itself, so there is no shared pointer (this increases the performance since no heap allocation for the shared pointer is necessary).

Once *co_await* returns, all children have finished and the result values are available. Thus, both parent and children are synchronized, and it is not necessary for the parent to call *ready()* to check on the availability of the result.

Coros can coawait a number of different types. Single types include
* C++ function packed into lambdas *\[=\](){}*, *std::bind()* or *std::function<void(void)>*
* Function{} class
* *Coro\<T\>* for any return type *T*, or empty *Coro\<\>*

Since the coro suspends and awaits the finishing of all of its children, this would allow only one child to be awaited. Thus there are two ways to start more than one child in parallel. First, using the *parallel()* function, a static number of children can be started, given as parameters:

    Coro<> coro_void(int x) {
        //...
        co_return; //return nothing
    };

    Coro<int> coro_int(float y) {
        //...
        co_return some_int; //return an int
    };

    Coro<float> coro_float(bool z) {
        //...
        co_return some_float; //return a float
    };

    void another_func() { //void (*)()
      //...
    }

    auto [ret1, ret2] //two values -> tuple, use structured binding
        = co_await parallel(
            [=](){ some_func(); } //no return value
            , coro_void(1)        //no return value
            , coro_int(2.1)       //returns int
            , coro_float(true)    //returns float
            , another_func );     //do not use () for void (*)()

In this example, four children are started, one function and three coros. Only coros can return a value, but one of the coros returns void, so there are only two return values (packed into a tuple), *ret1* being of type *int*, and *ret2* being of type *float*. Internally, *parallel()* results in a *std::tuple* holding references to the parameters, and you can use *std::tuple* instead of *parallel()* (see the implementation of *parallel()*).

Second you can co_await *std::pmr::vectors* of the above types. This allows to start and await any number of children of arbitrary types, where the number of children is determined dynamically at run time. If the vectors contain instances of type *Coro\<T\>*, then the result values will be of type *std::pmr::vector<T>* and contain the return values of the coros. If the return values are not needed, then it is advisable to switch to *Coro\<\>* instead, since creating the return vectors come with some performance overhead.

The following code shows how to start multiple children from a coro to run in parallel.

    //A coro returning a float
    Coro<int> coro_int(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
        std::cout << "coro_int " << i << std::endl;
        co_return i;
    }

    //A coro returning a float
    Coro<float> coro_float(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
        float f = 1.5f*(float)i;
        std::cout << "coro_float " << f << std::endl;
        co_return f;
    }

    //A coro returning nothing
    Coro<> coro_void(std::allocator_arg_t, n_pmr::memory_resource* mr, int i) {
        std::cout << "coro_void " << i << std::endl;
        co_return;
    }

    //a function
    void func(int i) {
        std::cout << "func " << i << std::endl;
    }

    Coro<int> test(std::allocator_arg_t, n_pmr::memory_resource* mr, int count) {
        std::pmr::vector<Coro<>> tv{ mr };  //vector of Coro<>
        tv.emplace_back(coro_void(std::allocator_arg, &g_global_mem, 1));

        std::pmr::vector<Coro<int>> ti{ mr };
        ti.emplace_back(coro_int(std::allocator_arg, &g_global_mem, 2));

        std::pmr::vector<Coro<float>> tf{mr};
        tf.emplace_back(coro_float(std::allocator_arg, &g_global_mem, 3));

        std::pmr::vector<std::function<void(void)>> fv{ mr }; //vector of C++ functions
        fv.emplace_back([=]() {func(4); });

        n_pmr::vector<Function> jv{ mr };                         //vector of Function{} instances
        jv.emplace_back( Function{[=]() {func(5); }, thread_index_t{}, thread_type_t{ 0 }, thread_id_t{ 0 }} );

        auto [ret1, ret2] = co_await parallel(tv, ti, tf, fv, jv);

        std::cout << "ret1 " << ret1[0] << " ret2 " << ret2[0] << std::endl;

        co_return 0;
    }

The output of the above code is

    coro_void 1
    func 4
    coro_float 4.5
    coro_int 2
    func 5
    ret1 2 ret2 4.5

The return values are stored in *ret1* and *ret2*, both are vectors containing only one value.

### Threads, Types, IDs

Functions of type *Function{}* can be assigned a specific thread that the Function should run on. In this case the Function is scheduled to the thread's local queue.

    schedule( Function{ [=]() {func(5); }, thread_index_t{0}, thread_type_t{11}, thread_id_t{99}} ); //run on thread 0, type 11, id 99

If no thread is given then the Function is scheduled to the global queue of a random thread. Additionally, for debugging and performance measurement purposes, jobs can be assigned by a type and an id. Both can be used to trace the calls, e.g. by writing the data into a log file as described below.

Coroutine futures *Coro\<T\>* are also "callable", and you can pass in parameters similar to the *Function{}* class, setting thread index, type and id:

    //schedule to thread 0, set type to 11 and id to 99
    co_await func(std::allocator_arg, &g_global_mem4, 1, 10)( thread_index_t{0}, thread_type_t{11}, thread_id_t{99} ) ;

Coroutines can also change their thread by awaiting a thread index number:

    Coro<float> func(std::allocator_arg_t, std::pmr::memory_resource* mr, int i) {

        //do something until here ...

        co_await thread_index_t{0};   //move this job to thread 0

        float f = i + 0.5f;     //continue on thread 0
        co_return 10.0f * i;
    }

## Generators and Fibers
A coroutine can be used as a generator or fiber (https://en.wikipedia.org/wiki/Fiber_(computer_science)). Essentially, this is a coroutine that never coreturns but suspends and waits to be called, compute a value, return the value, and suspend again. The coro can call any other child with *co_await*, but it **must** return its result using *co_yield* in order to stay alive.
In the below example, there is a fiber *yt* of type *Coro\<int\>*, which takes its input parameter from *g_yt_in*. Calling *co_await* on the fiber invokes the fiber, which
eventually calls *co_yield*. Here the fiber exits and returns to the invoking coro, which
accesses the result with calling *get()*.

    Coro<int> yield_test(int& input_parameter) {
      int value = 0;          //initialize the fiber here
      while (true) {          //a fiber never returns
        int res = value * input_parameter; //use internal and input parameters
        co_yield res;       //set std::pair<bool,T> value to indicate that this fiber is ready, and suspend
        //here its std::pair<bool,T> value is set to (false, T{}) to indicate that the fiber is working
        //co_await other(value, input_parameter);  //call any child
        ++value;            //do something useful
      }
      co_return value; //have this only to satisfy the compiler
    }

    int g_yt_in = 0;                //parameter that can be set from the outside
    auto yt = yield_test(g_yt_in);  //create a fiber using an input parameter

    Coro<int> loop(int N) {
      for (int i = 0; i < N; ++i) {
        g_yt_in = i; //set input parameter
        auto ret = co_await yt; //call the fiber and wait for it to complete
        std::cout << "Yielding " << ret << "\n";
      }
      vgjs::terminate();
      co_return 0;
    }

    void test(int N) {
      schedule( docu::docu4::loop(N));
    }

The output of this code is

    Yielding 0
    Yielding 1
    Yielding 4
    Yielding 9
    Yielding 16

The advantage of generators/fibers is that they are created only once, but can be called any number of times, hence the overhead is similar to that of C++ functions - or even better. The downside is that passing in parameters is more tricky. Also you need an arbitration mechanism to prevent two jobs calling the fiber in parallel. E.g., you can put fibers in a *JobQueue\<Coro\<int\>\>* queue and retrieve them from there.


## Finishing and Continuing Jobs
A job starting children defines a parent-child relationship with them. Since children can start children themselves, the result is a call tree of jobs running possibly in parallel on the CPU cores. In order to enable synchronization without blocking threads, the concept of "finishing" is introduced.

A parent synchronizes with its children through this non-blocking finishing process. If all children of a parent have finished and the parent additionally ends and returns, then it finishes itself. A job that finishes notifies it own parent of its finishing, thus enabling its parent to finish itself, and so on. This way, the event of finishing finally reaches the root of the job tree.

In C++ *functions*, children are started with the *schedule()* command, which is non-blocking. The waiting occurs after the parent function returns and ends itself. The job related to the parent remains in the system, and waits for its children to finish (if there are any). After finishing, the job notifies its own parent, and may start a continuation, which is another job that was previously defined by calling *continuation()*.

If the parent is a *coroutine*, then children are spawned by calling the *co_await* operator. Here the coro waits until all children have finished and resumes right after the *co_await*. Since the coro continues, it does not finish yet. Only after calling *co_return*, the coro finishes, and notifies its own parent. A coro should **not** call *schedule()* or *continuation()*!

## Tagged Jobs
A unique feature of VGJS is allowing *tags*. Consider a game loop where things are done in parallel. While user callbacks work on the current state, they might share a common state and rely on the data integrity while running. Thus changing data or deleting entities should be done after the callbacks are finished. VGJS allows to schedule jobs for doing this for the future.

When scheduling a job without specifying a tag, the job is immediately put into a global or local queue for running. If, however, *schedule()* is called with a tag, the job is placed into the tag's queue and waits there, until this tag is scheduled itself. Once the tag is scheduled, all jobs with the same tag are scheduled to the global/locals queues to run as *children* of the *current* job. Consider the following example:

    schedule([=](){ loop(1); });            //immediately scheduled

    schedule([=](){ loop(3); }, tag_t{1});  //wait in queue of tag 1
    schedule([=](){ loop(4); }, tag_t{1});  //wait in queue of tag 1

    schedule(tag_t{1});                       //run all jobs with tag 1
    schedule([=](){ loop(5); });            //immediately scheduled

    continuation([=](){ after_tag1(); }); //continuation waits for all jobs to finish

Tags act like barriers, and jobs can be prescheduled to do stuff later. E.g., changing shared resources or deleting entities can be scheduled to run later, in which the resources are no longer accessed in parallel.

Coroutines schedule functions and other coroutines for future runs also using the *schedule()* function. However, scheduling tag jobs must be done with *co_await*:

    void printPar(int i) { //print something
        std::cout << "i: " << i << std::endl;
    }

    Coro<int> tag1() {
        std::cout << "Tag 1" << std::endl;
        co_await parallel(tag_t{ 1 }, [=]() { printPar(4); }, [=]() { printPar(5); },[=]() { printPar(6); });
        co_await tag_t{ 1 }; //runt jobs with tag 1
        co_return 0;
    }

    void tag0() {
        std::cout << "Tag 0" << std::endl;
        schedule([=]() { printPar(1); }, tag_t{ 0 });
        schedule([=]() { printPar(2); }, tag_t{ 0 });
        schedule([=]() { printPar(3); }, tag_t{ 0 });
        schedule(tag_t{ 0 });   //run jobs with tag 0
        continuation(tag1());   //continue with tag1()
    }

    void test() {
        std::cout << "Starting tag test()\n";
        schedule([=]() { tag0(); });
        std::cout << "Ending tag test()\n";
    }

The example program above first schedules a function *tag0()* which runs tag 0 jobs. The output of this code is

    Starting thread 11
    Starting tag test()
    Ending tag test()
    Tag 0
    i: 2i: 3

    i: 1
    Tag 1
    i: 4
    i: 6
    i: 5

## Breaking the Parent-Child Relationship
Jobs having a parent will trigger a continuation of this parent after they have finished. This also means that these continuations depend on the children and have to wait. Starting a job that does not have a parent is easily done by using *nullptr* as the second argument of the *schedule()* call.

    void driver() {
        schedule( loop(std::allocator_arg, &g_global_mem4, 90), tag_t{}, nullptr );
    }

The parameter will always be used if specified, also when scheduling tagged jobs. Therefore

    schedule(tag_t{1}, nullptr);

runs tag 0 jobs, but these jobs do not have any parent, and the current job does not depend on them.


## Never use Pointers and References to Local Variables in *Functions* - only in Coroutines!
It is important to notice that running C++ functions is completely decoupled from each other. When running a parent, its children do not have the guarantee that the parent will continue running during their life time. Instead it is likely that a parent stops running and all its local variables go out of context, while its children are still running. Thus, parent functions should **never** pass pointers or references to variables that are **local** to them. Instead, in the dependency tree, everything that is shared amongst functions and especially passed to children as parameter must be either passed by value, or points or refers to **global or permanent** data structures or heaps.

This does **not** apply to coroutines, since coroutines do not go out of context when running children. So coroutines **can** pass references or pointers to local variables!

When sharing global variables in functions that might be changed by several jobs in parallel, e.g. counters counting something up or down, you should consider using *std::atomic<T>* in order to avoid unpredictable runtime behavior. In a job, never wait for anything for long, use polling instead and finally return. Waiting will block the thread that runs the job and take away overall processing efficiency.


## Data Parallelism and Performance
VGJS enables data parallel thinking since it enables focusing on data structures rather than tasks. The system assumes the use of many data structures that might or might not need computation. Data structures can be either global, or are organized as data streams that flow from one system to another system and get transformed in the process.

Since the VGJS incurs some overhead, jobs should not bee too small in order to enable some speedup. Depending on the CPU, job sizes in the order of 1-2 us seem to be enough to result in noticeable speedups on a 4 core Intel i7 with 8 hardware threads. Smaller job sizes are course possible but should not occur too often.

## Logging Jobs
Execution of jobs can be recorded in trace files compatible with the Google Chrome chrome://tracing/ viewer. Recording can be switched on by calling *enable_logging()*. By calling *disable_logging()*, recording is stopped and the recorded data is saved to a file with name "log.json". The available dump is also saved to file if the job system ends.

The dump file can then be loaded in the Google Chrome *chrome://tracing/* viewer. Just start Google Chrome and type in *chrome://tracing/* in the search field. Click on the Load button and select the trace file.
