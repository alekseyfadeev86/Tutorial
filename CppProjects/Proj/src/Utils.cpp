#include "Utils.hpp"
#include <stdexcept>
#include <map>

namespace Bicycle
{
	SpinLock::SpinLock() noexcept
	{
		Flag.clear();
	}
	
	void SpinLock::Lock() noexcept
	{
		while( Flag.test_and_set() )
		{}
	}
	
	void SpinLock::Unlock() noexcept
	{
		Flag.clear();
	}

	//------------------------------------------------------------------------------
	
	SharedSpinLock::SharedSpinLock() noexcept: Flag( 0 ) {}
	
	const uint64_t LockingFlag = 0x4000000000000000;
	const uint64_t LockedFlag  = LockingFlag | 0x8000000000000000;
	
	void SharedSpinLock::Lock() noexcept
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = 0;
			// Пытаемся захватить блокировку на запись
			if( Flag.compare_exchange_weak( expected, LockedFlag ) )
			{
				// Успех
				break;
			}
			else if( expected == LockingFlag )
			{
				// Какой-то (возможно, текущий) поток уже претендует
				// на монопольную блокировку, пытаемся захватить её
				if( Flag.compare_exchange_weak( expected, LockedFlag ) )
				{
					// Успех
					break;
				}
			}
			
			// Помечаем во флаге, что "претендуем" на монопольную блокировку
			Flag.compare_exchange_weak( expected, LockingFlag | expected );
		}
	} // void SharedSpinLock::Lock()
	
	void SharedSpinLock::SharedLock() noexcept
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = Flag.load();
			MY_ASSERT( ( 1 & ( expected >> 61 ) ) == 0 );
			if( ( expected & LockedFlag ) == 0 )
			{
				// Монопольная блокировка не захвачена и никто на неё не претендует
				if( Flag.compare_exchange_weak( expected, expected + 1 ) )
				{
					// Разделяемая блокировка захвачена текущим потоком
#ifndef _DEBUG
#error "? учитывать переполнение счётчика ?"
#endif
					break;
				}
			}
		}
	} // void SharedSpinLock::SharedLock()
	
	void SharedSpinLock::Unlock() noexcept
	{
		uint64_t expected = 0;
		uint64_t new_val = 0;
		while( 1 )
		{
			expected = Flag.load();
			if( expected == 0 )
			{
				// Блокировка не была захвачена ни одним потоком
				break;
			}
			
			new_val = ( expected == LockedFlag ) ? 0 : expected - 1;
			
			if( Flag.compare_exchange_weak( expected, new_val ) )
			{
				break;
			}
		} // while( 1 )
	}
	
	//---------------------------------------------------------------------------------------------
	
	bool CancellableTask::IsCancelled() const noexcept
	{
		return WasCancelled.load();
	}
	
	void CancellableTask::operator ()()
	{
		if( !WasCancelled.exchange( true ) )
		{
			// Задача не была отменена
			MY_ASSERT( Task );
			Task();
		}
	}

	CancellableTask::operator bool() const noexcept
	{
		return !IsCancelled();
	}
	
	bool CancellableTask::Cancel() noexcept
	{
		return !WasCancelled.exchange( true );
	}
	
	void TimeTasksQueue::ThreadFunc()
	{
		// Задачи, отсортированные по времени сработки
		std::multimap<time_point_type, task_type> tasks_map;

		while( RunFlag.load() )
		{
			// Считываем новые задачи, сортируем их по времени сработки
			auto new_elems = Tasks.Release();
			while( new_elems )
			{
				tasks_map.insert( new_elems.Pop() );
			}
			MY_ASSERT( !new_elems );

			bool timeout_expired = false;
			std::unique_lock<std::mutex> lock( Mut );
			MY_ASSERT( lock );
			if( !RunFlag.load() )
			{
				// Завершаем работу
				break;
			}
			else if( Tasks )
			{
				// Появились новые задачи
				continue;
			}

			// Удаляем отменённые задачи
			if( !tasks_map.empty() )
			{
				const auto begin = tasks_map.begin();
				auto remove_iter = begin;
				for( auto end = tasks_map.end(); remove_iter != end; ++remove_iter )
				{
					if( *( remove_iter->second ) )
					{
						// Наткнулись на актуальную задачу
						break;
					}
				}
				
				tasks_map.erase( begin, remove_iter );
			}
			
			if( tasks_map.empty() )
			{		
				Cv.wait( lock );
			}
			else
			{
				const auto tp = tasks_map.begin()->first;
				timeout_expired = Cv.wait_until( lock, tp ) == std::cv_status::timeout;
			}
			lock.unlock();

			if( timeout_expired )
			{
				// Сработка таймера
				// Определяем текущее время и диапазон обрабатываемых элементов
				// (время у которых не позже текущего)
				const auto tp = clock_type::now();
				const auto begin = tasks_map.begin();
				const auto end = tasks_map.upper_bound( tp );

				for( auto iter = begin; iter != end; ++iter )
				{
					MY_ASSERT( iter->first <= tp );
					MY_ASSERT( iter->second );
					MY_ASSERT( *( iter->second ) );
					( *iter->second )();
				} // for( ; ( iter != end ) && ( iter->first <= tp ); ++iter )
			
				// Удаляем обработанные элементы
				tasks_map.erase( begin, end );
			} // if( timeout_expired )
		} // while( RunFlag.load() )
	} // void TimeTasksQueue::ThreadFunc()

	TimeTasksQueue::TimeTasksQueue(): RunFlag( true )
	{
		WorkThread = std::thread( [ this ]() { ThreadFunc(); } );
	}
	
	void TimeTasksQueue::PostAt( const task_type &task,
	                             const time_point_type &tp )
	{
		if( !task || !( *task ) )
		{
			// Задана "пустышка" вместо задачи
			return;
		}

		if( Tasks.Emplace( tp, task ) )
		{
			// Дёргаем condition_variable только, если добавили первый элемент
			// (иначе кто-то другой уже дёрнул её, но поток пока не обработал)
			std::lock_guard<std::mutex> lock( Mut );
			Cv.notify_one();
		}
	}
	
	std::shared_ptr<TimeTasksQueue> TimeTasksQueue::GetQueue()
	{
		static std::mutex Mut;
		static std::weak_ptr<TimeTasksQueue> WeakPtr;

		std::lock_guard<std::mutex> lock( Mut );
		auto res = WeakPtr.lock();
		if( !res )
		{
			res.reset( new TimeTasksQueue );
			WeakPtr = res;
		}

		MY_ASSERT( res );
		return res;
	}

	TimeTasksQueue::~TimeTasksQueue()
	{
		RunFlag.store( false );
		{
			std::lock_guard<std::mutex> lock( Mut );
			Cv.notify_all();
		}
		WorkThread.join();
	}
	
	//---------------------------------------------------------------------------------------------
	
	namespace ThreadSync
	{
#if defined(_WIN32) || defined(_WIN64)
		bool Semaphore::Wait( double delta_time_sec )
		{
			const DWORD timeout = ( delta_time_sec < 0.001 ) ? 0 : 1000*delta_time_sec;
			switch( WaitForSingleObject( SemHandle, timeout ) )
			{
				case WAIT_OBJECT_0:
					// Дождались
					return true;
					
				case WAIT_ABANDONED: //Ожидание было отменени (вообще странно...)
				case WAIT_TIMEOUT: // Не дождались (прошло слишком много времени)
					break;
					
				case WAIT_FAILED:
					// Ошибка
					ThrowIfNeed();
					MY_ASSERT( false );
					break;
					
				default:
					MY_ASSERT( false );
					break;
			}
			
			return false;
		}
		
		Semaphore::Semaphore( LONG init_val )
		{
			SemHandle = CreateSemaphore( nullptr, init_val, MAX_SEM_COUNT, nullptr );
			if( SemHandle == NULL )
			{
				// Ошибка
				ThrowIfNeed();
				MY_ASSERT( false );
			}
		}
#else
		bool Semaphore::Wait( double delta_time_sec )
		{
			Error err;
			do
			{
				int res = -1;
				if( delta_time_sec < 0.000001 )
				{
					res = sem_trywait( &SemHandle );
				}
				else
				{
					// Определяем текущее время
					const time_t tnow = time( nullptr );
					if( tnow == ( time_t ) -1 )
					{
						// Ошибка
						ThrowIfNeed();
						MY_ASSERT( false );
					}
					
					const time_t sec_count = ( time_t ) delta_time_sec;
					struct timespec tp;
					tp.tv_sec  = tnow + sec_count;
					tp.tv_nsec = ( long ) 1000000000*( delta_time_sec - sec_count );
					
					// Ждём семафора до указанного времени
					res = sem_timedwait( &SemHandle, &tp );
				}
				
				if( res == 0 )
				{
					// Успех
					return true;
				}
				
				err = GetLastSystemError();
				MY_ASSERT( err );
			}
			while( err.Code == EINTR ); // Если ожидание прервано сигналом - повторяем
			
			MY_ASSERT( ( err.Code == ETIMEDOUT ) || ( err.Code == EAGAIN ) );
			
			return false;
		}
		
		Semaphore::Semaphore( unsigned int init_val )
		{
			if( sem_init( &SemHandle, 0, init_val ) != 0 )
			{
				// Ошибка
				ThrowIfNeed();
				MY_ASSERT( false );
			}
		}
#endif
	
		Semaphore::~Semaphore()
		{
#if defined(_WIN32) || defined(_WIN64)
			CloseHandle( SemHandle );
#else
			sem_destroy( &SemHandle );
#endif
		}
		
		void Semaphore::Push()
		{
#if defined(_WIN32) || defined(_WIN64)
			if( ReleaseSemaphore( &SemHandle, 1, nullptr ) == 0 )
#else
			if( sem_post( &SemHandle ) != 0 )
#endif
			{
				// Ошибка
				ThrowIfNeed();
				MY_ASSERT( false );
			}
		}
		
		void Semaphore::Pop()
		{
#if defined(_WIN32) || defined(_WIN64)
			if( WaitForSingleObject( SemHandle, TIMEOUT_INFINITE ) != WAIT_OBJECT_0 )
#else
			int res = -1;
			do
			{
				res = sem_wait( &SemHandle );
			}
			while( ( res != 0 ) && ( errno == EINTR ) );
			if( res != 0 )
#endif
			{
				// Ошибка
				ThrowIfNeed();
				MY_ASSERT( false );
			}
		}
	} // namespace ThreadSync
} // namespace Bicycle
