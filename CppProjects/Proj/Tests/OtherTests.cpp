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

void timer_test()
{
	{
		CancellableTask task1;
		MY_CHECK_ASSERT( !task1 );
		MY_CHECK_ASSERT( !task1.Cancel() );
	
		int64_t count = 0;
		CancellableTask task2( [ &count ](){ ++count; } );
		MY_CHECK_ASSERT( task2 );
		task2();
		MY_CHECK_ASSERT( count == 1 );
		MY_CHECK_ASSERT( !task2 );
		MY_CHECK_ASSERT( !task2.Cancel() );
		task2();
		MY_CHECK_ASSERT( count == 1 );
		MY_CHECK_ASSERT( !task2 );
		MY_CHECK_ASSERT( !task2.Cancel() );
		
		CancellableTask task3( [ &count ](){ ++count; } );
		MY_CHECK_ASSERT( task3 );
		MY_CHECK_ASSERT( task3.Cancel() );
		MY_CHECK_ASSERT( !task3 );
		MY_CHECK_ASSERT( !task3.Cancel() );
		task3();
		MY_CHECK_ASSERT( count == 1 );
		MY_CHECK_ASSERT( !task3 );
		MY_CHECK_ASSERT( !task3.Cancel() );
	}
	
	auto timer_ptr = TimeTasksQueue::GetQueue();
	MY_CHECK_ASSERT( timer_ptr );
	
	std::weak_ptr<TimeTasksQueue> timer_wptr( timer_ptr );
	timer_ptr.reset();
	MY_CHECK_ASSERT( !timer_ptr );
	MY_CHECK_ASSERT( timer_wptr.expired() );
	MY_CHECK_ASSERT( !timer_wptr.lock() );
	
	auto timer_ptr2 = TimeTasksQueue::GetQueue();
	MY_CHECK_ASSERT( timer_ptr2 );
	timer_wptr = timer_ptr2;
	timer_ptr = TimeTasksQueue::GetQueue();
	MY_CHECK_ASSERT( timer_ptr );
	MY_CHECK_ASSERT( timer_ptr.get() == timer_ptr2.get() );
	timer_ptr2.reset();
	MY_CHECK_ASSERT( !timer_wptr.expired() );
	
	typedef std::chrono::system_clock clock_type;
	
	const clock_type::time_point t0 = clock_type::now();
	
	const uint8_t N( 3 );
	clock_type::time_point tp[ N ];
#ifdef _DEBUG
	uint64_t microsec_timeouts[ N ] = { 1000*1000, 2000*1000, 3000*1000 };
	//uint64_t microsec_timeouts[ N ] = { 1000*1000, 20*1000*1000, 300*000*1000 };
#else
#error "вернуть"
	uint64_t microsec_timeouts[ N ] = { 1000*1000, 2000*1000, 3000*1000 };
#endif
	
	std::mutex mut;
	std::condition_variable cv;
	uint8_t counter( 0 );
	
	auto task = [ &tp, &counter, &mut, &cv, N ]( uint8_t i )
	{
		tp[ i ] = clock_type::now();
		
		std::lock_guard<std::mutex> lock( mut );
		if( ( ++counter ) == N )
		{
			cv.notify_all();
		}
	};
	
	typedef TimeTasksQueue::task_type task_type;
	
	for( uint8_t t = 0; t < N; ++t )
	{
		task_type task0( new CancellableTask( [ task, t ](){ task( t ); } ) );
		std::weak_ptr<CancellableTask> task_wptr( task0 );
		timer_ptr->Post( task0, microsec_timeouts[ t ] );
		MY_CHECK_ASSERT( task0 );
		task0.reset();
		MY_CHECK_ASSERT( !task_wptr.expired() );
	}
	
	bool chk = false;
	task_type task1( new CancellableTask( [ &chk ](){ chk = true; } ) );
	timer_ptr->Post( task1, 1500*1000 );
	std::weak_ptr<CancellableTask> task_wptr( task1 );
	MY_CHECK_ASSERT( task1->Cancel() );
	task1.reset();
	
	{
		std::unique_lock<std::mutex> lock( mut );
		if( counter < N )
		{
			auto res = cv.wait_for( lock, std::chrono::seconds( 5 ) );
			MY_CHECK_ASSERT( res != std::cv_status::timeout );
		}
	}
	
	MY_CHECK_ASSERT( task_wptr.expired() );
	MY_CHECK_ASSERT( !chk );
	for( uint8_t t = 0; t < N; ++t )
	{
		MY_CHECK_ASSERT( tp[ t ] > t0 );
		
		const uint64_t diff = std::chrono::duration_cast<std::chrono::microseconds>( tp[ t ] - t0 ).count();
		MY_CHECK_ASSERT( diff >= ( microsec_timeouts[ t ] - 100*1000 ) );
		MY_CHECK_ASSERT( diff <= ( microsec_timeouts[ t ] + 100*1000 ) );
	}
	
	task1.reset( new CancellableTask( [ &chk ](){ chk = true; } ) );
	task_wptr = task1;
	MY_CHECK_ASSERT( !task_wptr.expired() );
	timer_ptr->Post( task1, 1500*1000 );
	task1.reset();
	MY_CHECK_ASSERT( !task_wptr.expired() );
	timer_wptr = timer_ptr;
	timer_ptr.reset();
	MY_CHECK_ASSERT( timer_wptr.expired() );
	MY_CHECK_ASSERT( task_wptr.expired() );
} // void timer_test()

void utils_tests()
{
	spin_lock_test();
	guards_test();
	defer_test();
	timer_test();
}
