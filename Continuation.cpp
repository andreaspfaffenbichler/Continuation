#include <iostream>
#include <thread>
#include <functional>

#if defined( WIN32 ) || defined( _CONSOLE ) || defined( __GNUC__ )
#else
#define EXPERIMENTAL_COROUTINE
#endif

#ifdef EXPERIMENTAL_COROUTINE
#include <experimental/coroutine>
#else
#include <coroutine>
#endif

namespace
{
	template< typename R = nullptr_t > struct Continuation;

#ifdef EXPERIMENTAL_COROUTINE
	namespace std_coroutine = std::experimental;
#else
	namespace std_coroutine = std;
#endif

	void BuildAsyncChain( auto suspendedCoroutine,  auto callingCoroutine )
	{
		suspendedCoroutine.promise().callingCoroutine_ = callingCoroutine; 
		callingCoroutine.promise().sync_ = false; 
	}

	template< typename R >
	struct Continuation 
	{
		struct promise_type
		{
			promise_type() noexcept {}
			Continuation< R > get_return_object() 
			{
				return Continuation< R >{ std_coroutine::coroutine_handle< promise_type >::from_promise( *this ) };
			}

			struct ResumeContinuation
			{
				ResumeContinuation() noexcept {}
				bool await_ready() const noexcept { return false; }
				void await_suspend( std_coroutine::coroutine_handle< promise_type > thisCoroutine ) noexcept
				{
					auto& promise = thisCoroutine.promise();
					if( promise.callingCoroutine_ )
						promise.callingCoroutine_.resume();
				}
				void await_resume() noexcept {}
			};
			auto initial_suspend() noexcept { return std_coroutine::suspend_never{}; }
			auto final_suspend() noexcept { return ResumeContinuation{}; }
			void return_value( R result ) { result_ = result; }
			void unhandled_exception() {}
			R result_ = {}; 
			std_coroutine::coroutine_handle<> callingCoroutine_;
			bool sync_ = true;
		};

		Continuation( const Continuation& ) = delete;
		Continuation& operator=( const Continuation& ) = delete;
		Continuation& operator=( Continuation&& ) noexcept = delete;

		Continuation() noexcept = default;
		Continuation( Continuation&& t ) noexcept = default;
		explicit Continuation( std_coroutine::coroutine_handle< promise_type > coroutine ) : coroutine_( coroutine ) {}

		bool await_ready() const noexcept{ return false; }
		void await_suspend( auto callingCoroutine ) noexcept 
		{ 
			if( coroutine_.promise().sync_ )
				callingCoroutine.resume();
			else
				BuildAsyncChain( this->coroutine_, callingCoroutine );
		}
		auto await_resume() { return coroutine_.promise().result_; }

		std_coroutine::coroutine_handle< promise_type > coroutine_;
	};

	template< typename R > using AsyncApiCallback = std::function< void( R ) >;
	template< typename R > using AsyncApi = std::function< void( AsyncApiCallback< R > ) >;
	template< typename R >
		struct AsyncCallbackContinuationAwaiter
		{
			bool await_ready() { return false; }
			void await_suspend( auto callingContinuation )
			{ 
				callingContinuation.promise().sync_ = false;
				api_( [ this, callingContinuation ]( const R& r ) mutable
				{ 
					result_ = r;
					callingContinuation.resume();
				});
			}
			R await_resume() { return result_; }

			const AsyncApi< R > api_;
			R result_ = {};
		};
	template< typename R >
		auto AsyncCallbackContinuation( AsyncApi< R > api )
		{
			return AsyncCallbackContinuationAwaiter< R >{ api };
		}


// TEST
#define BOOST_TEST( x ) \
	if( !( x ) ) \
		std::cout << "Error: " << #x << std::endl;

#define BOOST_TEST_MESSAGE( x ) \
	std::cout << x << std::endl;


	namespace Test
	{
		std::thread t;
		bool continuationsRun = false;

		void apiAsync( const std::function< void( int ) >& callback )
		{
			t = std::thread( [ = ] 
			{ 
				std::this_thread::sleep_for( std::chrono::seconds{ 5 } );
				callback( 41 );
			});
		}

		int apiSync()
		{
			return 41;
		}

		Continuation< int > Test1Async()
		{
			int initalValue = 1;
 			BOOST_TEST_MESSAGE( "Start Test1Async" );
			int x = co_await AsyncCallbackContinuation< int >( &Test::apiAsync );
			x += initalValue;
			BOOST_TEST( x == 42 );
			BOOST_TEST_MESSAGE( "Test1Async" );
			co_return x += 1;
		}
		Continuation< int > Test1Sync()
		{
			int initalValue = 1;
 			BOOST_TEST_MESSAGE( "Start Test1Sync" );
			int x = 41;
			x += initalValue;
			BOOST_TEST( x == 42 );
			BOOST_TEST_MESSAGE( "Test1Sync" );
			co_return x += 1;
		}

		Continuation< double > Test2( auto&& test1 )
		{
			BOOST_TEST_MESSAGE( "Start Test2" );
			auto x = co_await test1();
			BOOST_TEST( x == 43 );
			BOOST_TEST_MESSAGE( "Test2" );
			co_return x + 1.0;
		}

		Continuation< double > Test3( auto&& callbackContinuation )
		{
			BOOST_TEST_MESSAGE( "Start Test3" );
			auto x = co_await Test2( callbackContinuation );
			BOOST_TEST( x == 44.0 );
			BOOST_TEST_MESSAGE( "Test3" );
			co_return x + 1.0;
		}

		Continuation<> Test4( auto&& callbackContinuation )
		{
			BOOST_TEST_MESSAGE( "Start Test4" );
			auto x = co_await Test3( callbackContinuation );
			BOOST_TEST( x == 45.0 );
			BOOST_TEST_MESSAGE( "Test4" );
			continuationsRun = true;
			co_return {};
		}
	}
}

int main()
{
	BOOST_TEST_MESSAGE( "main start" );

	//BOOST_TEST_MESSAGE( "return start" );
	//{
	//	Test::continuationsRun = false;
	//	Test::Test4( []()->Continuation< int >{ co_return co_await AwaitableLiteral( 41 ); } );
	//	BOOST_TEST( Test::continuationsRun );
	//}

	BOOST_TEST_MESSAGE( "synchron start" );
	{
		Test::continuationsRun = false;
		Test::Test4( &Test::Test1Sync );
		BOOST_TEST( Test::continuationsRun );
	}

	BOOST_TEST_MESSAGE( "asynchron start" );
	{
		Test::continuationsRun = false;
		Test::Test4( &Test::Test1Async );
		BOOST_TEST( !Test::continuationsRun );
	}
	BOOST_TEST( !Test::continuationsRun );
	BOOST_TEST_MESSAGE( "main after Test4" );
	//std::this_thread::sleep_for( std::chrono::seconds{ 1 } );
	BOOST_TEST_MESSAGE( "main after sleep" );
	Test::t.join();
	BOOST_TEST( Test::continuationsRun );
	BOOST_TEST_MESSAGE( "after join" );
}

