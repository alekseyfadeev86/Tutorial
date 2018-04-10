#include "Tests.hpp"
#include "LockFree.hpp"

#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <thread>
#include <atomic>

#ifdef NDEBUG
	#undef NDEBUG
	#include <assert.h>
	#define NDEBUG
#else
	#include <assert.h>
#endif

namespace StackTest
{
	/// Проверяет стек из предположения, что он пуст
	template <typename StackT>
	void TestStackEmpty( StackT &stack )
	{
		try
		{
			stack.Pop();
			MY_CHECK_ASSERT( false );
		}
		catch( const std::out_of_range &exc )
		{}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	}

	/// Проверка удаления элементов и соответствия их значений и количества требуемым
	template <typename StackT>
	void TestStackRemove( StackT &stack,
						  const std::vector<typename StackT::Type> &test_vec )
	{
		try
		{
			size_t t = test_vec.size();
			for( ; t > 0; --t )
			{
				MY_CHECK_ASSERT( stack.Pop() == test_vec[ t - 1 ] );
			}
			MY_CHECK_ASSERT( t == 0 );
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}

		TestStackEmpty( stack );
	}

	/// Общие проверки
	template <typename StackT>
	void CommonStackTest( const std::vector<typename StackT::Type> &test_vec )
	{
		typedef typename StackT::Type Type;

		StackT stack;
		TestStackEmpty( stack );
		try
		{
			for( const Type &val : test_vec )
			{
				stack.Push( val );
			}
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}

		TestStackRemove( stack, test_vec );
	}

	template <typename T>
	void TestListUnsafe( const std::vector<T> &test_vec,
	                     const std::function<bool( T )> &validator )
	{
		typename LockFree::UnsafeForwardList<T> ul;
		MY_CHECK_ASSERT( !ul );

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
				MY_CHECK_ASSERT( ul );
			}
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}

		ul.RemoveIf( validator );

		try
		{
			for( size_t t = vec_to_compare.size(); t > 0; --t )
			{
				auto val = ul.Pop();
				MY_CHECK_ASSERT( !validator( val ) );
				MY_CHECK_ASSERT( val == vec_to_compare[ t - 1 ] );
				MY_CHECK_ASSERT( ( bool ) ul == ( t > 1 ) );
			}
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}

		TestStackEmpty( ul );

		for( const auto &iter : test_vec )
		{
			ul.Push( iter );
		}
		MY_CHECK_ASSERT( ul.Reverse() == test_vec.size() );

		size_t t = 0;
		while( ul )
		{
			MY_CHECK_ASSERT( ul.Pop() == test_vec[ t++ ] );
		}
		MY_CHECK_ASSERT( t == test_vec.size() );

		size_t sz = test_vec.size();
		for( size_t t = 0; t < ( sz / 2 ); ++t )
		{
			ul.Push( test_vec[ t ] );
		}

		typename LockFree::ForwardList<T>::Unsafe ul2;
		for( size_t t = sz / 2; t < sz; ++t )
		{
			ul2.Push( test_vec[ t ] );
		}
		ul.Push( std::move( ul2 ) );
		MY_CHECK_ASSERT( !ul2 );

		for( size_t t = ( sz - 1 );; --t )
		{
			MY_CHECK_ASSERT( ul );
			MY_CHECK_ASSERT( ul.Pop() == test_vec[ t ] );

			if( t == 0 )
			{
				break;
			}
		}
		MY_CHECK_ASSERT( !ul );
	} // void TestListUnsafe

	template <typename T>
	void TestLists( const std::vector<T> &test_vec,
	                const std::function<bool( T )> &validator )
	{
		CommonStackTest<typename LockFree::ForwardList<T>::Unsafe>( test_vec );
		TestListUnsafe<T>( test_vec, validator );

		LockFree::ForwardList<T> fl;
		for( T val : test_vec )
		{
			fl.Push( val );
		}

		auto flu1 = fl.Release();
		auto flu2 = fl.Release();
		MY_CHECK_ASSERT( flu1 );
		TestStackEmpty( flu2 );
		TestStackRemove( flu1, test_vec );
		for( T val : test_vec )
		{
			fl.Push( val );
		}
		flu1 = fl.Release();
		fl.Push( std::move( flu1 ) );
		TestStackEmpty( flu1 );
		flu1 = fl.Release();
		TestStackRemove( flu1, test_vec );
	}
} // namespace StackTest



void forward_list_test()
{
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
		{
			const std::vector<LockFree::DebugStruct> test_vec( { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
			MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == ( int64_t )test_vec.size() );
			StackTest::TestLists<LockFree::DebugStruct>( test_vec,
														 []( LockFree::DebugStruct val ) -> bool { return ( val.Val % 2 ) == 0; } );
			MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == ( int64_t )test_vec.size() );

			{
				LockFree::ForwardList<LockFree::DebugStruct> fl;
				for( const auto &iter : test_vec )
				{
					fl.Push( iter );
				}
				MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == ( int64_t )( 2*test_vec.size() ) );
			}
			MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == ( int64_t )test_vec.size() );
		}

		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	}

	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

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
			fl.Emplace( ( int64_t ) ( th_num * t + diff ) );
		}

		++finished_count;
	};

	auto consumer = [ & ]()
	{
		uint16_t t = T++;
		MY_CHECK_ASSERT( t < th_num );
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

			fl.Push( std::move( flu ) );
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
			MY_CHECK_ASSERT( iter != chk.end() );
			chk.erase( iter );
		}
	}

	MY_CHECK_ASSERT( chk.empty() );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	
	LockFree::DebugStruct s_1( 123 );
	LockFree::DebugStruct s_2( 456 );
	LockFree::DebugStruct s_3( 789 );
	
	auto flu = fl.Release();
	MY_CHECK_ASSERT( !flu );
	
	MY_CHECK_ASSERT( fl.TryPush( s_1 ) );
	MY_CHECK_ASSERT( !fl.TryPush( s_2 ) );
	MY_CHECK_ASSERT( !fl.TryEmplace( s_3.Val ) );
	
	flu = fl.Release();
	MY_CHECK_ASSERT( flu );
	MY_CHECK_ASSERT( flu.Pop().Val == s_1.Val );
	MY_CHECK_ASSERT( !flu );
	
	MY_CHECK_ASSERT( fl.TryEmplace( s_2.Val ) );
	MY_CHECK_ASSERT( !fl.TryPush( s_1 ) );
	MY_CHECK_ASSERT( !fl.TryEmplace( s_3.Val ) );
	
	flu = fl.Release();
	MY_CHECK_ASSERT( flu );
	MY_CHECK_ASSERT( flu.Pop().Val == s_2.Val );
	MY_CHECK_ASSERT( !flu );
	
	std::atomic<uint64_t> add_count( 0 );
	std::atomic<uint8_t> n( 0 );
	
	const auto task = [ & ]()
	{
		bool f = ( n++ % 2 ) == 0;
		for( uint8_t t = f ? 0 : 1; t < 10; ++t )
		{
			bool chk = ( t % 2 ) == 0 ? fl.TryPush( s_1 ) : fl.TryEmplace( s_2.Val );
			if( chk )
			{
				++add_count;
			}
		}
	};
	
	std::thread threads2[ 4 ];
	for( auto &th : threads2 )
	{
		th = std::thread( task );
	}
	
	for( auto &th : threads2 )
	{
		th.join();
	}
	MY_CHECK_ASSERT( add_count.load() == 1 );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 4 );
} // void forward_list_test()

void deferred_deleter_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		const uint16_t MaxCount( 1000 );
		DeferredDeleter def_queue( 1 );

		// Добавляем MaxCount элементов на удаление,
		// все элементы должны быть удалены сразу, т.к. все эпохи свободны
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue.Delete( new LockFree::DebugStruct( t ) );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

		// Захватываем эпоху, добавляем элементы на удаление, пробуем удалить.
		// Ни одного элемента не должно быть удалено,т.к. эпоха захвачена
		auto epoch = def_queue.EpochAcquire();
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue.Delete( new LockFree::DebugStruct( t ) );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == MaxCount );
		def_queue.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == MaxCount );

		// Освобождаем эпоху, пробуем очистить очередь - все
		// элементы должны быть удалены
		epoch.Release();
		def_queue.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

		DeferredDeleter def_queue2( 2 );
		auto epoch1 = def_queue2.EpochAcquire();
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue2.Delete( new LockFree::DebugStruct( t ) );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == MaxCount );
		auto epoch2 = def_queue2.EpochAcquire();
		for( uint16_t t = 0; t < MaxCount; ++t )
		{
			def_queue2.Delete( new LockFree::DebugStruct( t ) );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 2*MaxCount );
		epoch1.Release();
		def_queue2.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == MaxCount );
		epoch2.Release();
		def_queue2.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

		{
			DeferredDeleter def_queue2( 1 );
			for( uint16_t t = 0; t < MaxCount; ++t )
			{
				def_queue2.Delete( new LockFree::DebugStruct( t ) );
			}
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

		DeferredDeleter def_queue3( 1, 2 );
		auto epoch3 = def_queue3.EpochAcquire();
		def_queue3.Delete( new LockFree::DebugStruct( 1 ) );
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 1 );
		epoch3.Release();
		def_queue3.ClearIfNeed();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 1 );
		epoch3 = def_queue3.EpochAcquire();
		def_queue3.Delete( new LockFree::DebugStruct( 1 ) );
		epoch3.Release();
		def_queue3.ClearIfNeed();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

		epoch3 = def_queue3.EpochAcquire();
		for( uint8_t t = 0; t < 3; ++t )
		{
			def_queue3.Delete( new LockFree::DebugStruct( 1 ) );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 3 );
		def_queue3.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 3 );
		def_queue3.UpdateEpoch( epoch3 );
		def_queue3.Clear();
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	} // if( !Checked.exchange( true ) )

	// Многопоточная проверка
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	const uint8_t threads_count = 10;
	DeferredDeleter def_queue( threads_count );
	std::atomic<int64_t> counter( 0 );
	std::atomic<bool> run( false );

	// Пробуем захватывать/отпускать эпохи из разных потоков (всего 2*threads_count потоков)
	// в любой момент количество захваченных эпох не должно превышать threads_count
	auto epoch_catcher = [ &counter, threads_count, &def_queue, &run ]()
	{
		try
		{
			while( !run.load() )
			{
				std::this_thread::yield();
			}

			auto ep_keeper = def_queue.EpochAcquire();
			MY_CHECK_ASSERT( ++counter <= threads_count );
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
			MY_CHECK_ASSERT( --counter < threads_count );
			ep_keeper.Release();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
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
	MY_CHECK_ASSERT( counter.load() == 0 );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

	run.store( false );
	std::atomic<int64_t> threads_work( 0 );
	auto handler = [ &def_queue, &run, &threads_work ]()
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
		if( --threads_work == 0 )
		{
			def_queue.Clear();
		}
	};

	th_vec.clear();
	threads_work.store( threads_count );
	for( auto t = 0; t < threads_count; ++t )
	{
		th_vec.push_back( std::thread( handler ) );
	}

	run.store( true );

	for( auto &th : th_vec )
	{
		th.join();
	}
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
} // void deferred_deleter_test()

void test_stack_empty( LockFree::Stack<LockFree::DebugStruct> &stack )
{
	try
	{
		LockFree::DebugStruct test_elem( -123 );
		auto elem = stack.Pop( &test_elem );
		MY_CHECK_ASSERT( elem.Val == test_elem.Val );
		auto elem2 = stack.Pop();
		MY_CHECK_ASSERT( false );
	}
	catch( const std::out_of_range& )
	{}
	catch( ... )
	{
		MY_CHECK_ASSERT( false );
	}
}

void stack_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		Stack<LockFree::DebugStruct> stack( 1, 0 );
		test_stack_empty( stack );

		stack.Emplace( 1 );
		stack.Push( LockFree::DebugStruct( 2 ) );
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 2 );

		try
		{
			bool is_empty = true;
			auto elem1 = stack.Pop( nullptr, &is_empty );
			MY_CHECK_ASSERT( elem1.Val == 2 );
			MY_CHECK_ASSERT( !is_empty );
			auto elem2 = stack.Pop( nullptr, &is_empty );
			MY_CHECK_ASSERT( elem2.Val == 1 );
			MY_CHECK_ASSERT( is_empty );
			test_stack_empty( stack );
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}

		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
		{
			Stack<LockFree::DebugStruct> stack2( 1, 0 );
			stack2.Emplace( 1 );
			stack2.Push( LockFree::DebugStruct( 2 ) );
			MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 2 );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	}

	// Многопоточная проверка
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	static const uint8_t ThreadsNum( 10 );
	static const uint16_t OneThreadOpsNum( 10 );
	Stack<LockFree::DebugStruct> stack( ThreadsNum, 0 );
	std::vector<uint16_t> readed_values[ ThreadsNum ];
	std::atomic<uint8_t> N( 0 );

	auto h = [ & ]()
	{
		const uint8_t n = N++;
		MY_CHECK_ASSERT( n < ThreadsNum );
		std::vector<uint16_t> &vec_ref = readed_values[ n ];
		vec_ref.reserve( OneThreadOpsNum );
		bool is_ready( false );

		try
		{
			for( uint16_t t = 0; t < 2*OneThreadOpsNum; ++t )
			{
				if( ( t % 2 ) == 0 )
				{
					stack.Emplace( t );
				}
				else
				{
					LockFree::DebugStruct Fake( -1 );
					auto val = stack.Pop( &Fake );
					if( val.Val != Fake.Val )
					{
						vec_ref.push_back( val.Val );
					}
				}
			}

			is_ready = true;

			while( 1 )
			{
				auto val = stack.Pop();
				MY_CHECK_ASSERT( val.Val >= 0 );
				MY_CHECK_ASSERT( val.Val <= 0xFFFF );
				vec_ref.push_back( val.Val );
			}
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( is_ready );
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
	
	// Некоторые элементы стека могли остаться неудалёнными
	// в очереди на отложенное удаление
	stack.CleanDeferredQueue();
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

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

	const auto end = m.end();
	for( uint16_t t = 0; t < OneThreadOpsNum; ++t )
	{
		auto iter = m.find( 2*t );
		MY_CHECK_ASSERT( iter != end );
		MY_CHECK_ASSERT( iter->second == ThreadsNum );
	}

	// Проверяем возвращаемое значение Push-а (был ли стек пуст до добавления)
	std::atomic<bool> run( false );
	std::atomic<int64_t> counter( 0 );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	auto h2 = [ & ]()
	{
		while( !run.load() )
		{}

		if( stack.Emplace( 1 ) )
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

	MY_CHECK_ASSERT( counter.load() == 1 );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == ( int64_t ) threads.size() );

	// Проверяем возвращаемое значение Pop-а (был ли удалён последний элемент)
	run.store( false );
	counter.store( 0 );
	std::atomic<int64_t> threads_work( 0 );
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
			MY_CHECK_ASSERT( false );
		}

		if( --threads_work == 0 )
		{
			try
			{
				stack.Pop();
			}
			catch( const std::out_of_range& )
			{}
			catch( ... )
			{
				MY_CHECK_ASSERT( false );
			}
		}
	};

	threads_work.store( threads.size() );
	for( auto &th : threads )
	{
		th = std::thread( h3 );
	}

	run.store( true );

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( counter.load() == 1 );
	MY_ASSERT( threads_work.load() == 0 );
	
	// Некоторые элементы стека могли остаться неудалёнными
	// в очереди на отложенное удаление
	stack.CleanDeferredQueue();
	
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

	run.store( false );
	counter.store( 0 );
	threads_work.store( 0 );
	auto h4 = [ & ]()
	{
		while( !run.load() )
		{}

		for( uint8_t t = 0; t < 10; ++t )
		{
			if( stack.Emplace( t ) )
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
			MY_CHECK_ASSERT( false );
		}

		if( --threads_work == 0 )
		{
			try
			{
				while( true )
				{
					stack.Pop();
				}
			}
			catch( const std::out_of_range& )
			{}
			catch( ... )
			{
				MY_CHECK_ASSERT( false );
			}
		}
	};

	threads_work.store( threads.size() );
	for( auto &th : threads )
	{
		th = std::thread( h4 );
	}

	run.store( true );

	for( auto &th : threads )
	{
		th.join();
	}
	
	MY_ASSERT( threads_work.load() == 0 );
	MY_CHECK_ASSERT( counter.load() == 0 );
	
	// Некоторые элементы стека могли остаться неудалёнными
	// в очереди на отложенное удаление
	stack.CleanDeferredQueue();
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	
	LockFree::DebugStruct s_0( -999 );
	LockFree::DebugStruct s_1( 123 );
	LockFree::DebugStruct s_2( 456 );
	LockFree::DebugStruct s_3( 789 );
	MY_CHECK_ASSERT( stack.Pop( &s_0 ).Val == s_0.Val );
	MY_CHECK_ASSERT( stack.TryPush( s_1 ) );
	MY_CHECK_ASSERT( !stack.TryPush( s_2 ) );
	MY_CHECK_ASSERT( !stack.TryEmplace( s_3.Val ) );
	MY_CHECK_ASSERT( stack.Pop( &s_0 ).Val == s_1.Val );
	
	MY_CHECK_ASSERT( stack.Pop( &s_0 ).Val == s_0.Val );
	MY_CHECK_ASSERT( stack.TryEmplace( s_2.Val ) );
	MY_CHECK_ASSERT( !stack.TryPush( s_1 ) );
	MY_CHECK_ASSERT( !stack.TryEmplace( s_3.Val ) );
	MY_CHECK_ASSERT( stack.Pop( &s_0 ).Val == s_2.Val );
	MY_CHECK_ASSERT( stack.Pop( &s_0 ).Val == s_0.Val );
	
	std::atomic<uint64_t> add_count( 0 );
	std::atomic<uint8_t> n( 0 );
	
	const auto task = [ & ]()
	{
		bool f = ( n++ % 2 ) == 0;
		for( uint8_t t = f ? 0 : 1; t < 10; ++t )
		{
			bool chk = ( t % 2 ) == 0 ? stack.TryPush( s_1 ) : stack.TryEmplace( s_2.Val );
			if( chk )
			{
				++add_count;
			}
		}
	};
	
	std::thread threads2[ 4 ];
	for( auto &th : threads2 )
	{
		th = std::thread( task );
	}
	
	for( auto &th : threads2 )
	{
		th.join();
	}
	MY_CHECK_ASSERT( add_count.load() == 1 );
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 5 );
} // void stack_test()

void queue_test()
{
	using namespace LockFree;
	static std::atomic<bool> Checked( false );
	if( !Checked.exchange( true ) )
	{
		// Однопоточная проверка
		Queue<LockFree::DebugStruct> queue( 1, 0 );
		auto elem_ptr = queue.Pop();
		MY_CHECK_ASSERT( !elem_ptr );

		queue.Emplace( 1 );
		queue.Push( LockFree::DebugStruct( 2 ) );
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 2 );

		{
			auto elem_ptr1 = queue.Pop();
			MY_CHECK_ASSERT( elem_ptr1 );
			MY_CHECK_ASSERT( elem_ptr1.get()->Val == 1 );
			auto elem_ptr2 = queue.Pop();
			MY_CHECK_ASSERT( elem_ptr2 );
			MY_CHECK_ASSERT( elem_ptr2.get()->Val == 2 );
			auto elem_ptr3 = queue.Pop();
			MY_CHECK_ASSERT( !elem_ptr3 );
		}

		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
		{
			Queue<LockFree::DebugStruct> queue2( 1, 0 );
			queue2.Emplace( 1 );
			queue2.Push( LockFree::DebugStruct( 2 ) );
			MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 2 );
		}
		MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	}

	// Многопоточная проверка
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );
	static const uint8_t ThreadsNum( 10 );
	static const uint16_t OneThreadOpsNum( 10 );
	Queue<LockFree::DebugStruct> queue( ThreadsNum, 0 );
	std::vector<uint16_t> readed_values[ ThreadsNum ];
	std::atomic<uint8_t> N( 0 );

	auto h = [ & ]()
	{
		uint8_t n = N++;
		MY_CHECK_ASSERT( n < ThreadsNum );
		std::vector<uint16_t> &vec_ref = readed_values[ n ];
		vec_ref.reserve( OneThreadOpsNum );

		for( uint16_t t = 0; t < 2*OneThreadOpsNum; ++t )
		{
			if( ( t % 2 ) == 0 )
			{
				queue.Emplace( t );
			}
			else
			{
				auto val = queue.Pop();
				if( val )
				{
					MY_CHECK_ASSERT( val.get()->Val >= 0 );
					MY_CHECK_ASSERT( val.get()->Val <= 0xFFFF );
					vec_ref.push_back( val.get()->Val );
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

			MY_CHECK_ASSERT( val.get()->Val >= 0 );
			MY_CHECK_ASSERT( val.get()->Val <= 0xFFFF );
			vec_ref.push_back( val.get()->Val );
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
	MY_CHECK_ASSERT( LockFree::DebugStruct::GetCounter() == 0 );

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

	const auto end = m.end();
	for( uint16_t t = 0; t < OneThreadOpsNum; ++t )
	{
		auto iter = m.find( 2*t );
		MY_CHECK_ASSERT( iter != end );
		MY_CHECK_ASSERT( iter->second == ThreadsNum );
	}
} // void queue_test()

void lockfree_test()
{
	try
	{
		using namespace LockFree::internal;
		forward_list_test();
		MY_CHECK_ASSERT( StructElementType<LockFree::DebugStruct>::GetCounter() == 0 );
		
		deferred_deleter_test();
		MY_CHECK_ASSERT( StructElementType<LockFree::DebugStruct>::GetCounter() == 0 );
		
		stack_test();
		MY_CHECK_ASSERT( StructElementType<LockFree::DebugStruct>::GetCounter() == 0 );
		
		queue_test();
		MY_CHECK_ASSERT( StructElementType<LockFree::DebugStruct>::GetCounter() == 0 );
	}
	catch( const std::exception &exc )
	{
		std::cout << exc.what() << std::endl;
	}
}

void lock_free_tests()
{
	// Проверка контейнеров
#ifdef _DEBUG
#if defined( _WIN32) || defined(_WIN64)
	printf( "Lockfree containers testing\n" );
#else
	printf( "Проверка неблокирующих контейнеров\n" );
#endif
	fflush( stdout );
#endif

	//for( uint16_t t = 0; t < 1000; ++t )
#if defined( _WIN32) || defined(_WIN64)
	for( uint16_t t = 0; t < 0x10; ++t )
#else
	for( uint16_t t = 0; t < 0x100; ++t )
#endif
	{
#ifdef _DEBUG
		if( ( t % 10 ) == 0 )
#else
		if( false )
#endif
		{
#if defined( _WIN32) || defined(_WIN64)
			printf( "Multithread debug, step %i\n", t + 1 );
#else
			printf( "Многопоточная отладка, шаг %i\n", t + 1 );
#endif
			fflush( stdout );
		}
		lockfree_test();
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}

#ifdef _DEBUG
#if defined( _WIN32) || defined(_WIN64)
	printf( "Lockfree test finished\n" );
//	printf( "Lockfree test finished.Press enter to finish\n" );
#else
	printf( "Готово\n" );
//	printf( "Готово.Нажмите enter, чтобы закончить\n" );
#endif
	fflush( stdout );
//	getchar();
#endif
}
