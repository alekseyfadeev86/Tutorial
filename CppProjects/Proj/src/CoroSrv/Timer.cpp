#include "CoroSrv/Timer.hpp"
#include <map>

namespace Bicycle
{
	namespace CoroService
	{
		void Timer::TimerThread::ThreadFunc()
		{
			// Задачи, отсортированные по времени сработки
			std::map<time_type, std::vector<task_type>> tasks_map;

			std::unique_lock<std::mutex> lock( Mut, std::defer_lock_t() );

			while( RunFlag.load() )
			{
				// Считываем новые задачи, сортируем их по времени сработки
				auto new_elems = Tasks.Release();
				while( new_elems )
				{
					elem_type elem = new_elems.Pop();
					tasks_map[ elem.first ].push_back( std::move( elem.second ) );
				}

				bool timeout_expired = false;
				lock.lock();
				if( tasks_map.empty() )
				{
					Cv.wait( lock );
				}
				else
				{
					auto tp = tasks_map.begin()->first;
					timeout_expired = Cv.wait_until( lock, tp ) == std::cv_status::timeout;
				}
				lock.unlock();

				if( timeout_expired )
				{
					// Сработка таймера
					auto tp = ClockType::now();
					while( !tasks_map.empty() )
					{
						auto iter = tasks_map.begin();
						if( iter->first > tp )
						{
							break;
						}

						MY_ASSERT( iter != tasks_map.end() );
						auto cur_tasks = std::move( iter->second );
						tasks_map.erase( iter );

						for( auto &one_task : cur_tasks )
						{
							MY_ASSERT( one_task );
							one_task();
						}
					}
				}
			} // while( RunFlag.load() )
		} // void Timer::TimerThread::ThreadFunc()

		Timer::TimerThread::TimerThread(): RunFlag( true )
		{
			WorkThread = std::thread( [ this ]() { ThreadFunc(); } );
		}

		Timer::TimerThread::~TimerThread()
		{
			RunFlag.store( false );
			{
				std::lock_guard<std::mutex> lock( Mut );
				Cv.notify_all();
			}
			WorkThread.join();
		}

		void Timer::TimerThread::Post( const std::function<void ()> &task,
		                               uint64_t timeout_microsec )
		{
			if( !task )
			{
				return;
			}

			auto time_point = ClockType::now() + std::chrono::microseconds( timeout_microsec );
			Tasks.Push( time_point, task );

			std::lock_guard<std::mutex> lock( Mut );
			Cv.notify_one();
		} // void Timer::TimerThread::Post

		std::shared_ptr<Timer::TimerThread> Timer::GetTimerThread()
		{
			static std::mutex Mut;
			static std::weak_ptr<TimerThread> WeakPtr;

			std::lock_guard<std::mutex> lock( Mut );
			auto res = WeakPtr.lock();
			if( !res )
			{
				res.reset( new TimerThread );
				WeakPtr = res;
			}

			MY_ASSERT( res );
			return res;
		}

		Timer::Timer(): SharedTimerPtr( GetTimerThread() )
		{
			MY_ASSERT( SharedTimerPtr );
		}

		void Timer::ExpiresAfter( uint64_t microseconds, Error &err )
		{
			err = Error();
			if( IsStopped() )
			{
				// Сервис в процессе остановки
				err.Code = ErrorCodes::SrvStop;
				err.What = "Coro service is stopping";
				return;
			}

			LockGuard<SharedSpinLock> lock( WorkerPtrLock );
			std::shared_ptr<TimerWorker>  new_worker( WorkerPtr.lock() );
			if( new_worker && !new_worker->Flag.load() )
			{
				// Таймер ещё активен (ещё не сработал)
				err.Code = ErrorCodes::TimerNotExpired;
				err.What = "Timer already active";
				return;
			}

			new_worker.reset( new TimerWorker );
			MY_ASSERT( new_worker );

			// Явно сбрасываем флаг выполнения
			new_worker->Flag.store( false );

			// Добавляем фиктивный элемент
			// (понадобится при отмене и обработке срабатывания таймера)
			new_worker->Waiters.Push( nullptr, nullptr );
			WorkerPtr = new_worker;

			// Добавляем в поток таймера задачу
			MY_ASSERT( SharedTimerPtr );
			SharedTimerPtr->Post( [this, new_worker ]()
			{
				MY_ASSERT( new_worker );
				if( new_worker->Flag.exchange( true ) )
				{
					// Обработка уже выполнена
					return;
				}

				// Извлекаем и обрабатываем элементы сопрограмм
				auto waiters = new_worker->Waiters.Release();
				TimerWorker::element_t elem;
#ifdef _DEBUG
				try
				{
#endif
				while( waiters )
				{
					elem = waiters.Pop();
					MY_ASSERT( ( elem.first == nullptr ) == ( elem.second == nullptr ) );
					if( elem.first != nullptr )
					{
						// Таймер сработал
						*( elem.second ) = 0;

						// Добавляем задачу перехода к сопрограмме
						Coroutine *coro_ptr = elem.first;
						PostToSrv( [ coro_ptr ]()
						{
							bool res = coro_ptr->SwitchTo();
							MY_ASSERT( res );
						} );
					}
				} // while( waiters )
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
				}
#endif
			}, microseconds );
			new_worker.reset();
			MY_ASSERT( !WorkerPtr.expired() );
		} // void Timer::ExpiresAfter( uint64_t microseconds, Error &err )

		void Timer::ExpiresAfter( uint64_t microseconds )
		{
			Error err;
			ExpiresAfter( microseconds, err );
			ThrowIfNeed( err );
		}

		void Timer::Wait( Error &err )
		{
			err = Error();
			if( IsStopped() )
			{
				// Сервис в процессе остановки
				err.Code = ErrorCodes::SrvStop;
				err.What = "Coro service is stopping";
				return;
			}

			std::shared_ptr<TimerWorker> worker_ptr;
			{
				SharedLockGuard<SharedSpinLock> lock( WorkerPtrLock );
				worker_ptr = WorkerPtr.lock();
			}

			if( !worker_ptr )
			{
				// Таймер уже сработал
				err.Code = ErrorCodes::TimerExpired;
				err.What = "Timer already expired";
				return;
			}

			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				err.Code = ErrorCodes::NotInsideSrvCoro;
				err.What = "Must be called from service coroutine";
				return;
			}

			int8_t flag = 0;

			// Таймер активен (по крайней мере, был)
			std::function<void()> task = [ this, &worker_ptr, cur_coro_ptr, &flag ]()
			{
				if( worker_ptr->Waiters.Push( cur_coro_ptr, &flag ) )
				{
					// Элементы уже были извлечены - таймер сработал
					auto waiters = worker_ptr->Waiters.Release();
					MY_ASSERT( worker_ptr->Flag.load() );
					TimerWorker::element_t elem;
#ifdef _DEBUG
					try
					{
#endif
					while( waiters )
					{
						elem = waiters.Pop();
						MY_ASSERT( ( elem.first != nullptr ) && ( elem.second != nullptr ) );
						*( elem.second ) = -1;

						// Добавляем задачу перехода к сопрограмме
						Coroutine *coro_ptr = elem.first;
						PostToSrv( [ coro_ptr ]()
						{
							bool res = coro_ptr->SwitchTo();
							MY_ASSERT( res );
						} );
					} // while( waiters )
#ifdef _DEBUG
					}
					catch( ... )
					{
						MY_ASSERT( false );
					}
#endif
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// увеличится счётчик)
			SetPostTaskAndSwitchToMainCoro( &task );

			if( flag < 0 )
			{
				// Таймер уже сработал
				err.Code = ErrorCodes::TimerExpired;
				err.What = "Timer already expired";
			}
			else if( flag > 0 )
			{
				// Ожидание было отменено
				err.Code = ErrorCodes::OperationAborted;
				err.What = "Operation was aborted";
			}
		} // void Timer::Wait( Error &err )

		void Timer::Wait()
		{
			Error err;
			Wait( err );
			ThrowIfNeed( err );
		}

		void Timer::Cancel( Error &err )
		{
			err = Error();
			if( IsStopped() )
			{
				// Сервис в процессе остановки
				err.Code = ErrorCodes::SrvStop;
				err.What = "Coro service is stopping";
				return;
			}

			std::shared_ptr<TimerWorker> worker_ptr;
			{
				SharedLockGuard<SharedSpinLock> lock( WorkerPtrLock );
				worker_ptr = WorkerPtr.lock();
			}

			if( !worker_ptr || worker_ptr->Flag.exchange( true ) )
			{
				// Таймер уже сработал, либо обработка отменена
				return;
			}

			auto waiters = worker_ptr->Waiters.Release();
			TimerWorker::element_t elem;
#ifdef _DEBUG
			try
			{
#endif
			while( waiters )
			{
				elem = waiters.Pop();
				MY_ASSERT( ( elem.first == nullptr ) == ( elem.second == nullptr ) );
				if( elem.first != nullptr )
				{
					// Операция ожидания отменена
					*( elem.second ) = 1;

					// Добавляем задачу перехода к сопрограмме
					Coroutine *coro_ptr = elem.first;
					PostToSrv( [ coro_ptr ]()
					{
						bool res = coro_ptr->SwitchTo();
						MY_ASSERT( res );
					} );
				}
			} // while( waiters )
#ifdef _DEBUG
			}
			catch( ... )
			{
				MY_ASSERT( false );
			}
#endif
		} // void Timer::Cancel( Error &err )

		void Timer::Cancel()
		{
			Error err;
			Cancel( err );
			ThrowIfNeed( err );
		}

		void Timer::Close( Error &err )
		{
			Cancel( err );
		}
	} // namespace CoroService
} // namespace Bicycle
