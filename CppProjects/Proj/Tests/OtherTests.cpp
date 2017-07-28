#include "Tests.hpp"
#include "Errors.hpp"
#include "Utils.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#ifdef NDEBUG
	#undef NDEBUG
	#include <assert.h>
	#define NDEBUG
#else
	#include <assert.h>
#endif

using namespace Bicycle;

void errors_test()
{
	Error example;
	MY_CHECK_ASSERT( !example );
	MY_CHECK_ASSERT( ( example.Code == ErrorCodes::Success ) && example.What.empty() );

	example.Code = 123;
	example.What = "qaz";
	MY_CHECK_ASSERT( example );

	Error fake;
	Error test( example );
	MY_CHECK_ASSERT( ( test.Code == example.Code ) && ( test.What == example.What ) );
	MY_CHECK_ASSERT( ( bool ) test == ( bool ) example );

	Error test2;
	test2 = example;
	MY_CHECK_ASSERT( ( test2.Code == example.Code ) && ( test2.What == example.What ) );
	MY_CHECK_ASSERT( ( bool ) test2 == ( bool ) example );

	Error test3( std::move( test ) );
	MY_CHECK_ASSERT( ( test3.Code == example.Code ) && ( test3.What == example.What ) );
	MY_CHECK_ASSERT( ( bool ) test3 == ( bool ) example );
	MY_CHECK_ASSERT( ( test.Code == fake.Code ) && ( test.What == fake.What ) );
	MY_CHECK_ASSERT( ( bool ) test == ( bool ) fake );

	test = std::move( test2 );
	MY_CHECK_ASSERT( ( test.Code == example.Code ) && ( test.What == example.What ) );
	MY_CHECK_ASSERT( ( bool ) test == ( bool ) example );
	MY_CHECK_ASSERT( ( test2.Code == fake.Code ) && ( test2.What == fake.What ) );
	MY_CHECK_ASSERT( ( bool ) test2 == ( bool ) fake );
}

void spin_lock_test()
{
	SpinLock sl;
	std::atomic<int64_t> Counter( 0 );
	std::vector<std::thread> threads( 10 );

	auto h = [ &sl, &Counter ]()
	{
		for( uint16_t t = 0; t < 100; ++t )
		{
			sl.Lock();
			MY_CHECK_ASSERT( ++Counter == 1 );
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
			MY_CHECK_ASSERT( --Counter == 0 );
			sl.Unlock();
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( h );
	}

	for( auto &th : threads )
	{
		th.join();
	}
	MY_CHECK_ASSERT( Counter.load() == 0 );

	SharedSpinLock shar_sl;

	auto h2 = [ &shar_sl, &Counter ]()
	{
		for( uint16_t t = 0; t < 100; ++t )
		{
			if( ( t % 2 ) == 0 )
			{
				shar_sl.Lock();
				MY_CHECK_ASSERT( --Counter == -1 );
				std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
				MY_CHECK_ASSERT( ++Counter == 0 );
				shar_sl.Unlock();
			}
			else
			{
				shar_sl.SharedLock();
				MY_CHECK_ASSERT( ++Counter > 0 );
				std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
				MY_CHECK_ASSERT( --Counter >= 0 );
				shar_sl.Unlock();
			}
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( h2 );
	}

	for( auto &th : threads )
	{
		th.join();
	}
	MY_CHECK_ASSERT( Counter.load() == 0 );
} // void spin_lock_test()

struct TestLocker
{
	int64_t Flag;
	TestLocker(): Flag( 0 ) {}
	TestLocker( const TestLocker& ) = delete;
	TestLocker& operator=( const TestLocker& ) = delete;

	void Lock()
	{
		MY_CHECK_ASSERT( Flag == 0 );
		Flag = -1;
	}

	void SharedLock()
	{
		MY_CHECK_ASSERT( Flag >= 0 );
		++Flag;
	}

	void Unlock()
	{
		if( Flag < 0 )
		{
			MY_CHECK_ASSERT( Flag == -1 );
			Flag = 0;
		}
		else if( Flag > 0 )
		{
			--Flag;
		}
	}
};

void guards_test()
{
	TestLocker l;
	MY_CHECK_ASSERT( l.Flag == 0 );
	{
		LockGuard<TestLocker> lock( l );
		MY_CHECK_ASSERT( l.Flag == -1 );
	}

	MY_CHECK_ASSERT( l.Flag == 0 );
	{
		SharedLockGuard<TestLocker> lock1( l );
		MY_CHECK_ASSERT( l.Flag == 1 );
		{
			SharedLockGuard<TestLocker> lock2( l );
			MY_CHECK_ASSERT( l.Flag == 2 );
		}
		MY_CHECK_ASSERT( l.Flag == 1 );
	}

	MY_CHECK_ASSERT( l.Flag == 0 );
	{
		UniqueLocker<TestLocker> lock1;
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( !lock1 );

		UniqueLocker<TestLocker> lock2( l, false );
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( lock2 );
		MY_CHECK_ASSERT( !lock2.Locked() );

		UniqueLocker<TestLocker> lock3( l, true );
		MY_CHECK_ASSERT( l.Flag == -1 );
		MY_CHECK_ASSERT( lock3 );
		MY_CHECK_ASSERT( lock3.Locked() );

		UniqueLocker<TestLocker> lock4( std::move( lock3 ) );
		MY_CHECK_ASSERT( l.Flag == -1 );
		MY_CHECK_ASSERT( lock4 );
		MY_CHECK_ASSERT( lock4.Locked() );
		MY_CHECK_ASSERT( !lock3 );

		lock1 = std::move( lock4 );
		MY_CHECK_ASSERT( l.Flag == -1 );
		MY_CHECK_ASSERT( lock1 );
		MY_CHECK_ASSERT( lock1.Locked() );
		MY_CHECK_ASSERT( !lock4 );

		lock1.Unlock();
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( lock1 );
		MY_CHECK_ASSERT( !lock1.Locked() );

		lock2.Lock();
		MY_CHECK_ASSERT( l.Flag == -1 );
		MY_CHECK_ASSERT( lock2 );
		MY_CHECK_ASSERT( lock2.Locked() );
	}

	MY_CHECK_ASSERT( l.Flag == 0 );
	{
		SharedLocker<TestLocker> lock1;
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( !lock1 );

		SharedLocker<TestLocker> lock2( l, false );
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( lock2 );
		MY_CHECK_ASSERT( !lock2.Locked() );

		SharedLocker<TestLocker> lock3( l, true );
		MY_CHECK_ASSERT( l.Flag == 1 );
		MY_CHECK_ASSERT( lock3 );
		MY_CHECK_ASSERT( lock3.Locked() );

		SharedLocker<TestLocker> lock4( std::move( lock3 ) );
		MY_CHECK_ASSERT( l.Flag == 1 );
		MY_CHECK_ASSERT( lock4 );
		MY_CHECK_ASSERT( lock4.Locked() );
		MY_CHECK_ASSERT( !lock3 );

		lock1 = std::move( lock4 );
		MY_CHECK_ASSERT( l.Flag == 1 );
		MY_CHECK_ASSERT( lock1 );
		MY_CHECK_ASSERT( lock1.Locked() );
		MY_CHECK_ASSERT( !lock4 );

		lock1.Unlock();
		MY_CHECK_ASSERT( l.Flag == 0 );
		MY_CHECK_ASSERT( lock1 );
		MY_CHECK_ASSERT( !lock1.Locked() );

		lock2.Lock();
		MY_CHECK_ASSERT( l.Flag == 1 );
		MY_CHECK_ASSERT( lock2 );
		MY_CHECK_ASSERT( lock2.Locked() );

		{
			SharedLocker<TestLocker> lock5( l, true );
			MY_CHECK_ASSERT( l.Flag == 2 );
			MY_CHECK_ASSERT( lock5 );
			MY_CHECK_ASSERT( lock5.Locked() );
		}

		MY_CHECK_ASSERT( l.Flag == 1 );
	}
	MY_CHECK_ASSERT( l.Flag == 0 );
} // void guards_test()

void defer_test()
{
	int64_t i = false;
	Defer def1;
	MY_CHECK_ASSERT( !def1 );

	Defer def2( [ &i ]() { ++i; } );
	MY_CHECK_ASSERT( def2 );
	def2();
	MY_CHECK_ASSERT( !def2 );
	MY_CHECK_ASSERT( i == 1 );

	{
		Defer def3( [ &i ]() { ++i; } );
		MY_CHECK_ASSERT( def3 );
	}
	MY_CHECK_ASSERT( i == 2 );
}

void utils_tests()
{
	spin_lock_test();
	guards_test();
	defer_test();
}
