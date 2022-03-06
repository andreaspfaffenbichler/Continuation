#include <iostream>
#include <coroutine>
#include <thread>
#include <functional>

using namespace std;

#pragma optimize ( "", off )

namespace
{
	struct TaskPromiseBase
	{
		struct FinalAwaitable
		{
			FinalAwaitable() noexcept {}
			bool await_ready() const noexcept { return false; }
			template<typename PROMISE>
				void await_suspend( std::coroutine_handle< PROMISE > coroutine) noexcept
				{
					TaskPromiseBase& promise = coroutine.promise();
					// Use 'release' memory semantics in case we finish before the
					// awaiter can suspend so that the awaiting thread sees our
					// writes to the resulting value.
					// Use 'acquire' memory semantics in case the caller registered
					// the continuation before we finished. Ensure we see their write
					// to m_continuation.
					if( promise.state_.exchange(true, std::memory_order_acq_rel) )
					{
						promise.continuation_.resume();
					}
				}
			void await_resume() noexcept {}
		};

		TaskPromiseBase() noexcept
			: state_( false )
		{}

		auto initial_suspend() noexcept { return std::suspend_always{}; }
		auto final_suspend() noexcept { return FinalAwaitable{}; }

		bool set_continuation( std::coroutine_handle<> continuation )
		{
			continuation_ = continuation;
			return !state_.exchange( true, std::memory_order_acq_rel );
		}

	private:
		std::coroutine_handle<> continuation_;
		std::atomic< bool > state_;
	};

	template< typename TASK >
		struct TaskPromise : TaskPromiseBase
		{
			TASK get_return_object() 
			{
				return TASK{ TASK::CoroutineHandle::from_promise( *this ) };
			}
			void unhandled_exception() { }
			void return_value( TASK::RESULT_TYPE result ) { result_ = result; }
			TASK::RESULT_TYPE result_ = {}; 
		};

	template< typename R >
	struct [[nodiscard]] Task 
	{
		using RESULT_TYPE = R;
		using TASK = Task< R >;
		using promise_type = TaskPromise< TASK >;
		using CoroutineHandle = std::coroutine_handle< promise_type >;

		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;
		Task(Task&& t) noexcept : coroutine_(t.coroutine_) { t.coroutine_ = {}; }
		Task& operator=(Task&& t) noexcept {
			if (this == &t) return *this;
			if (coroutine_) coroutine_.destroy();
			coroutine_ = t.coroutine_;
			t.coroutine_ = {};
			return *this;
		}
		explicit Task( CoroutineHandle coroutine ) 
			: coroutine_( coroutine ) 
		{}
		~Task()
		{
			if( coroutine_ )
				coroutine_.destroy();
		}
		void destroy() { coroutine_.destroy(); }
		void resume() { coroutine_.resume(); }

		struct AwaitableBase
		{
			using CoroutineHandle = std::coroutine_handle< promise_type >;
			CoroutineHandle coroutine_;

			AwaitableBase( CoroutineHandle coroutine ) noexcept
				: coroutine_( coroutine )
			{}

			bool await_ready() const noexcept
			{
				return !coroutine_ || coroutine_.done();
			}

			bool await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept
			{
				// NOTE: We are using the bool-returning version of await_suspend() here
				// to work around a potential stack-overflow issue if a coroutine
				// awaits many synchronously-completing tasks in a loop.
				//
				// We first start the task by calling resume() and then conditionally
				// attach the continuation if it has not already completed. This allows us
				// to immediately resume the awaiting coroutine without increasing
				// the stack depth, avoiding the stack-overflow problem. However, it has
				// the down-side of requiring a std::atomic to arbitrate the race between
				// the coroutine potentially completing on another thread concurrently
				// with registering the continuation on this thread.
				//
				// We can eliminate the use of the std::atomic once we have access to
				// coroutine_handle-returning await_suspend() on both MSVC and Clang
				// as this will provide ability to suspend the awaiting coroutine and
				// resume another coroutine with a guaranteed tail-call to resume().
				coroutine_.resume();
				return coroutine_.promise().set_continuation( awaitingCoroutine );
			}
		};

		auto operator co_await() const & noexcept
		{
			struct Awaitable : AwaitableBase
			{
				using AwaitableBase::AwaitableBase;
				decltype(auto) await_resume()
				{
					if( !this->coroutine_ )
						throw std::logic_error{ "!this->m_coroutine"};
					return this->coroutine_.promise().result_; 
				}
			};
			return Awaitable{ coroutine_ };
		}
		auto operator co_await() const && noexcept
		{
			struct Awaitable : AwaitableBase
			{
				using AwaitableBase::AwaitableBase;
				decltype(auto) await_resume()
				{
					if( !this->coroutine_ )
						throw std::logic_error{ "!this->m_coroutine"};
					return this->coroutine_.promise().result_; 
				}
			};
			return Awaitable{ coroutine_ };
		}

	private:
		CoroutineHandle coroutine_;
	};

	template< typename R > using Callback = std::function< void( R ) >;
	template< typename R > using Api = std::function< void( Callback< R > ) >;

	template< typename R >
		struct CallAsyncAwaiter
		{
			CallAsyncAwaiter( const Api< R > &api )
				:api_( api ) {}

			bool await_ready() { return false; }

			void await_suspend( std::coroutine_handle<> handle )
			{ 
				api_( [ this, handle ]( const R& r ) 
				{ 
					result_ = r;
					handle(); //resume
				});
			}

			R await_resume() { return result_; }

			const Api< R > api_;
			R result_ = {};
		};
	
	template< typename R >
		auto CallAsync( Api< R > api )
		{
			return CallAsyncAwaiter< R >{ api };
		}

// TEST
#define BOOST_TEST( x ) \
	if( !( x ) ) \
		std::cout << "Error: " << #x << std::endl;

#define BOOST_TEST_MESSAGE( x ) \
	std::cout << x << std::endl;


	std::thread t;

	void api( const std::function< void( int ) >& callback )
	{
		t = std::thread( [ = ] 
		{ 
			std::this_thread::sleep_for( std::chrono::seconds{ 5 } );
			callback( 42 );
		});
	}

	Task< int > Test1X()
	{
		BOOST_TEST_MESSAGE( "Start Test1X" );
		int x = co_await CallAsync< int >( &api );
		//BOOST_TEST( x == 42 );
		BOOST_TEST( x == 42 );
		BOOST_TEST_MESSAGE( "Test1X" );
		co_return x += 1;
	}

	Task< double > Test1aX()
	{
		BOOST_TEST_MESSAGE( "Start Test1aX" );
		int x = co_await Test1X();
		//BOOST_TEST( x == 42 );
		BOOST_TEST( x == 43 );
		BOOST_TEST_MESSAGE( "Test1aX" );
		co_return x += 1.0;
	}

	Task< nullptr_t > Test2X()
	{
		BOOST_TEST_MESSAGE( "Start Test2X" );
		auto x = co_await Test1aX();
		//BOOST_TEST( x == 42 );
		BOOST_TEST( x == 44.0 );
		BOOST_TEST_MESSAGE( "Test2X" );
		co_return {};
	}
}

int main()
{
	auto test = Test2X();
	test.resume();
	BOOST_TEST_MESSAGE( "main after resume" );
//	auto run = Test2X().resume();
	std::this_thread::sleep_for( std::chrono::seconds{ 1 } );
	t.join();
}

