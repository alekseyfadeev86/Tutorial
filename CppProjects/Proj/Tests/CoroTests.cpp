#include "Tests.hpp"
#include "Coro.hpp"
#include <stdio.h>
#include <thread>
#include <atomic>
#include <memory>

#ifdef NDEBUG
	#undef NDEBUG
	#include <assert.h>
	#define NDEBUG
#else
	#include <assert.h>
#endif

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
	using namespace Bicycle;
	using namespace Coro;

	ThreadLocal loc;
	loc.Set( ( void* ) 12345 );
	MY_CHECK_ASSERT( ( int64_t ) loc.Get() == 12345 );

	std::thread th( [ &loc ]()
	{
		MY_CHECK_ASSERT( loc.Get() == nullptr );
		loc.Set( ( void* ) 987654 );
		MY_CHECK_ASSERT( ( int64_t ) loc.Get() == 987654 );
	});
	th.join();
	MY_CHECK_ASSERT( ( int64_t ) loc.Get() == 12345 );
}

void coro_tests()
{
	test_thread_local();

	using namespace Bicycle;
	using namespace Coro;

	try
	{
		Coroutine coro;
	}
	catch( ... )
	{
		MY_CHECK_ASSERT( false );
	}

	try
	{
		Coroutine coro;
	}
	catch( ... )
	{
		MY_CHECK_ASSERT( false );
	}

	try
	{
		Coroutine coro( []() -> Coroutine* { return nullptr; }, 10*1024 );
		coro.SwitchTo();
		MY_CHECK_ASSERT( false );
	}
	catch( const Exception &exc )
	{
		MY_CHECK_ASSERT( exc.ErrorCode == ErrorCodes::FromThreadToCoro );
	}
	catch( ... )
	{
		MY_CHECK_ASSERT( false );
	}

	Coroutine coro1;

	try
	{
		Coroutine coro2;
		MY_CHECK_ASSERT( false );
	}
	catch( const Exception &exc )
	{
		MY_CHECK_ASSERT( exc.ErrorCode == ErrorCodes::CoroToCoro );
		MY_CHECK_ASSERT( std::string( exc.what() ) == "Cannot convert coroutine to coroutine" );
	}
	catch( ... )
	{
		MY_CHECK_ASSERT( false );
	}

	Coroutine *prev_coro = nullptr;
	MY_CHECK_ASSERT( coro1.SwitchTo( &prev_coro ) );
	MY_CHECK_ASSERT( prev_coro == &coro1 );
	MY_CHECK_ASSERT( !coro1.IsDone() );

	{
		int val = -1;
		Coroutine coro2( [ &coro1, &val ]() -> Coroutine*
		{
			//printf( "%s\n", "from coro2" );
			//fflush( stdout );
			val = 123;
			return &coro1;
		}, 10*1024);

		Coroutine *prev_coro = nullptr;
		MY_CHECK_ASSERT( coro2.SwitchTo( &prev_coro ) );
		MY_CHECK_ASSERT( prev_coro == &coro2 );
		MY_CHECK_ASSERT( coro2.IsDone() );
		MY_CHECK_ASSERT( val == 123 );
		//printf( "%s\n", "after return from coro2" );
		//fflush( stdout );
	}

	Coro::Coroutine *coro2_ptr;

	std::atomic<bool> barrier( false );
	std::atomic<bool> run( true );

	const int64_t main_thread_id = GetCurrentThreadId();
	int64_t sec_thread_id = 0;
	auto thread_func = [ & ]() -> Coroutine*
	{
		sec_thread_id = GetCurrentThreadId();

		auto coro_fnc = [ & ]() -> Coroutine*
		{
			// Сюда попадает дочерний поток, и только он выполняет эту сопрограмму
			barrier.store( true );

			MY_CHECK_ASSERT( GetCurrentThreadId() == sec_thread_id );
			//printf( "tid: %i, %s\n", GetCurrentThreadId(), "coro_fnc" );
			//fflush( stdout );

			// Ждём, когда основной поотк завершит свои дела в сопрограмме coro2_ptr
			while( run.load() )
			{
				std::this_thread::yield();
			}

			MY_CHECK_ASSERT( GetCurrentThreadId() == sec_thread_id );
			//printf( "tid: %i, %s\n", GetCurrentThreadId(), "coro_fnc end" );
			//fflush( stdout );
			return coro2_ptr;
		};

		// Преобразуем поток в сопрограмму
		Coroutine coro2;
		coro2_ptr = &coro2;

		// Создаём новую сопрограмму
		Coroutine coro3( coro_fnc, 10*1024 );

		//printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_0" );
		//fflush( stdout );

		// Дочерний поток переходит в сопрограмму coro3
		// основной - в текущую (coro2)
		Coroutine *prev_coro = nullptr;
		MY_CHECK_ASSERT( GetCurrentThreadId() == sec_thread_id );
		MY_CHECK_ASSERT( coro3.SwitchTo( &prev_coro ) );
		MY_CHECK_ASSERT( GetCurrentThreadId() == main_thread_id );
		MY_CHECK_ASSERT( prev_coro == &coro1 );
		MY_CHECK_ASSERT( !coro1.IsDone() );

		//printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_1" );
		//fflush( stdout );

		// Пытаемся перейти в coro3, но неудачно, т.к. она уже выполняется дочерним потоком
		MY_CHECK_ASSERT( !coro3.SwitchTo() );
		MY_CHECK_ASSERT( !coro3.IsDone() );

		// Основной поток переходит их этой сопрограммы в coro1, а дочерний
		// возвращается в эту (coro2) после завершения сопрограммы coro3
		MY_CHECK_ASSERT( GetCurrentThreadId() == main_thread_id );
		MY_CHECK_ASSERT( coro1.SwitchTo( &prev_coro ) );
		MY_CHECK_ASSERT( GetCurrentThreadId() == sec_thread_id );
		MY_CHECK_ASSERT( prev_coro == &coro3 );
		MY_CHECK_ASSERT( coro3.IsDone() );
		MY_CHECK_ASSERT( !coro3.SwitchTo() );
		//printf( "tid: %i, %s\n", GetCurrentThreadId(), "thread coro_2" );
		//fflush( stdout );

		return 0;
	}; // auto thread_func = [ & ]() -> Coroutine*

	//printf( "tid: %i, %s\n", GetCurrentThreadId(), "main begin" );
	//fflush( stdout );

	std::thread hthread( thread_func );

	while( !barrier.load() )
	{
		std::this_thread::yield();
	}

	// Переходим в основную сопрограмму дочернего потока,
	// затем выходим из неё
	MY_CHECK_ASSERT( GetCurrentThreadId() == main_thread_id );
	MY_CHECK_ASSERT( coro2_ptr->SwitchTo( &prev_coro ) );
	MY_CHECK_ASSERT( GetCurrentThreadId() == main_thread_id );
	MY_CHECK_ASSERT( prev_coro == coro2_ptr );

	//printf( "tid: %i, %s\n", GetCurrentThreadId(), "main" );
	//fflush( stdout );
	run.store( false );
	hthread.join();

	Coroutine *th1_coro_ptr = nullptr;
	Coroutine *th2_coro_ptr = nullptr;
	int64_t th1_id = 0;
	int64_t th2_id = 0;
	std::atomic<uint8_t> N( 0 );

	std::thread th1( [ & ]
	{
		Coroutine main_coro;
		th1_coro_ptr = &main_coro;
		th1_id = GetCurrentThreadId();

		{
			Coroutine add_coro( [ & ]() -> Coroutine*
			{
				++N;
				while( N.load() < 3 )
				{
					std::this_thread::yield();
				}
				MY_CHECK_ASSERT( th2_coro_ptr != nullptr );
				return th2_coro_ptr;
			}, 10*1024 );

			MY_CHECK_ASSERT( add_coro.SwitchTo() );
			MY_CHECK_ASSERT( GetCurrentThreadId() == th2_id );
			++N;

			while( !add_coro.IsDone() )
			{
				std::this_thread::yield();
			}
		}
	} );

	std::thread th2( [ & ]
	{
		Coroutine main_coro;
		th2_coro_ptr = &main_coro;
		th2_id = GetCurrentThreadId();

		++N;
		while( N.load() < 2 )
		{
			std::this_thread::yield();
		}

		MY_CHECK_ASSERT( th1_coro_ptr != nullptr );
		MY_CHECK_ASSERT( th1_coro_ptr->SwitchTo() );
		MY_CHECK_ASSERT( GetCurrentThreadId() == th1_id );
	});

	th1.join();
	th2.join();

	/*if( 0 )
	{
		// Проверка падения при неверной функции сопрограммы
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
//			MY_CHECK_ASSERT( coro1.SwitchTo() );
			return &coro_1;
		};
		Coroutine coro2( coro_fnc, 1024 );
		std::thread th( [ &coro2 ](){ MY_CHECK_ASSERT( coro2.SwitchTo() ); } );
		th.join();

		MY_CHECK_ASSERT( coro2.SwitchTo() );

		printf( "%s\n", "main finish" );
		fflush( stdout );
	} // if( 0 )*/
} // void coro_tests()
