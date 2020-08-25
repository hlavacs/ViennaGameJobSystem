


// Example coroutine type showing passing a custom allocator via
// a coroutine parameter that is used for allocating the coroutine
// frame.
// - Lewis Baker

 
#define _NODISCARD [[nodiscard]]
#include <experimental/coroutine>
#include <experimental/resumable>
#include <experimental/generator>

#include <future>
#include <iostream>
#include <chrono>
#include <thread>
#include <array>
#include <memory_resource>
#include <concepts>
#include <algorithm>
#include <string>




namespace coro2 {
    template<typename T>
    class task;

    auto g_global_mem2 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    template<typename T>
    class task_promise_base
    {
        /*struct final_awaitable
        {
            bool await_ready() { return false; }

            template<typename P>
            std::experimental::coroutine_handle<> await_suspend(std::experimental::coroutine_handle<P> coro)
            {
                auto co = coro.promise().continuation_;
                return co;
            }
            void await_resume() {}
        };*/

    public:
        task_promise_base() : value_{} {}

        ~task_promise_base() {}

        task<T> get_return_object() {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise_base>::from_promise(*this) };
        }
        
        std::experimental::suspend_always initial_suspend() { 
            return{}; 
        }
        
        template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
        void return_value(U&& value) {
            value_ = value;
        }

        void unhandled_exception() {
            std::terminate();
        }

        T& get() {
            return value_;
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            template<typename PROMISE>
            void await_suspend(std::experimental::coroutine_handle<PROMISE> h) noexcept {
                auto& promise = h.promise();
                if (!promise.continuation_) return;

                if (promise.ready_.exchange(true, std::memory_order_acq_rel)) {
                    promise.continuation_.resume();
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

    private:
        friend class task<T>;
        std::experimental::coroutine_handle<> continuation_;
        std::atomic<bool> ready_ = false;
        T value_;
    };


    //--------------------------------------------------------------------------------------

    template<typename T>
    class task
    {
    public:

        task(task_promise_base<T>& promise, std::experimental::coroutine_handle<> coro)
            : m_promise(std::addressof(promise))
            , m_coro(coro)
        {}

        task(task&& other)
            : m_promise(std::exchange(other.m_promise, nullptr))
            , m_coro(std::exchange(other.m_coro, {}))
        {}

        ~task() {
            if (m_coro && !m_coro.done()) m_coro.destroy();
        }

        T get() {
            return m_promise->get();
        }

        bool resume() {
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

        //----------------------------------------------------------------------------------------------------------

        bool await_ready() noexcept {
            return false;
        }

        bool await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
            m_promise->continuation_ = continuation;
            m_coro.resume();
            return !m_promise->ready_.exchange(true, std::memory_order_acq_rel);
        }

        T await_resume() noexcept {
            return m_promise->value_;
        }

    private:
        task_promise_base<T>* m_promise;
        std::experimental::coroutine_handle<> m_coro;
    };


    //--------------------------------------------------------------------------------------


    template<typename T, typename Allocator>
    class task_promise : public task_promise_base<T>
    {
    public:

        void* operator new(std::size_t size) {
            void* ptr = g_global_mem2.allocate(size);
            if (!ptr) throw std::bad_alloc{};
            return ptr;
        }

        void operator delete(void* ptr, std::size_t size) {
            g_global_mem2.deallocate(ptr, size);
        }


        /*template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, Allocator& allocator, Args&&...)
        {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* mem = (char*)allocator.allocate(allocatorOffset + sizeof(Allocator));
            try
            {
                new (&mem + sz) Allocator(allocator);
            }
            catch (...)
            {
                allocator.deallocate(mem);
                throw;
            }
            return mem;
        }
        void operator delete(void* p, std::size_t sz)
        {
            auto allocatorOffset = (sz + alignof(Allocator) - 1) & ~(alignof(Allocator) - 1);
            char* mem = static_cast<char*>(p);
            Allocator& allocator = *reinterpret_cast<Allocator*>(mem + allocatorOffset);
            Allocator allocatorCopy = std::move(allocator); // assuming noexcept copy here.
            allocator.~Allocator();
            allocatorCopy.deallocate(mem);
        }*/

        task<T> get_return_object()
        {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise>::from_promise(*this) };
        }
    };
}


namespace std
{
    namespace experimental
    {
        template<typename T, typename... Args>
        struct coroutine_traits<coro2::task<T>, Args...>
        {
            using promise_type = coro2::task_promise_base<T>;
        };

        template<typename T, typename Allocator, typename... Args>
        struct coroutine_traits<coro2::task<T>, std::allocator_arg_t, Allocator, Args...>
        {
            using promise_type = coro2::task_promise<T, Allocator>;
        };

        template<typename T, typename Class, typename Allocator, typename... Args>
        struct coroutine_traits<coro2::task<T>, Class, std::allocator_arg_t, Allocator, Args...>
        {
            using promise_type = coro2::task_promise<T, Allocator>;
        };
    }
}

namespace coro2 {
    struct MyAllocator {
        MyAllocator() {};
        MyAllocator(const MyAllocator&) {};
        ~MyAllocator() {};
        void* allocate(std::size_t sz) { 
            return new uint8_t[sz]; 
        };
        void deallocate(void* p) { 
            delete[] p; 
        };
    private:
        void* m_state;
    };

    template<typename ALLOCATOR>
    task<int> completes_synchronously(std::allocator_arg_t, ALLOCATOR allocator, int i) {
        co_return 2 * i;
    }

    template<typename ALLOCATOR>
    task<int> loop_synchronously(std::allocator_arg_t, ALLOCATOR allocator, int count) {
        int sum = 0;

        for (int i = 0; i < count; ++i) {
            sum += co_await completes_synchronously(std::allocator_arg, allocator, i);
        }
        co_return sum;
    }

    void test() {
        MyAllocator allocator;
        std::pmr::polymorphic_allocator<char> allocator2;

        auto f = loop_synchronously(std::allocator_arg_t{}, allocator2, 10);
        std::cout << f.resume() << std::endl;
        std::cout << f.get() << std::endl;

    }
}



