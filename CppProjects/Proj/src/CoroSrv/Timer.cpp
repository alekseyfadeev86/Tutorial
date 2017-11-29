#include "CoroSrv/Timer.hpp"
#include <map>

namespace Bicycle
{
	namespace CoroService
	{
		Timer::Timer(): SharedTimerPtr( TimeTasksQueue::GetQueue() )
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

			LockGuard<SharedSpinLock> lock( TaskPtrLock );
			if( Waiters )
			{
				// Список ожидающих сопрограмм не пуст - таймер ещё активен (ещё не сработал)
				err.Code = ErrorCodes::TimerNotExpired;
				err.What = "Timer already active";
				return;
			}

			MY_ASSERT( !( TaskWeakPtr.expired() && Waiters ) );

			// Добавляем фиктивный элемент
			// (понадобится при отмене и обработке срабатывания таймера)
			Waiters.Push( nullptr, nullptr );

			// Добавляем в поток таймера задачу
			MY_ASSERT( SharedTimerPtr );
			TimeTasksQueue::task_type new_task_ptr( new CancellableTask( [ this ]()
			{
				// Извлекаем и обрабатываем элементы сопрограмм
				auto waiters = Waiters.Release();
				element_t elem;
#ifdef _DEBUG
				try
				{
#endif
				while( waiters )
				{
					elem = waiters.Pop();
					MY_ASSERT( ( elem.first == nullptr ) == ( elem.second == nullptr ) );
					MY_ASSERT( ( elem.first == nullptr ) == !waiters );

					if( elem.first != nullptr )
					{
						// Таймер сработал
						*( elem.second ) = 0;

						// Передаём сервису указатель на сопрограмму для выполнения
						MY_ASSERT( elem.first != nullptr );
						PostToSrv( *elem.first );
					}
				} // while( waiters )
				MY_ASSERT( !waiters );
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
				}
#endif
			} ) );

			TaskWeakPtr = new_task_ptr;
			SharedTimerPtr->Post( new_task_ptr, microseconds );
			new_task_ptr.reset();
			MY_ASSERT( !TaskWeakPtr.expired() );
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

			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				err.Code = ErrorCodes::NotInsideSrvCoro;
				err.What = "Must be called from service coroutine";
				return;
			}

			int8_t flag = 0;
			int8_t *flag_ptr = &flag;

			auto poster = GetPoster();
			MY_ASSERT( poster );

			std::function<void()> task = [ this, poster, cur_coro_ptr, flag_ptr ]()
			{
				waiters_t::Unsafe local_waiters;
				{
					SharedLockGuard<SharedSpinLock> lock( TaskPtrLock );
					auto task_ptr = TaskWeakPtr.lock();
					if( !task_ptr || task_ptr->IsCancelled() )
					{
						// Таймер уже сработал
						local_waiters.Push( cur_coro_ptr, flag_ptr );
					}
					else if( Waiters.Push( cur_coro_ptr, flag_ptr ) )
					{
						// Элементы уже были извлечены (был добавлен первый элемент) - таймер сработал
						MY_ASSERT( task_ptr->IsCancelled() );
						local_waiters = Waiters.Release();
					}
					else
					{
						// Всё норм
						return;
					}
				}

#ifdef _DEBUG
				try
				{
#endif
				MY_ASSERT( local_waiters );
				element_t elem;
				while( local_waiters )
				{
					elem = local_waiters.Pop();
					MY_ASSERT( ( elem.first != nullptr ) && ( elem.second != nullptr ) );

					// Помечаем флаг как "таймер сработал до начала ожидания"
					*( elem.second ) = -1;
					Coroutine &coro_ref = *elem.first;

					if( local_waiters )
					{
						// Передаём сервису указатель на сопрограмму для выполнения
						MY_ASSERT( elem.first != nullptr );
						poster( coro_ref );
					}
					else
					{
						// Переходим отсюда в сопрограмму напрямую,
						// чтобы лишний раз не нагружать сервис
						// (больше тут делать всё равно нечего)
						bool res = coro_ref.SwitchTo();
						MY_ASSERT( res );
						return;
					}
				} // while( waiters )
				MY_ASSERT( false );
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
				}
#endif
			}; // std::function<void()> task = [ poster, worker_ptr, cur_coro_ptr, flag_ptr ]()

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// сработает таймер)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );

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
			waiters_t::Unsafe local_waiters;
			{
				SharedLockGuard<SharedSpinLock> lock( TaskPtrLock );
				auto task_ptr = TaskWeakPtr.lock();
				if( task_ptr && task_ptr->Cancel() )
				{
					// Отменили задачу таймера
					local_waiters = Waiters.Release();
				}
			}

			element_t elem;
#ifdef _DEBUG
			try
			{
#endif
			while( local_waiters )
			{
				elem = local_waiters.Pop();
				MY_ASSERT( ( elem.first == nullptr ) == ( elem.second == nullptr ) );
				if( elem.first != nullptr )
				{
					// Операция ожидания отменена
					*( elem.second ) = 1;

					// Передаём сервису указатель на сопрограмму для выполнения
					MY_ASSERT( elem.first != nullptr );
					PostToSrv( *elem.first );
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
