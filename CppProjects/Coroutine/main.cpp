#include "Coro.h"
#include <stdio.h>
#include <thread>
#include <atomic>
#include <memory>
#include <assert.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/syscall.h>
inline pid_t GetCurrentThreadId()
{
	return syscall( SYS_gettid );
}
#endif

void test_thread_local()
{
	using namespace Coro;
	ThreadLocal loc;
	loc.Set( ( void* ) 12345 );
	assert( ( int64_t ) loc.Get() == 12345 );

	std::thread th( [ &loc ]()
	{
		assert( loc.Get() == nullptr );
		loc.Set( ( void* ) 987654 );
	});
	th.join();
	assert( ( int64_t ) loc.Get() == 12345 );
}

void test_coroutines()
{
	using namespace Coro;
	Coroutine coro1;

	try
	{
		Coroutine coro2;
		assert( false );
	}
	catch( const Exception &exc )
	{
		assert( exc.ErrorCode == ErrorCodes::CoroToCoro );
		assert( std::string( exc.what() ) == "Cannot convert coroutine to coroutine" );
	}
	catch( ... )
	{
		assert( false );
	}

	assert( coro1.SwitchTo() );

	std::unique_ptr<Coro::Coroutine> coro2_ptr;
	std::unique_ptr<Coro::Coroutine> coro3_ptr;

	std::atomic<bool> barrier( false );
	std::atomic<bool> run( true );

	auto thread_func = [ & ]() -> Coroutine*
	{
		auto coro_fnc = [ & ]() -> Coroutine*
		{
			barrier.store( true );

			printf( "tid: %i, %s\n", GetCurrentThreadId(), "coro_fnc" );
			fflush( stdout );

			while( run.load() )
			{
				std::this_thread::yield();
			}


			printf( "tid: %i, %s\n", GetCurrentThreadId(), "coro_fnc end" );
			fflush( stdout );

			// assert( coro2_ptr->SwitchTo() );
			// return nullptr;
			return coro2_ptr.get();
		};

		coro2_ptr.reset( new Coro::Coroutine() );
		coro3_ptr.reset( new Coro::Coroutine( coro_fnc, 10*1024 ) );

		printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_0" );
		fflush( stdout );

		assert( coro3_ptr->SwitchTo() );

		printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_1" );
		fflush( stdout );

		assert( coro1.SwitchTo() );
		printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_2" );
		fflush( stdout );

		return 0;
	};

	printf( "tid: %i, %s\n", GetCurrentThreadId(), "main begin" );
	fflush( stdout );

	std::thread hthread( thread_func );

	while( !barrier.load() )
	{
		std::this_thread::yield();
	}

	coro2_ptr->SwitchTo();

	printf( "tid: %i, %s\n", GetCurrentThreadId(), "main" );
	fflush( stdout );
	run.store( false );

	hthread.join();
} // void test_coroutines()

int main( int argc, char *argv[] )
{
	test_thread_local();
	test_coroutines();
	return 0;

	// Проверка падения при неверной функции сопрограммы
	using namespace Coro;

	if( 0 )
	{
		Coroutine coro1;
		try
		{
			Coroutine coro_1;
		}
		catch( std::exception &exc )
		{
			printf( "%s\n", exc.what() );
			fflush( stdout );
		}

		Coroutine coro_1( []() -> Coroutine*
		{
			printf( "%s\n", "coro_1" );
			fflush( stdout );
			return nullptr;
		}, 10*1024 );

		auto coro_fnc = [ & ]() -> Coroutine*
		{
			printf( "%s\n", "coro" );
			fflush( stdout );
//			assert( coro1.SwitchTo() );
			return &coro_1;
		};
		Coroutine coro2( coro_fnc, 1024 );
		std::thread th( [ &coro2 ](){ assert( coro2.SwitchTo() ); } );
		th.join();
		assert( coro2.SwitchTo() );

		printf( "%s\n", "main finish" );
		fflush( stdout );
		return 0;
	}

	return 0;
}
