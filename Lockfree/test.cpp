#define UNITTEST

#include <iostream>

#include <set>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <assert.h>
#include "LockFree.h"

namespace StackTest
{
	/// Проверяет стек из предположения, что он пуст
	template <typename StackT>
	void TestStackEmpty( StackT &stack )
	{
		try
		{
			stack.Pop();
			assert( false );
		}
		catch( const std::out_of_range &exc )
		{}
		catch( ... )
		{
			assert( false );
		}
	}

	/// Проверка удаления элементов и соответствия их значений и количества требуемым
	template <typename StackT>
	void TestStackRemove( StackT &stack,
	                      const std::vector<typename StackT::Type> &test_vec )
	{
		typedef typename StackT::Type Type;
		try
		{
			for( size_t t = test_vec.size(); t > 0; --t )
			{
				assert( stack.Pop() == test_vec[ t - 1 ] );
			}
		}
		catch( ... )
		{
			assert( false );
		}
	}

	/// Проверка добавления и удаления элементов
	template <typename StackT>
	void TestStackAddRemove( StackT &stack,
	                         const std::vector<typename StackT::Type> &test_vec )
	{
		typedef typename StackT::Type Type;

		try
		{
			for( Type val : test_vec )
			{
				stack.Push( val );
			}
		}
		catch( ... )
		{
			assert( false );
		}

		TestStackRemove( stack, test_vec );
	}

	/// Общие проверки
	template <typename StackT>
	void CommonStackTest( const std::vector<typename StackT::Type> &test_vec )
	{
		typedef typename StackT::Type Type;

		StackT stack;
		TestStackEmpty( stack );
		TestStackAddRemove( stack, test_vec );
	}

	template <typename T>
	void TestStackUnsafe( const std::vector<T> &test_vec,
	                      const std::function<bool( T )> &validator )
	{
		typename LockFree::ForwardList<T>::Unsafe ul;
		assert( !ul );

		std::vector<T> vec_to_compare;
		try
		{
			for( T v : test_vec )
			{
				ul.Push( v );
				if( !validator( v ) )
				{
					vec_to_compare.push_back( v );
				}
				assert( ul );
			}
		}
		catch( ... )
		{
			assert( false );
		}

		ul.RemoveIf( validator );

		try
		{
			for( size_t t = vec_to_compare.size(); t > 0; --t )
			{
				assert( ul.Pop() == vec_to_compare[ t - 1 ] );
				assert( ( bool ) ul == ( t > 1 ) );
			}
		}
		catch( ... )
		{
			assert( false );
		}

		TestStackEmpty( ul );
	}

	template <typename T>
	void TestStacks( const std::vector<T> &test_vec,
	                 const std::function<bool( T )> &validator )
	{
		std::cout << "-------0------\n";
		CommonStackTest<typename LockFree::ForwardList<T>::Unsafe>( test_vec );
		std::cout << "-------1------\n";
		TestStackUnsafe<T>( test_vec, validator );
		std::cout << "-------2------\n";

		LockFree::ForwardList<T> fl;
		for( T val : test_vec )
		{
			fl.Push( val );
		}

		auto flu1 = fl.Release();
		auto flu2 = fl.Release();
		assert( flu1 );
		TestStackEmpty( flu2 );
		std::cout << "-------3------\n";
		TestStackRemove( flu1, test_vec );
		std::cout << "-------4------\n";
		for( T val : test_vec )
		{
			fl.Push( val );
		}
		flu1 = fl.Release();
		std::cout << "-------5------\n";
		fl.Push( flu1 );
		TestStackEmpty( flu1 );
		std::cout << "-------6------\n";
		flu1 = fl.Release();
		TestStackRemove( flu1, test_vec );
	}
} // namespace StackTest

void forward_list_test()
{
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		const std::vector<LockFree::DebugStruct> test_vec( { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
		StackTest::TestStacks<LockFree::DebugStruct>( test_vec,
													  []( LockFree::DebugStruct val ) -> bool { return ( val.Val % 2 ) == 0; } );
	}

	const uint8_t th_num = 10;
	const uint16_t count = 1000;
	std::atomic<uint16_t> Diff( 0 );
	LockFree::ForwardList<LockFree::DebugStruct> fl;
	std::atomic<uint8_t> finished_count( 0 );
	std::vector<uint16_t> received[ th_num ];
	std::atomic<uint16_t> T( 0 );

	auto producer = [ & ]()
	{
		uint16_t diff = Diff++;

		for( uint16_t t = 0; t < count; ++t )
		{
			fl.Push( ( int64_t ) ( th_num * t + diff ) );
		}

		++finished_count;
	};

	auto consumer = [ & ]()
	{
		uint16_t t = T++;
#ifdef _DEBUG
		assert( t < th_num );
#endif
		received[ t ].reserve( 2 * count );

		while( true )
		{
			auto flu = fl.Release();
			if( flu )
			{
				received[ t ].push_back( ( uint16_t ) flu.Pop().Val );
			}
			else if( finished_count.load() >= th_num )
			{
				break;
			}

			fl.Push( flu );
		}
	};

	std::vector<std::thread> threads;
	for( uint8_t t = 0; t < 2*th_num; ++t )
	{
		if( ( t % 2 ) == 0 )
		{
			threads.push_back( std::thread( producer ) );
		}
		else
		{
			threads.push_back( std::thread( consumer ) );
		}
	}

	std::set<uint16_t> chk;
	for( uint16_t t = 0; t < count; ++t )
	{
		for( uint16_t i = 0; i < th_num; ++i )
		{
			chk.insert( th_num * t + i );
		}
	}

	for( auto &th : threads )
	{
		if( th.joinable() )
		{
			th.join();
		}
	}

	for( uint16_t t = 0; t < th_num; ++t )
	{
		for( uint16_t val : received[ t ] )
		{
			auto iter = chk.find( val );
			assert( iter != chk.end() );
			chk.erase( iter );
		}
	}

	assert( chk.empty() );
	assert( LockFree::DebugStruct::GetCounter() == 0 );
} // void forward_list_test()

void deferred_deleter_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		DeferredDeleter<LockFree::DebugStruct> def_queue( 1 );
		const uint16_t MaxCount( 1000 );

		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue.Delete( new LockFree::DebugStruct( t ) );
		}
		assert( LockFree::DebugStruct::GetCounter() == 0 );

		auto epoch = def_queue.EpochAcquire();

		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue.Delete( new LockFree::DebugStruct( t ) );
		}
		assert( LockFree::DebugStruct::GetCounter() == MaxCount );
		def_queue.Clear();
		assert( LockFree::DebugStruct::GetCounter() == MaxCount );
		epoch.Release();
		def_queue.Clear();
		assert( LockFree::DebugStruct::GetCounter() == 0 );

		DeferredDeleter<LockFree::DebugStruct> def_queue2( 2 );
		auto epoch1 = def_queue2.EpochAcquire();
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue2.Delete( new LockFree::DebugStruct( t ) );
		}
		assert( LockFree::DebugStruct::GetCounter() == MaxCount );
		auto epoch2 = def_queue2.EpochAcquire();
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue2.Delete( new LockFree::DebugStruct( t ) );
		}
		assert( LockFree::DebugStruct::GetCounter() == 2*MaxCount );
		epoch1.Release();
		def_queue2.Clear();
		assert( LockFree::DebugStruct::GetCounter() == MaxCount );
		epoch2.Release();
		def_queue2.Clear();
		assert( LockFree::DebugStruct::GetCounter() == 0 );
	} // if( !Checked.exchange( true ) )

	// Многопоточная проверка
	const uint8_t threads_count = 10;
	DeferredDeleter<LockFree::DebugStruct> def_queue( threads_count );
	std::atomic<int64_t> counter( 0 );
	std::atomic<bool> run( false );
	auto epoch_catcher = [ &counter, threads_count, &def_queue, &run ]()
	{
		try
		{
			while( !run.load() )
			{
				std::this_thread::yield();
			}

			auto ep_keeper = def_queue.EpochAcquire();
			assert( ++counter <= threads_count );
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
			assert( --counter < threads_count );
			ep_keeper.Release();
		}
		catch( ... )
		{
			assert( false );
		}
	};

	std::vector<std::thread> th_vec;
	th_vec.reserve( 2* threads_count );
	for( uint8_t t = 0; t < 2*threads_count; ++t )
	{
		th_vec.push_back( std::thread( epoch_catcher ) );
	}

	run.store( true );

	for( auto &th : th_vec )
	{
		th.join();
	}

	run.store( false );
	auto handler = [ &def_queue, &run ]()
	{
		while( !run.load() )
		{
			std::this_thread::yield();
		}

		for( uint32_t t = 0; t < 10000; ++t )
		{
			auto ep_keeper = def_queue.EpochAcquire();

			def_queue.Delete( new LockFree::DebugStruct( t ) );

			if( t % 2 == 0 )
			{
				ep_keeper.Release();
			}

			def_queue.Clear();
		}

		def_queue.Clear();
	};

	th_vec.clear();
	for( auto t = 0; t < threads_count; ++t )
	{
		th_vec.push_back( std::thread( handler ) );
	}

	run.store( true );

	for( auto &th : th_vec )
	{
		th.join();
	}
	assert( LockFree::DebugStruct::GetCounter() == 0 );
} // void deferred_deleter_test()

void test_stack_empty( LockFree::Stack<LockFree::DebugStruct> &stack )
{
	try
	{
		LockFree::DebugStruct test_elem( -123 );
		auto elem = stack.Pop( &test_elem );
		assert( elem.Val == test_elem.Val );
		auto elem2 = stack.Pop();
		assert( false );
	}
	catch( const std::out_of_range& )
	{}
	catch( ... )
	{
		assert( false );
	}
}

void stack_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		Stack<LockFree::DebugStruct> stack( 1 );
		test_stack_empty( stack );

		stack.Push( 1 );
		stack.Push( LockFree::DebugStruct( 2 ) );
		assert( LockFree::DebugStruct::GetCounter() == 2 );

		try
		{
			bool is_empty = true;
			auto elem1 = stack.Pop( nullptr, &is_empty );
			assert( elem1.Val == 2 );
			assert( !is_empty );
			auto elem2 = stack.Pop( nullptr, &is_empty );
			assert( elem2.Val == 1 );
			assert( is_empty );
			test_stack_empty( stack );
		}
		catch( ... )
		{
			assert( false );
		}

		assert( LockFree::DebugStruct::GetCounter() == 0 );
		{
			Stack<LockFree::DebugStruct> stack2( 1 );
			stack2.Push( 1 );
			stack2.Push( LockFree::DebugStruct( 2 ) );
			assert( LockFree::DebugStruct::GetCounter() == 2 );
		}
		assert( LockFree::DebugStruct::GetCounter() == 0 );
	}

	// Многопоточная проверка
	static const uint8_t ThreadsNum( 10 );
	static const uint16_t OneThreadOpsNum( 10 );
	Stack<LockFree::DebugStruct> stack( ThreadsNum );
	std::vector<uint16_t> readed_values[ ThreadsNum ];
	std::atomic<uint8_t> N( 0 );

	auto h = [ & ]()
	{
		uint8_t n = N++;
		assert( n < ThreadsNum );
		readed_values[ n ].reserve( OneThreadOpsNum );
		bool is_ready( false );

		try
		{
			for( uint16_t t = 0; t < 2*OneThreadOpsNum; ++t )
			{
				if( ( t % 2 ) == 0 )
				{
					stack.Push( t );
				}
				else
				{
					LockFree::DebugStruct Fake( -1 );
					auto val = stack.Pop( &Fake );
					if( val.Val != Fake.Val )
					{
						readed_values[ n ].push_back( val.Val );
					}
				}
			}

			is_ready = true;

			while( 1 )
			{
				auto val = stack.Pop();
				assert( val.Val >= 0 );
				readed_values[ n ].push_back( val.Val );
			}
		}
		catch( ... )
		{
			assert( is_ready );
		}
	};

	std::vector<std::thread> threads( ThreadsNum );
	for( auto &th : threads )
	{
		th = std::thread( h );
	}

	for( auto &th : threads )
	{
		th.join();
	}
	assert( LockFree::DebugStruct::GetCounter() == 0 );

	std::map<uint16_t, uint8_t> m;
	for( auto &vec : readed_values )
	{
		for( uint16_t v : vec )
		{
			auto res = m.insert( std::make_pair( v, 1 ) );
			if( !res.second )
			{
				++( res.first->second );
			}
		}
	}

	for( uint16_t t = 0; t < OneThreadOpsNum; ++t )
	{
		auto iter = m.find( 2*t );
		assert( iter != m.end() );
		assert( iter->second == ThreadsNum );
	}

	std::atomic<bool> run( false );
	std::atomic<int64_t> counter( 0 );
	auto h2 = [ & ]()
	{
		while( !run.load() )
		{}

		if( stack.Push( 1 ) )
		{
			++counter;
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( h2 );
	}

	run.store( true );

	for( auto &th : threads )
	{
		th.join();
	}

	assert( counter.load() == 1 );

	run.store( false );
	counter.store( 0 );
	auto h3 = [ & ]()
	{
		while( !run.load() )
		{}

		try
		{
			bool is_empty = false;
			stack.Pop( nullptr, &is_empty );
			if( is_empty )
			{
				++counter;
			}
		}
		catch( ... )
		{
			assert( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( h3 );
	}

	run.store( true );

	for( auto &th : threads )
	{
		th.join();
	}

	assert( counter.load() == 1 );

	run.store( false );
	counter.store( 0 );
	auto h4 = [ & ]()
	{
		while( !run.load() )
		{}

		for( uint8_t t = 0; t < 10; ++t )
		{
			if( stack.Push( t ) )
			{
				++counter;
			}
		}

		try
		{
			while( true )
			{
				bool is_empty = false;
				stack.Pop( nullptr, &is_empty );
				if( is_empty )
				{
					--counter;
					break;
				}
			}
		}
		catch( const std::out_of_range& )
		{}
		catch( ... )
		{
			assert( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( h4 );
	}

	run.store( true );

	for( auto &th : threads )
	{
		th.join();
	}

	assert( counter.load() == 0 );
} // void stack_test()

void queue_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		Queue<LockFree::DebugStruct> queue( 1 );
		auto elem_ptr = queue.Pop();
		assert( !elem_ptr );

		queue.Push( 1 );
		queue.Push( LockFree::DebugStruct( 2 ) );
		assert( LockFree::DebugStruct::GetCounter() == 2 );

		{
			auto elem_ptr1 = queue.Pop();
			assert( elem_ptr1 );
			assert( elem_ptr1.Get()->Val == 1 );
			auto elem_ptr2 = queue.Pop();
			assert( elem_ptr2 );
			assert( elem_ptr2.Get()->Val == 2 );
			auto elem_ptr3 = queue.Pop();
			assert( !elem_ptr3 );
		}

		assert( LockFree::DebugStruct::GetCounter() == 0 );
		{
			Queue<LockFree::DebugStruct> queue2( 1 );
			queue2.Push( 1 );
			queue2.Push( LockFree::DebugStruct( 2 ) );
			assert( LockFree::DebugStruct::GetCounter() == 2 );
		}
		assert( LockFree::DebugStruct::GetCounter() == 0 );
	}

	// Многопоточная проверка
	static const uint8_t ThreadsNum( 10 );
	static const uint16_t OneThreadOpsNum( 10 );
	Queue<LockFree::DebugStruct> queue( ThreadsNum );
	std::vector<uint16_t> readed_values[ ThreadsNum ];
	std::atomic<uint8_t> N( 0 );

	auto h = [ & ]()
	{
		uint8_t n = N++;
		assert( n < ThreadsNum );
		readed_values[ n ].reserve( OneThreadOpsNum );

		for( uint16_t t = 0; t < 2*OneThreadOpsNum; ++t )
		{
			if( ( t % 2 ) == 0 )
			{
				queue.Push( t );
			}
			else
			{
				auto val = queue.Pop();
				if( val )
				{
					readed_values[ n ].push_back( val.Get()->Val );
				}
			}
		}

		while( 1 )
		{
			auto val = queue.Pop();
			if( !val )
			{
				break;
			}

			assert( val.Get()->Val >= 0 );
			readed_values[ n ].push_back( val.Get()->Val );
		}
	};

	std::vector<std::thread> threads( ThreadsNum );
	for( auto &th : threads )
	{
		th = std::thread( h );
	}

	for( auto &th : threads )
	{
		th.join();
	}
	assert( LockFree::DebugStruct::GetCounter() == 0 );

	std::map<uint16_t, uint8_t> m;
	for( auto &vec : readed_values )
	{
		for( uint16_t v : vec )
		{
			auto res = m.insert( std::make_pair( v, 1 ) );
			if( !res.second )
			{
				++( res.first->second );
			}
		}
	}

	for( uint16_t t = 0; t < OneThreadOpsNum; ++t )
	{
		auto iter = m.find( 2*t );
		assert( iter != m.end() );
		assert( iter->second == ThreadsNum );
	}
} // void queue_test()

void lockfree_test()
{
	try
	{
		forward_list_test();
		deferred_deleter_test();
		stack_test();
		queue_test();
	}
	catch( const std::exception &exc )
	{
		std::cout << exc.what() << std::endl;
	}
}
 
int main()
{
	// Проверка контейнеров
#ifdef _WIN32
	printf( "Press enter to begin\n" );
#else
	printf( "Нажмите enter, чтобы начать\n" );
#endif
	fflush( stdout );
	getchar();

	for( uint16_t t = 0; t < 1000; ++t )
	{
#ifdef _WIN32
		printf( "Multithread debug, step %i\n", t + 1 );
#else
		printf( "Многопоточная отладка, шаг %i\n", t + 1 );
#endif
		fflush( stdout );
		lockfree_test();
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}

#ifdef _WIN32
	printf( "Ready.Press enter to finish\n" );
#else
	printf( "Готово.Нажмите enter, чтобы закончить\n" );
#endif
	fflush( stdout );
	getchar();
	return 0;
}
