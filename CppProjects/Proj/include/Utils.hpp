#pragma once

#include <stdint.h>
#include <atomic>
#include <functional>
#include <memory>
#include <condition_variable>
#include <thread>
#include "LockFree.hpp"

namespace Bicycle
{
	/// Класс "спин-лока"
	class SpinLock
	{
		private:
			std::atomic_flag Flag;
			
		public:
			SpinLock( const SpinLock& ) = delete;
			SpinLock& operator=( const SpinLock& ) = delete;
			
			SpinLock();
			
			/// Захват блокировки
			void Lock();
			
			/// Освобождение блокировки
			void Unlock();
	};
	
	/// Класс "спин-лока" с возможностью захвата разделяемой блокировки
	class SharedSpinLock
	{
		private:
			std::atomic<uint64_t> Flag;
			
		public:
			SharedSpinLock( const SharedSpinLock& ) = delete;
			SharedSpinLock& operator=( const SharedSpinLock& ) = delete;
			
			SharedSpinLock();
			
			/// Захват монопольной блокировки
			void Lock();
			
			/// Захват разделяемой блокировки
			void SharedLock();
			
			/// Освобождение блокировки
			void Unlock();
	};
	
	template <typename T>
	class LockGuard
	{
		private:
			T &Locker;
			
		public:
			LockGuard( const LockGuard& ) = delete;
			LockGuard& operator=( const LockGuard& ) = delete;
			
			LockGuard( T &l ): Locker( l )
			{
				Locker.Lock();
			}
			
			~LockGuard()
			{
				Locker.Unlock();
			}
	};
	
	template <typename T>
	class SharedLockGuard
	{
		private:
			T &Locker;
			
		public:
			SharedLockGuard( const SharedLockGuard& ) = delete;
			SharedLockGuard& operator=( const SharedLockGuard& ) = delete;
			
			SharedLockGuard( T &l ): Locker( l )
			{
				Locker.SharedLock();
			}
			
			~SharedLockGuard()
			{
				Locker.Unlock();
			}
	};
	
	template <typename T>
	class UniqueLocker
	{
		private:
			T *Locker;
			bool IsLocked;

		public:
			UniqueLocker( const UniqueLocker& ) = delete;
			UniqueLocker& operator=( const UniqueLocker& ) = delete;

			UniqueLocker(): Locker( nullptr ), IsLocked( false ) {}

			UniqueLocker( T &l, bool lock = true ): Locker( &l ), IsLocked( false )
			{
				if( lock )
				{
					Locker->Lock();
					IsLocked = true;
				}
			}

			UniqueLocker( UniqueLocker &&l ): Locker( l.Locker ), IsLocked( l.IsLocked )
			{
				l.Locker = nullptr;
				l.IsLocked = false;
			}

			UniqueLocker& operator=( UniqueLocker &&l )
			{
				if( &l != this )
				{
					if( IsLocked )
					{
						Locker->Unlock();
					}

					Locker = l.Locker;
					IsLocked = l.IsLocked;

					l.Locker = nullptr;
					l.IsLocked = false;
				}

				return *this;
			}
			
			~UniqueLocker()
			{
				if( IsLocked )
				{
					Locker->Unlock();
				}
			}

			operator bool() const
			{
				return Locker != nullptr;
			}

			bool Locked() const
			{
				return IsLocked;
			}

			void Lock()
			{
				if( !IsLocked )
				{
					Locker->Lock();
					IsLocked = true;
				}
			}

			void Unlock()
			{
				if( IsLocked )
				{
					Locker->Unlock();
					IsLocked = false;
				}
			}
	};
	
	template <typename T>
	class SharedLocker
	{

		private:
			T *Locker;
			bool IsLocked;
			
		public:
			SharedLocker( const SharedLocker& ) = delete;
			SharedLocker& operator=( const SharedLocker& ) = delete;

			SharedLocker(): Locker( nullptr ), IsLocked( false ) {}

			SharedLocker( T &l, bool lock ): Locker( &l ), IsLocked( false )
			{
				if( lock )
				{
					Locker->SharedLock();
					IsLocked = true;
				}
			}

			SharedLocker( SharedLocker &&l ): Locker( l.Locker ), IsLocked( l.IsLocked )
			{
				l.Locker = nullptr;
				l.IsLocked = false;
			}

			SharedLocker& operator=( SharedLocker &&l )
			{
				if( &l != this )
				{
					if( IsLocked )
					{
						Locker->Unlock();
					}

					Locker = l.Locker;
					IsLocked = l.IsLocked;

					l.Locker = nullptr;
					l.IsLocked = false;
				}

				return *this;
			}
			
			~SharedLocker()
			{
				if( IsLocked )
				{
					Locker->Unlock();
				}
			}

			operator bool() const
			{
				return Locker != nullptr;
			}

			bool Locked() const
			{
				return IsLocked;
			}

			void Lock()
			{
				if( !IsLocked )
				{
					Locker->SharedLock();
					IsLocked = true;
				}
			}

			void Unlock()
			{
				if( IsLocked )
				{
					Locker->Unlock();
					IsLocked = false;
				}
			}
	};

	/// Класс, выполняющий заданные действия при удалении
	class Defer
	{
		private:
			std::function<void()> Task;

		public:
			Defer( const Defer& ) = delete;
			Defer& operator=( const Defer& ) = delete;

			template <typename... Args>
			Defer( Args... args ): Task( args... ) {}

			~Defer()
			{
				( *this )();
			}

			void operator()()
			{
				std::function<void()> f( std::move( Task ) );
				if( f )
				{
					f();
				}
			}

			operator bool() const
			{
				return ( bool ) Task;
			}
	};
	
	/// Отменяемая задача
	class CancellableTask
	{
		private:
			/// Задача
			std::function<void()> Task;
			
			/// Показывает, была ли отменена задача
			std::atomic<bool> WasCancelled;
			
		public:
			CancellableTask( const CancellableTask& ) = delete;
			CancellableTask& operator=( const CancellableTask& ) = delete;
			
			template<typename ...Args>
			CancellableTask( Args ...args ): Task( args... ),
			                                 WasCancelled( !Task )
			{
				MY_ASSERT( ( bool ) Task != WasCancelled.load() );
			}

			/// Показывает, была ли задача отменена
			bool IsCancelled() const;
			
			/**
			 * @brief operator () потокобезопасное выполнение задачи:
			 * только 1 поток выполнит задачу, если она не была
			 * выполнена или отменена
			 */
			void operator()();
			
			/// Возвращает true, если задача не была отменена
			operator bool() const;
			
			/**
			 * @brief Cancel отмена задачи (потокобезопасная)
			 * @return false, если задача уже была выполнена
			 * или отменена
			 */
			bool Cancel();
	};
	
	/// Класс очереди задач с указанием времени их выполнения
	class TimeTasksQueue
	{
		public:
			typedef std::shared_ptr<CancellableTask> task_type;
			typedef std::weak_ptr<CancellableTask> task_weak_type;

		private:
			typedef std::chrono::system_clock ClockType;
			
			/// Флаг, показывающий, нужно ли продолжать работу
			std::atomic<bool> RunFlag;

			/// Рабочий поток
			std::thread WorkThread;

			std::condition_variable Cv;
			std::mutex Mut;

			typedef std::chrono::time_point<ClockType> time_type;
			typedef std::pair<time_type, task_type> elem_type;

			/// Задачи для таймера
			LockFree::ForwardList<elem_type> Tasks;

			/// Задача рабочего потока
			void ThreadFunc();

			TimeTasksQueue();
			
		public:
			static std::shared_ptr<TimeTasksQueue> GetQueue();
			
			~TimeTasksQueue();

			/**
			 * @brief Post добавление задачи в очередь таймера
			 * @param task задача
			 * @param timeout_microsec время (в микросекундах)
			 * спустя которое задача должна быть выполнена
			 */
			void Post( const task_type &task,
			           uint64_t timeout_microsec );
	};
} // namespace Bicycle
