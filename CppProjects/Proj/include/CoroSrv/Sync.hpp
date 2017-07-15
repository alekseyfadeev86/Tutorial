#pragma once
#include "CoroSrv/Service.hpp"

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
				LockFree::Queue<Coroutine*> LockWaiters;

			public:
				Mutex();

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
				LockFree::Queue<Coroutine*> LockWaiters;

				/// Очередь сопрограмм на разделяемое владение мьютксом
				LockFree::Queue<Coroutine*> SharedLockWaiters;

				/// "Пробуждение" первой сопрограммы из очереди
				void AwakeCoro( bool for_unique_lock, bool by_push );

			public:
				SharedMutex();

				/// Захват монопольной блокировки
				void Lock();

				/// Попытка захвата монопольной блокировки
				bool TryLock();

				/// Захват разделяемой блокировки
				void SharedLock();

				/// Попытка захвата разделяемой блокировки
				bool TrySharedLock();

				/// Освобождение блокировки
				void Unlock();
		};

		class Semaphore: public ServiceWorker
		{
			private:
				/// Счётчик
				std::atomic<uint64_t> Counter;

				/// Очередь ожидающих сопрограмм
				LockFree::Queue<Coroutine*> Waiters;

				/// Попытка уменьшения счётчика на 1 (его значение не может быть < 0)
				bool TryDecrement();

				/**
				 * @brief AwakeCoro "пробудить" сопрограмму: добавить в
				 * Service задачу перехода к сопрограмме
				 * @param ptr элемент очереди Waiters
				 */
				void AwakeCoro( std::unique_ptr<Coroutine*> &&ptr );

			public:
				Semaphore();

				/// Увеличение счётчика на 1
				void Push();

				/// Ожидание установления счётчика > 1 и его уменьшение на 1
				void Pop();
		};

		class Event: public ServiceWorker
		{
			private:
				/// Флаг состояния (-1 - событие активно, иначе - показывает длину очереди ожидающих)
				std::atomic<int64_t> StateFlag;

				/// Очередь ожидающих сопрограмм
				LockFree::Queue<Coroutine*> Waiters;

			public:
				Event();

				/// Установка события в активное состояние
				void Set();

				/// Сброс события (будет неактивен)
				void Reset();

				/// Ожидание активности события
				void Wait();
		};
	} // namespace CoroService
} // namespace Bicycle
