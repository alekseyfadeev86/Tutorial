#pragma once
#include "Service.hpp"

namespace Bicycle
{
	namespace CoroService
	{
		class Mutex: public ServiceWorker
		{
			private:
				/// Длина очереди сопрограмм на владение мьютексом
				std::atomic<uint64_t> QueueLength;

				/// Очередь сопрограмм на владение мьютксом
				LockFree::DigitsQueue LockWaiters;

			public:
				Mutex();
				~Mutex();

				/// Захват мьютекса
				void Lock();

				/// Попытка захвата мьютекса
				bool TryLock();

				/// Освобождение мьютекса
				void Unlock();
		};

		class SharedMutex: public ServiceWorker
		{
			private:
				/// Флаг текущего состояния
				std::atomic<uint64_t> StateFlag;

				/// Очередь сопрограмм на монопольное владение мьютксом
				LockFree::DigitsQueue LockWaiters;

				/// Очередь сопрограмм на разделяемое владение мьютксом
				LockFree::DigitsQueue SharedLockWaiters;

				/// "Пробуждение" первой сопрограммы из очереди
				void AwakeCoro( bool for_unique_lock, bool by_push );

			public:
				SharedMutex();
				~SharedMutex();
				
				/// Попытка захвата монопольной блокировки
				bool TryLock();

				/// Захват монопольной блокировки
				void Lock();
				
				/// Попытка захвата разделяемой блокировки
				bool TrySharedLock();

				/// Захват разделяемой блокировки
				void SharedLock();

				/// Освобождение блокировки
				void Unlock();
		};

		class Semaphore: public ServiceWorker
		{
			private:
				/// Счётчик
				std::atomic<uint64_t> Counter;

				/// Очередь ожидающих сопрограмм
				LockFree::DigitsQueue Waiters;

				/// Попытка уменьшения счётчика на 1 (его значение не может быть < 0)
				bool TryDecrement();

				/**
				 * @brief AwakeCoro "пробудить" сопрограмму: добавить в
				 * Service задачу перехода к сопрограмме
				 * @param ptr элемент очереди Waiters
				 */
				void AwakeCoro( Coroutine *coro_ptr );

			public:
				/**
				 * @brief Semaphore конструктор семафора
				 * @param init_val начальное значение
				 */
				Semaphore( uint64_t init_val = 0 );
				~Semaphore();

#ifndef _DEBUG
#error "переписать проверки"
#endif
				
				/// Увеличение счётчика на 1
				void Push();

				/// Ожидание установления счётчика > 1 и его уменьшение на 1
				void Pop();
				
#error "TODO: добавить Pop с ожиданием"
		};

		class Event: public ServiceWorker
		{
			private:
				/// Флаг состояния (-1 - событие активно, иначе - показывает длину очереди ожидающих)
				std::atomic<int64_t> StateFlag;

				/// Очередь ожидающих сопрограмм
				LockFree::DigitsQueue Waiters;

			public:
				Event();
				~Event();

				/// Установка события в активное состояние
				void Set();

				/// Сброс события (будет неактивен)
				void Reset();

				/// Ожидание активности события
				void Wait();
		};
	} // namespace CoroService
} // namespace Bicycle
