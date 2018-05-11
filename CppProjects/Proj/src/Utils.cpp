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
			
			// Пытаемся пометить во флаге, что "претендуем" на монопольную блокировку
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
	
	void TimeTasksQueue::SemPush()
	{
#ifdef _DEBUG
		while( 1 )
		{
			try
			{
				Sem.Push();
				break;
			}
			catch( ... )
			{
#ifndef _DEBUG
#error "? обработать исключение ?"
#endif
			}
	}
	
	void TimeTasksQueue::ThreadFunc()
	{
		// Задачи, отсортированные по времени сработки
		std::multimap<time_point_type, task_type> tasks_map;
		
		elem_type task_element;
		
		LockFree::ForwardList<elem_type>::Unsafe new_elements;

		while( RunFlag.load() )
		{
			try
			{
				// Добавляем оставшиеся элементы, которые были извлечены из
				// Tasks, но не добавлены в tasks_map
				if( task_element.second )
				{
					tasks_map.insert( task_element );
					task_element.second.reset();
				}
				
				while( new_elements )
				{
					MY_ASSERT( !task_element.second );
					new_elements.Pop( task_element );
					
					MY_ASSERT( task_element.second );
					tasks_map.insert( task_element );
					task_element.second.reset();
				}
				MY_ASSERT( !new_elements );
				
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
				
				bool timeout_expired = false;
				if( tasks_map.empty() )
				{		
					// Текущих задач нет - ждём новых
					Sem.Pop();
				}
				else
				{
					// Есть текущие задачи - ждём времени выполнения первой из них,
					// либо поступления новых
					timeout_expired = Sem.TryPopUntil( tasks_map.begin()->first );
				}
				
				if( !RunFlag.load() )
				{
					// Завершаем работу очереди
					break;
				}
				else if( !timeout_expired )
				{
					// Поступили новые задачи
					MY_ASSERT( !new_elements );
					new_elements = Tasks.Release();
					continue;
				}
				else
				{
					// Сработка таймера
					// Определяем текущее время и диапазон обрабатываемых элементов
					// (время у которых не позже текущего)
					const auto begin = tasks_map.begin();
					const auto end = tasks_map.upper_bound( clock_type::now() );
	
					for( auto iter = begin; iter != end; ++iter )
					{
						MY_ASSERT( iter->first <= tp );
						MY_ASSERT( iter->second );
						MY_ASSERT( *( iter->second ) );
						try
						{
							( *iter->second )();
						}
						catch( ... )
						{
							MY_ASSERT( false );
						}
					} // for( ; ( iter != end ) && ( iter->first <= tp ); ++iter )
				
					// Удаляем обработанные элементы
					tasks_map.erase( begin, end );
				}
			} // try
#ifdef _DEBUG
			catch( const std::exception& )
#else
#error "? или выбрать более конкретный тип исключений ?"
#error "? ограничить макс. число перехватываемых исключений подряд (например, 2 исключения подряд перехватили, а 3е - нет) ?"
			catch( const std::bad_alloc& )
#endif
			{}
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
			// Накручиваем счётчик семафора только, если добавили первый элемент
			// (иначе кто-то другой уже дёрнул её, но поток пока не обработал)
			SemPush();
		} // if( Tasks.Emplace( tp, task ) )
	} // void TimeTasksQueue::PostAt
	
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
		SemPush();
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
