#pragma once
#include "CoroSrv/Service.hpp"
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
				//typedef std::chrono::steady_clock ClockType;
				typedef std::chrono::system_clock ClockType;

				class TimerThread
				{
					private:
						/// Флаг, показывающий, нужно ли продолжать работу
						std::atomic<bool> RunFlag;

						/// Рабочий поток
						std::thread WorkThread;

						std::condition_variable Cv;
						std::mutex Mut;

						typedef std::chrono::time_point<ClockType> time_type;
						typedef std::function<void()> task_type;
						typedef std::pair<time_type, task_type> elem_type;

						/// Задачи для таймера
						LockFree::ForwardList<elem_type> Tasks;

						/// Задача рабочего потока
						void ThreadFunc();

					public:
						TimerThread();
						~TimerThread();

						/**
						 * @brief Post добавление задачи в очередь таймера
						 * @param task задача
						 * @param timeout_microsec время (в микросекундах)
						 * спустя которое задача должна быть выполнена
						 */
						void Post( const std::function<void()> &task, uint64_t timeout_microsec );
				};

				static std::shared_ptr<TimerThread> GetTimerThread();

				/// Указатель на поток таймера
				const std::shared_ptr<TimerThread> SharedTimerPtr;

				/// Структура "работника" для пробуждения сопрграмм при сработке таймера
				struct TimerWorker
				{
					/// Флаг для однократного выполнения работы
					std::atomic<bool> Flag;

					// first - указатель на сопрограмму, second - указатель на флаг причины сработки^
					// 0 - таймер сработал
					// < 0 - таймер сработал до начала ожидания
					// > 0 - ожидание было отменено
					typedef std::pair<Coroutine*, int8_t*> element_t;

					/// Список сопрограмм, ждущих сработки таймера
					LockFree::ForwardList<element_t> Waiters;
				};

				SharedSpinLock WorkerPtrLock;
				std::weak_ptr<TimerWorker> WorkerPtr;

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
