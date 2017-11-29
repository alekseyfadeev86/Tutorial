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
				/// Указатель на поток таймера
				const std::shared_ptr<TimeTasksQueue> SharedTimerPtr;

				// first - указатель на сопрограмму, second - указатель на флаг причины сработки
				// 0 - таймер сработал
				// < 0 - таймер сработал до начала ожидания
				// > 0 - ожидание было отменено
				typedef std::pair<Coroutine*, int8_t*> element_t;
				typedef LockFree::ForwardList<element_t> waiters_t;

				/// Список сопрограмм, ждущих сработки таймера
				waiters_t Waiters;

				SharedSpinLock TaskPtrLock;

				/// "Слабый" указатель на объект задачи таймера (нужен для оптимизации отмены задачи)
				TimeTasksQueue::task_weak_type TaskWeakPtr;

			public:
				Timer();

				/**
				 * @brief ExpiresAfter настраивает время сработки таймера
				 * @param microseconds время в микросекундах, через
				 * которое таймер будет активен
				 * @param err буфер для записи ошибки выполнения
				 */
				void ExpiresAfter( uint64_t microseconds, Error &err );

				/**
				 * @brief ExpiresAfter настраивает время сработки таймера
				 * @param microseconds время в микросекундах, через
				 * которое таймер будет активен
				 * @throw Exception в случае ошибки
				 */
				void ExpiresAfter( uint64_t microseconds );

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
