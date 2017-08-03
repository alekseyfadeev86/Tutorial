#pragma once

#include <stdint.h>
#include <atomic>
#include <functional>

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

	/// "Оставленная" блокировка
	class LockWithForsake
	{
		private:
			/// Битовая маска
			std::atomic<uint64_t> Mask;

		public:
			LockWithForsake();

			/**
			 * @brief TryLock попытка захвата блокировки и
			 * редактирование битовой маски (через битовое OR,
			 * будет выполнено в любом случае)
			 * @param mask накладываемое значение маски
			 * @return true, если Mask имел нулевое значение
			 * @throw std::invalid_argument, если mask == 0
			 */
			bool TryLock( uint32_t mask );

			/**
			 * @brief TryUnlock попытка снятия блокировки.
			 * Если текущее значение маски равно значению при захвате блокировки,
			 * её значение обнуляется (блокировка снимается), иначе маске
			 * присваивается значение маски при блокировке,
			 * а прежнее значение записывается в mask
			 * (успех, если текущее значение маски равно начальному)
			 * @param mask ссылка на буфер для текущего значения маски
			 * @return успешность
			 */
			bool TryUnlock( uint32_t &mask );

			/**
			 * @brief ForcedUnlock снятие блокировки (обнуление маски)
			 * @return прежнее значение маски
			 */
			uint32_t ForcedUnlock();
	};
} // namespace Bicycle
