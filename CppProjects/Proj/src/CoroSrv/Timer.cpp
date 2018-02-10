#include "CoroSrv/Timer.hpp"
#include <map>

namespace Bicycle
{
	namespace CoroService
	{
		TimeTasksQueue::task_type Timer::MakeTimerTask( Error &err )
		{
			static const TimeTasksQueue::task_type FakeTask;
			
			err.Reset();
			if( IsStopped() )
			{
				// Сервис в процессе остановки
				err.Code = ErrorCodes::SrvStop;
				err.What = "Coro service is stopping";
				return FakeTask;
			}

			MY_ASSERT( WaitersPtr );
			waiters_t &waiters = *WaitersPtr;
			
			// Добавляем фиктивный элемент
			// (понадобится при отмене и обработке срабатывания таймера)
			if( !waiters.TryEmplace( nullptr, nullptr ) )
			{
				// Список ожидающих сопрограмм не пуст - таймер ещё активен (ещё не сработал)
				err.Code = ErrorCodes::TimerNotExpired;
				err.What = "Timer already active";
				return FakeTask;
			}
			
			// Добавляем в поток таймера задачу
			MY_ASSERT( SharedTimerPtr );
			
			const auto poster = GetPoster();
			MY_ASSERT( poster );
			
			const std::weak_ptr<waiters_t> waiters_weak_ptr( WaitersPtr );
			return TimeTasksQueue::task_type( new CancellableTask( [ waiters_weak_ptr, poster ]()
			{
				auto waiters_ptr = waiters_weak_ptr.lock();
				if( !waiters_ptr )
				{
					// Объект таймера был удалён
					return;
				}
				
				// Извлекаем и обрабатываем элементы сопрограмм
				auto waiters = waiters_ptr->Release();
				waiters_ptr.reset();
				
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
						// Ставим флаг "Таймер сработал"
						*( elem.second ) = 0;

						// Передаём сервису указатель на сопрограмму для выполнения
						MY_ASSERT( elem.first != nullptr );
						poster( *elem.first );
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
		} // TimeTasksQueue::task_type Timer::MakeTimerTask( Error &err )
		
		Timer::Timer(): SharedTimerPtr( TimeTasksQueue::GetQueue() ),
		                WaitersPtr( std::make_shared<Timer::waiters_t>() )
		{
			MY_ASSERT( SharedTimerPtr );
			MY_ASSERT( WaitersPtr );
		}
		
		Timer::~Timer()
		{
			Error e;
			Cancel( e );
			MY_ASSERT( !e );
		}

		void Timer::Wait( Error &err )
		{
			err.Reset();
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
			
			{
				SharedLockGuard<SharedSpinLock> lock( Locker );
				auto task_ptr = TaskWeakPtr.lock();
				if( !task_ptr || task_ptr->IsCancelled() )
				{
					// Задача таймера была уже выполнена или отменена
					err.Code = ErrorCodes::TimerExpired;
					err.What = "Timer already expired";
					return;
				}
			}

			int8_t flag = 0;
			int8_t *flag_ptr = &flag;
			
			const auto poster = GetPoster();
			const auto waiters_ptr( WaitersPtr );
			MY_ASSERT( poster );
			MY_ASSERT( waiters_ptr );

			std::function<void()> task = [ poster, waiters_ptr, cur_coro_ptr, flag_ptr ]()
			{
				MY_ASSERT( waiters_ptr );
				MY_ASSERT( poster );
				
				if( !waiters_ptr->Emplace( cur_coro_ptr, flag_ptr ) )
				{
					// В списке уже были другие элементы - выходим
					return;
				}
				
				// Элементы уже были извлечены (сейчас был добавлен первый элемент) - таймер сработал
				auto local_waiters = waiters_ptr->Release();
				if( !local_waiters )
				{
					// Такое может быть, если, например, кто-то дёрнул Cancel
					return;
				}

				// Если попали сюда - таймер уже сработал и сейчас не активен
#ifdef _DEBUG
				try
				{
#endif
				element_t elem;
				while( true )
				{
					MY_ASSERT( local_waiters );
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
			MY_ASSERT( ( bool ) err == ( flag != 0 ) );
		} // void Timer::Wait( Error &err )

		void Timer::Wait()
		{
			Error err;
			Wait( err );
			ThrowIfNeed( err );
		}

		void Timer::Cancel( Error &err )
		{
			err.Reset();
			waiters_t::Unsafe local_waiters;
			{
				SharedLockGuard<SharedSpinLock> lock( Locker );
				auto task_ptr = TaskWeakPtr.lock();
				if( task_ptr && task_ptr->Cancel() )
				{
					// Отменили задачу таймера
					MY_ASSERT( WaitersPtr );
					local_waiters = WaitersPtr->Release();
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
