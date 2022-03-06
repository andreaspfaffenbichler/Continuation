#include <iostream>
#include <coroutine>
#include <thread>
#include <functional>

using namespace std;

#pragma optimize ( "", off )

namespace
{
	struct ContinuationPromiseBase
	{
		struct ResumeContinuation
		{
			ResumeContinuation() noexcept {}
			bool await_ready() const noexcept { return false; }
			template<typename PROMISE>
				void await_suspend( std::coroutine_handle< PROMISE > coroutine) noexcept
				{
					ContinuationPromiseBase& promise = coroutine.promise();
					if( promise.continuation_ )
						promise.continuation_.resume();
				}
			void await_resume() noexcept {}
		};
		ContinuationPromiseBase() noexcept {}
		auto initial_suspend() noexcept { return std::suspend_never{}; }
		auto final_suspend() noexcept { return ResumeContinuation{}; }
		std::coroutine_handle<> continuation_;
	};

	template< typename CONTINUATION >
		struct ContinuationPromise : ContinuationPromiseBase
		{
			CONTINUATION get_return_object() 
			{
				return CONTINUATION{ CONTINUATION::CoroutineHandle::from_promise( *this ) };
			}
			void unhandled_exception() { }
			void return_value( CONTINUATION::RESULT_TYPE result ) { result_ = result; }
			CONTINUATION::RESULT_TYPE result_ = {}; 
		};

	template< typename R >
	struct [[nodiscard]] Continuation 
	{
		using RESULT_TYPE = R;
		using CONTINUATION = Continuation< R >;
		using promise_type = ContinuationPromise< CONTINUATION >;
		using CoroutineHandle = std::coroutine_handle< promise_type >;

		Continuation( const Continuation& ) = delete;
		Continuation& operator=( const Continuation& ) = delete;
		Continuation& operator=( Continuation&& ) noexcept = delete;

		Continuation( Continuation&& t ) noexcept { coroutine_(t.coroutine_); }
		explicit Continuation( CoroutineHandle coroutine ) 
			: coroutine_( coroutine ) 
		{}
		~Continuation()
		{
			if( coroutine_ )
				coroutine_.destroy();
		}

		bool await_ready() const noexcept
		{
			return false;
		}
		void await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept
		{
			coroutine_.promise().continuation_ = awaitingCoroutine;
		}
		auto await_resume()
		{
			return coroutine_.promise().result_; 
		}

	private:
		CoroutineHandle coroutine_;
	};

	template< typename R > using Callback = std::function< void( R ) >;
	template< typename R > using Api = std::function< void( Callback< R > ) >;
	template< typename R >
		struct CallbackContinuationAwaiter
		{
			CallbackContinuationAwaiter( const Api< R > &api )
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

	std::thread t;

	void api( const std::function< void( int ) >& callback )
	{
		t = std::thread( [ = ] 
		{ 
			std::this_thread::sleep_for( std::chrono::seconds{ 5 } );
			callback( 42 );
		});
	}

	Continuation< int > Test1X()
	{
		BOOST_TEST_MESSAGE( "Start Test1X" );
		int x = co_await CallbackContinuation< int >( &api );
		//BOOST_TEST( x == 42 );
		BOOST_TEST( x == 42 );
		BOOST_TEST_MESSAGE( "Test1X" );
		co_return x += 1;
	}

	Continuation< double > Test1aX()
	{
		BOOST_TEST_MESSAGE( "Start Test1aX" );
		int x = co_await Test1X();
		//BOOST_TEST( x == 42 );
		BOOST_TEST( x == 43 );
		BOOST_TEST_MESSAGE( "Test1aX" );
		co_return x + 1.0;
	}

	Continuation< nullptr_t > Test2X()
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
	BOOST_TEST_MESSAGE( "main start" );
	auto test = Test2X();
	BOOST_TEST_MESSAGE( "main after Test2X" );
	std::this_thread::sleep_for( std::chrono::seconds{ 1 } );
	BOOST_TEST_MESSAGE( "main after sleep" );
	t.join();
	BOOST_TEST_MESSAGE( "after join" );
}

