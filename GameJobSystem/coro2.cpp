


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

    template<typename T>
    class task_promise_base
    {
        struct final_awaitable
        {
            bool await_ready() { return false; }
            template<typename P>
            std::experimental::coroutine_handle<> await_suspend(std::experimental::coroutine_handle<P> coro)
            {
                auto co = coro.promise().m_continuation;
                return co;
            }
            void await_resume() {}
        };
    public:
        task_promise_base() {}; // : m_state(state::empty) {}

        ~task_promise_base()
        {
            /*switch (m_state)
            {
            case state::value:
                reinterpret_cast<T*>(&m_storage)->~T();
                break;
            case state::exception:
                reinterpret_cast<std::exception_ptr*>(&m_storage)->~exception_ptr();
                break;
            case state::empty:
                break;
            }*/
        }
        task<T> get_return_object()
        {
            return task<T>{ *this, std::experimental::coroutine_handle<task_promise_base>::from_promise(*this) };
        }
        
        std::experimental::suspend_always initial_suspend() { return{}; }
        
        final_awaitable final_suspend() { return{}; }

        template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
        void return_value(U&& value)
        {
            //new (&m_storage) T(std::forward<U>(value));
            //m_state = state::value;
        }

        void unhandled_exception()
        {
            //new (&m_storage) std::exception_ptr(std::current_exception());
            //m_state = state::exception;
        }
        T& value()
        {
            /*if (m_state == state::exception)
            {
                std::rethrow_exception(*reinterpret_cast<std::exception_ptr*>(&m_storage));
            }

            return *reinterpret_cast<T*>(&m_storage);*/
            return {};
        }
    private:
        friend class task<T>;
        //enum class state { empty, value, exception };
        std::experimental::coroutine_handle<> m_continuation;
        //state m_state;
        //std::aligned_union_t<0, T, std::exception_ptr> m_storage;
    };




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

        ~task()
        {
            if (m_coro) m_coro.destroy();
        }

        bool await_ready() noexcept { return !m_coro || m_coro.done(); }

        std::experimental::coroutine_handle<> await_suspend(std::experimental::coroutine_handle<> continuation)
        {
            m_promise->m_continuation = continuation;
            return m_coro;
        }

        T await_resume()
        {
            /*if (!m_promise) throw std::exception{};
            return m_promise->value();*/
            return {};
        }

        bool resume() {
            if (!m_coro.done())
                m_coro.resume();
            return !m_coro.done();
        };

    private:
        task_promise_base<T>* m_promise;
        std::experimental::coroutine_handle<> m_coro;
    };

    template<typename T, typename Allocator>
    class task_promise : public task_promise_base<T>
    {
    public:
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

    task<int> bar(std::allocator_arg_t, MyAllocator allocator)
    {
        co_return 2;
    }

    task<int> foo(std::allocator_arg_t, MyAllocator allocator)
    {
        //int result = co_await bar(std::allocator_arg, allocator);
        co_return 2;
    }

    void test() {
        MyAllocator allocator;

        auto f = foo(std::allocator_arg, allocator);
        std::cout << f.resume() << std::endl;
    }
}



