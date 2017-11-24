#include "Utils.hpp"
#include <stdexcept>
#include <map>

namespace Bicycle
{
	SpinLock::SpinLock()
	{
		Flag.clear();
	}
	
	void SpinLock::Lock()
	{
		while( Flag.test_and_set() )
		{}
	}
	
	void SpinLock::Unlock()
	{
		Flag.clear();
	}

	//------------------------------------------------------------------------------
	
	SharedSpinLock::SharedSpinLock(): Flag( 0 ) {}
	
	const uint64_t LockingFlag = 0x4000000000000000;
	const uint64_t LockedFlag  = LockingFlag | 0x8000000000000000;
	
	void SharedSpinLock::Lock()
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = 0;
			if( Flag.compare_exchange_weak( expected, LockedFlag ) )
			{
				// Блокировка на запись была захвачена текущим потоком (до этого никто не претендовал)
				break;
			}
			else if( expected == LockingFlag )
			{
				// Какой-то (возможно, текущий) поток уже претендует на монопольную блокировку
				if( Flag.compare_exchange_weak( expected, LockedFlag ) )
				{
					// Блокировка на запись была захвачена текущим потоком
					break;
				}
			}
			
			// Помечаем во флаге, что "претендуем" на монопольную блокировку
			Flag.compare_exchange_weak( expected, LockingFlag | expected );
		}
	} // void SharedSpinLock::Lock()
	
	void SharedSpinLock::SharedLock()
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = Flag.load();
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
	
	void SharedSpinLock::Unlock()
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
	
	void CancellableTask::operator ()()
	{
		if( !WasCancelled.exchange( true ) )
		{
			MY_ASSERT( Task );
			Task();
		}
	}
	
	CancellableTask::operator bool() const
	{
		return !WasCancelled.load();
	}
	
	bool CancellableTask::Cancel()
	{
		return !WasCancelled.exchange( true );
	}
	
	void TimeTasksQueue::ThreadFunc()
	{
		// Задачи, отсортированные по времени сработки
		std::multimap<time_type, task_type> tasks_map;

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
				const auto tp = ClockType::now();
				const auto begin = tasks_map.begin();
				const auto end = tasks_map.upper_bound( tp );

				for( auto iter = begin; iter != end; ++iter )
				{
					MY_ASSERT( iter->first <= tp );
					MY_ASSERT( iter->second );
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

	void TimeTasksQueue::Post( const task_type &task,
	                           uint64_t timeout_microsec )
	{
		if( !task || !( *task ) )
		{
			return;
		}

		auto time_point = ClockType::now() + std::chrono::microseconds( timeout_microsec );
		if( Tasks.Push( time_point, task ) )
		{
			// Дёргаем condition_variable только, если добавили первый элемент
			// (иначе кто-то другой уже дёрнул её, но поток пока не обработал)
			std::lock_guard<std::mutex> lock( Mut );
			Cv.notify_one();
		}
	} // void Timer::TimerThread::Post
} // namespace Bicycle
