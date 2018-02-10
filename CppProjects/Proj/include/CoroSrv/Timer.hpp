#pragma once
#include "Service.hpp"
#include <condition_variable>

namespace Bicycle
{
	namespace ErrorCodes
	{
		/// Таймер уже сработал
		const err_code_t TimerExpired = 0xFFFFFF00;

		/// Таймер активен (ещё не сработал)
		const err_code_t TimerNotExpired = 0xFFFFFF01;
	} // namespace ErrorCodes

	namespace CoroService
	{
		class Timer: public AbstractCloser
		{
			private:
				// first - указатель на сопрограмму, second - указатель на флаг причины сработки
				// 0 - таймер сработал
				// < 0 - таймер сработал до начала ожидания
				// > 0 - ожидание было отменено
				typedef std::pair<Coroutine*, int8_t*> element_t;
				typedef LockFree::ForwardList<element_t> waiters_t;
				
				/// Указатель на поток таймера
				const std::shared_ptr<TimeTasksQueue> SharedTimerPtr;

				/// Список сопрограмм, ждущих сработки таймера
				const std::shared_ptr<waiters_t> WaitersPtr;

				SharedSpinLock Locker;

				/// "Слабый" указатель на объект задачи таймера (нужен для оптимизации отмены задачи)
				TimeTasksQueue::task_weak_type TaskWeakPtr;
				
				TimeTasksQueue::task_type MakeTimerTask( Error &err );

			public:
				Timer();
				~Timer();

				/**
				 * @brief ExpiresAfter настраивает время сработки таймера
				 * @param timeout время, спустя которое произойдёт сработка таймера
				 * @param err буфер для записи ошибки выполнения
				 */
				template<typename _Rep, typename _Period>
				inline void ExpiresAfter( const std::chrono::duration<_Rep, _Period> &timeout, Error &err )
				{
					auto task = MakeTimerTask( err );
					MY_ASSERT( ( bool ) task != ( bool ) err );
					if( !err )
					{
						LockGuard<SharedSpinLock> lock( Locker );
#ifdef _DEBUG
						{
							auto p = TaskWeakPtr.lock();
							MY_ASSERT( !p || p->IsCancelled() );
						}
#endif
						TaskWeakPtr = task;
						SharedTimerPtr->ExecuteAfter<_Rep, _Period>( task, timeout );
						MY_ASSERT( !TaskWeakPtr.expired() );
					}
				}
				
				/**
				 * @brief ExpiresAfter настраивает время сработки таймера
				 * @param timeout время, спустя которое произойдёт сработка таймера
				 * @throw Exception в случае ошибки
				 */
				template<typename _Rep, typename _Period>
				inline void ExpiresAfter( const std::chrono::duration<_Rep, _Period> &timeout )
				{
					Error err;
					ExpiresAfter<_Rep, _Period>( timeout, err );
					ThrowIfNeed( err );
				}
				
				/**
				 * @brief ExpiresAt настраивает время сработки таймера
				 * @param tp время сработки
				 * @param err буфер для записи ошибки выполнения
				 */
				template<typename _Clock, typename _Duration>
				inline void ExpiresAt( const std::chrono::time_point<_Clock, _Duration> &tp, Error &err )
				{
					auto task = MakeTimerTask( err );
					MY_ASSERT( ( bool ) task != ( bool ) err );
					if( !err )
					{
						LockGuard<SharedSpinLock> lock( Locker );
#ifdef _DEBUG
						{
							auto p = TaskWeakPtr.lock();
							MY_ASSERT( !p || p->IsCancelled() );
						}
#endif
						TaskWeakPtr = task;
						SharedTimerPtr->ExecuteAt<_Clock, _Duration>( task, tp );
						MY_ASSERT( !TaskWeakPtr.expired() );
					}
				}
				
				/**
				 * @brief ExpiresAt настраивает время сработки таймера
				 * @param tp время сработки
				 * @throw Exception в случае ошибки
				 */
				template<typename _Clock, typename _Duration>
				inline void ExpiresAt( const std::chrono::time_point<_Clock, _Duration> &tp )
				{
					Error err;
					ExpitresAt( tp, err );
					ThrowIfNeed( err );
				}

				/**
				 * @brief Wait ожидание срабатывания таймера
				 * @param err буфер для записи ошибки выполнения
				 */
				void Wait( Error &err );

				/**
				 * @brief Wait ожидание срабатывания таймера
				 * @throw Exception в случае ошибки
				 */
				void Wait();

				/**
				 * @brief Cancel отмена всех операций, ожидающих завершения
				 * (соответствующие сопрограммы получат код ошибки OperationAborted)
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				void Cancel( Error &err );

				/**
				 * @brief Cancel отмена всех операций, ожидающих завершения
				 * (соответствующие сопрограммы получат код ошибки OperationAborted)
				 * @throw Exception в случае ошибки
				 */
				void Cancel();

				/**
				 * @brief Close закрытие таймера, отмена всех операций ожидания
				 * @param err буфер для записи ошибки
				 */
				virtual void Close( Error &err ) override final;
		};

		// TODO: ??? запилить (в линуксе) с использованием timerfd_create, timerfd_settime, timerfd_gettime ???
	} // namespace CoroService
} // namespace Bicycle
