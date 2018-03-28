#pragma once

#include <stdint.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <condition_variable>
#include <thread>
#include "LockFree.hpp"
#include "Errors.hpp"
#if !(defined(_WIN32) || defined(_WIN64))
#include <semaphore.h>
#endif

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
			typedef std::chrono::system_clock clock_type;
			typedef std::chrono::time_point<clock_type> time_point_type;
			
			/// Флаг, показывающий, нужно ли продолжать работу
			std::atomic<bool> RunFlag;

			/// Рабочий поток
			std::thread WorkThread;

			std::condition_variable Cv;
			std::mutex Mut;

			typedef std::pair<time_point_type, task_type> elem_type;

			/// Задачи для таймера
			LockFree::ForwardList<elem_type> Tasks;

			/// Задача рабочего потока
			void ThreadFunc();

			TimeTasksQueue();
			
			/**
			 * @brief PostAt добавление задачи в очередь таймера
			 * @param task задача
			 * @param tp момент времени, в который должна сработать задача
			 */
			void PostAt( const task_type &task,
			             const time_point_type &tp );
			
		public:
			static std::shared_ptr<TimeTasksQueue> GetQueue();
			
			~TimeTasksQueue();
			
			/**
			 * @brief ExecuteAfter добавление задачи в очередь таймера,
			 * чтобы была выполнена с нужной задержкой
			 * @param task задача
			 * @param tp момент времени, в который должна сработать задача
			 */
			template<typename _Rep, typename _Period>
			inline void ExecuteAfter( const task_type &task,
			                          const std::chrono::duration<_Rep, _Period> &timeout )
			{
				const auto dt = std::chrono::duration_cast<time_point_type::duration>( timeout );
				PostAt( task, clock_type::now() + dt );
			}
			
			/**
			 * @brief ExecuteAt добавление задачи в очередь таймера,
			 * чтобы была выполнена в нужный момент времени
			 * @param task задача
			 * @param tp момент времени, в который должна сработать задача
			 */
			template<typename _Clock, typename _Duration>
			inline void ExecuteAt( const task_type &task,
			                       const std::chrono::time_point<_Clock, _Duration> &tp )
			{
				ExecuteAfter( task, tp - _Clock::now() );
			}
			
			/**
			 * @brief ExecuteAt добавление задачи в очередь таймера,
			 * чтобы была выполнена в нужный момент времени
			 * @param task задача
			 * @param tp момент времени, в который должна сработать задача
			 */
			inline void ExecuteAt( const task_type &task,
			                       const time_point_type &tp )
			{
				PostAt( task, tp );
			}
	};
	
	namespace ThreadSync
	{
		/// Семафор
		class Semaphore
		{
			private:
#if defined(_WIN32) || defined(_WIN64)
				HANDLE SemHandle;
#else
				sem_t SemHandle;
#endif
				/// Ждать семафора delta_time секунд
				bool Wait( double delta_time_sec );
				
			public:
				/**
				 * @brief Semaphore конструктор семафора
				 * @param init_val начальное значение
				 * @throw Exception в случае ошибки
				 */
#if defined(_WIN32) || defined(_WIN64)
				Semaphore( LONG init_val );
#else
				Semaphore( unsigned int init_val = 0 );
#endif
				~Semaphore();
				
				/**
				 * @brief Push Увеличение счётчика на 1
				 * @throw Exception в случае ошибки
				 */
				void Push();
	
				/**
				 * @brief Pop Ожидание установления счётчика > 1
				 * и его уменьшение на 1
				 * @throw Exception в случае ошибки
				 */
				void Pop();
				
				/**
				 * @brief TryPopFor попытка уменьшить значение
				 * счётчика в течение timeout-а
				 * @param timeout максимальное время ожидания
				 * @return успешность операции
				 * @throw Exception в случае ошибки
				 */
				template<typename _Rep, typename _Period>
				inline bool TryPopFor( const std::chrono::duration<_Rep, _Period> &timeout )
				{
					using namespace std::chrono;
					const auto microsec_timeout = duration_cast<microseconds>( timeout );
					return Wait( ( ( double ) microsec_timeout ) / 1000000. );
				}
				
				/**
				 * @brief TryPopUntil попытка уменьшить значение
				 * счётчика до заданного момента времени
				 * @param tp момент времени, после которого
				 * ожидание прекращается
				 * @return успешность операции
				 * @throw Exception в случае ошибки
				 */
				template<typename _Clock, typename _Duration>
				inline bool TryPopUntil( const std::chrono::time_point<_Clock, _Duration> &tp )
				{
					TryPopFor( tp - _Clock::now() );
				}
		};
	} // namespace ThreadSync
} // namespace Bicycle
