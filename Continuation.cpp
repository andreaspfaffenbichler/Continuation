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
#ifdef EXPERIMENTAL_COROUTINE
	namespace std_coroutine = std::experimental;
#else
	namespace std_coroutine = std;
#endif

	template< typename R = nullptr_t >
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
		};

		Continuation( const Continuation& ) = delete;
		Continuation& operator=( const Continuation& ) = delete;
		Continuation& operator=( Continuation&& ) noexcept = delete;

		Continuation( Continuation&& t ) noexcept = default;
		explicit Continuation( std_coroutine::coroutine_handle< promise_type > coroutine ) : coroutine_( coroutine ) {}

		bool await_ready() const noexcept{ return false; }
		void await_suspend( std_coroutine::coroutine_handle<> callingCoroutine ) noexcept 
		{ 
			coroutine_.promise().callingCoroutine_ = callingCoroutine; 
		}
		auto await_resume() { return coroutine_.promise().result_; }

		void Rundown() 
		{
			while( !coroutine_.done() )
				coroutine_.resume();
		}

		std_coroutine::coroutine_handle< promise_type > coroutine_;
	};

	template< typename R > using Callback = std::function< void( R ) >;
	template< typename R > using Api = std::function< void( Callback< R > ) >;
	template< typename R >
		struct CallbackContinuationAwaiter
		{
			bool await_ready() { return false; }
			void await_suspend( std_coroutine::coroutine_handle<> handle )
			{ 
				api_( [ this, handle ]( const R& r ) mutable
				{ 
					result_ = r;
					handle.resume();
				});
			}
			R await_resume() { return result_; }

			const Api< R > api_;
			R result_ = {};
		};
	template< typename R >
		auto CallbackContinuation( Api< R > api )
		{
			return CallbackContinuationAwaiter< R >{ api };
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

		void apiSync( const std::function< void( int ) >& callback )
		{
			callback( 41 );
		}

		Continuation< int > Test1( Api< int > api )
		{
			int initalValue = 1;
 			BOOST_TEST_MESSAGE( "Start Test1" );
			int x = co_await CallbackContinuation< int >( api );
			x += initalValue;
			BOOST_TEST( x == 42 );
			BOOST_TEST_MESSAGE( "Test1" );
			co_return x += 1;
		}

		Continuation< double > Test2( Api< int > api )
		{
			BOOST_TEST_MESSAGE( "Start Test2" );
			auto x = co_await Test1( api );
			BOOST_TEST( x == 43 );
			BOOST_TEST_MESSAGE( "Test2" );
			co_return x + 1.0;
		}

		Continuation< double > Test3( Api< int > api )
		{
			BOOST_TEST_MESSAGE( "Start Test3" );
			auto x = co_await Test2( api );
			BOOST_TEST( x == 44.0 );
			BOOST_TEST_MESSAGE( "Test3" );
			co_return x + 1.0;
		}

		Continuation<> Test4( Api< int > api )
		{
			BOOST_TEST_MESSAGE( "Start Test4" );
			auto x = co_await Test3( api );
			BOOST_TEST( x == 45.0 );
			BOOST_TEST_MESSAGE( "Test4" );
			continuationsRun = true;
			co_return {};
		}
	}

	template< typename R >
		R SyncronizeHack( auto x )
		{
			auto coroutine = x();
			coroutine.Rundown();
			return coroutine.coroutine_.promise().result_;
		}
}

int main()
{
	BOOST_TEST_MESSAGE( "main start" );

	//BOOST_TEST_MESSAGE( "synchron start simple" );
	//{
	//	bool run = false;
	//	auto coro = [ & ]()->Continuation<>
	//	{
	//		auto x = co_await Test::Test3( &Test::apiSync );
	//		BOOST_TEST( x == 45.0 );
	//		run = true;
	//		co_return {};
	//	}();
	//	BOOST_TEST( run == true );
	//}
	//BOOST_TEST_MESSAGE( "synchron start" );
	//{
	//	auto x = SyncronizeHack< double >( &B::Test3 );
	//	BOOST_TEST( x == 45.0 );
	//}

	//BOOST_TEST_MESSAGE( "synchron literal start" );
	//{
	//	auto x = SyncronizeHack< double >( &C::Test3 );
	//	BOOST_TEST( x == 45.0 );
	//}
	//BOOST_TEST_MESSAGE( "synchron literal start simple" );
	//{
	//	bool run = false;
	//	{
	//		auto coro = [ & ]()->Continuation<>
	//		{
	//			auto x = co_await Test::Test3( &Test::apiAsync );
	//			BOOST_TEST( x == 45.0 );
	//			run = true;
	//			co_return {};
	//		}();
	//	}
	//	BOOST_TEST( run == true );
	//}

	BOOST_TEST_MESSAGE( "synchron start" );
	{
		Test::continuationsRun = false;
		Test::Test4( &Test::apiSync );
		BOOST_TEST( Test::continuationsRun );
	}

	BOOST_TEST_MESSAGE( "asynchron start" );
	{
		Test::continuationsRun = false;
		Test::Test4( &Test::apiAsync );
		BOOST_TEST( !Test::continuationsRun );
	}
	BOOST_TEST( !Test::continuationsRun );
	BOOST_TEST_MESSAGE( "main after Test4" );
	std::this_thread::sleep_for( std::chrono::seconds{ 1 } );
	BOOST_TEST_MESSAGE( "main after sleep" );
	Test::t.join();
	BOOST_TEST( Test::continuationsRun );
	BOOST_TEST_MESSAGE( "after join" );
}

