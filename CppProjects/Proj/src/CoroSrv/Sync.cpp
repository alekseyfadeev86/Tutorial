#include "CoroSrv/Sync.hpp"

namespace Bicycle
{
	namespace CoroService
	{
		Mutex::Mutex(): QueueLength( 0 ), LockWaiters( 0, 0xFF ){}
		
		Mutex::~Mutex()
		{
			MY_ASSERT( !LockWaiters.Pop() );
			MY_ASSERT( QueueLength.load() == 0 );
		}

		void Mutex::Lock()
		{
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Must be called from service coroutine" );
			}

			// Предполагаем, что блокировка свободна и никто не претендует
			if( TryLock() )
			{
				// Угадали
				return;
			}

			// Не угадали
			std::function<void()> task = [ this, cur_coro_ptr ]()
			{
				// Добавляем указатель на текущую сопрограмму в очередь "ждунов"
				LockWaiters.Push( cur_coro_ptr );

				// Увеличиваем счётчик ожидающих сопрограмм
				if( QueueLength++ == 0 )
				{
					// Текущий поток увеличил счётчик с нулевого значения: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					// Счётчик будет уменьшен при освобождении блокировки
					Coroutine *coro_ptr = ( Coroutine* ) LockWaiters.Pop();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );
		}

		bool Mutex::TryLock()
		{
			// Если счётчик "ждунов" был равен 0 (никто не претендует на блокировку и не владеет ей),
			// заменяем 0 на 1 и возвращаем true (захватили блокировку), иначе облом
			uint64_t expected_value = 0;
			return QueueLength.compare_exchange_strong( expected_value, 1 );
		}

		void Mutex::Unlock()
		{
			MY_ASSERT( QueueLength.load() > 0 );

			// Уменьшаем счётчик сопрограмм-претендентов
			if( --QueueLength == 0 )
			{
				// Больше никто не претендует на владение блокировкой
				return;
			}

			// Ещё остались "ждуны": извлекаем из очереди первого,
			// сообщаем основной сопрограмме о необходимости переключиться на неё
			Coroutine *coro_ptr = ( Coroutine* ) LockWaiters.Pop();
			MY_ASSERT( coro_ptr != nullptr );
			PostToSrv( *coro_ptr );
		} // void Mutex::Unlock()

		//-----------------------------------------------------------------------------------------

		const uint64_t UniqueLockFlag = 0x8000000000000000;
		inline bool UniqueLockCaptured( uint64_t state_flag )
		{
			return ( UniqueLockFlag & state_flag ) != 0;
		}

		inline uint32_t UniqueLockWaitersNum( uint64_t state_flag )
		{
			return ( uint32_t ) ( 0x1FFFFF & ( state_flag >> 42 ) );
		}

		inline uint32_t SharedLockUsersNum( uint64_t state_flag )
		{
			return ( uint32_t ) ( 0x1FFFFF & ( state_flag >> 21 ) );
		}

		inline uint32_t SharedLockWaitersNum( uint64_t state_flag )
		{
			return ( uint32_t ) ( 0x1FFFFF & state_flag );
		}

		inline bool UniqueLockFree( uint64_t state_flag )
		{
			return ( state_flag >> 42 ) == 0;
		}

#ifdef _DEBUG
		inline void CheckStateFlag( uint64_t state_flag )
		{
			bool unique_lock_captured = UniqueLockCaptured( state_flag );
			bool unique_lock_waiters  = UniqueLockWaitersNum( state_flag ) > 0;
			bool shared_lock_captured = SharedLockUsersNum( state_flag ) > 0;
			bool shared_lock_waiters  = SharedLockWaitersNum( state_flag ) > 0;

			if( unique_lock_captured )
			{
				MY_ASSERT( !shared_lock_captured );
			}
			else if( !shared_lock_captured )
			{
				MY_ASSERT( !unique_lock_captured );
				MY_ASSERT( !( unique_lock_waiters || shared_lock_waiters ) == ( state_flag == 0 ) );
				MY_ASSERT( !( unique_lock_waiters || shared_lock_waiters ) );
			}
			else if( !unique_lock_waiters )
			{
				MY_ASSERT( !unique_lock_captured );
				MY_ASSERT( shared_lock_captured );
				MY_ASSERT( !shared_lock_waiters );
			}
		}
#define VALIDATE_STATE( s ) CheckStateFlag( s )
#define VALIDATE_UNIQUE_LOCK {\
auto f = StateFlag.load();\
VALIDATE_STATE( f );\
MY_ASSERT( UniqueLockCaptured( f ) );\
VALIDATE_STATE( StateFlag.load() );\
MY_ASSERT( UniqueLockCaptured( StateFlag.load() ) );\
MY_ASSERT( SharedLockUsersNum( StateFlag.load() ) == 0 );}
		
#define VALIDATE_SHARED_LOCK {\
auto f = StateFlag.load();\
VALIDATE_STATE( f );\
MY_ASSERT( SharedLockUsersNum( f ) > 0 );\
VALIDATE_STATE( StateFlag.load() );\
MY_ASSERT( !UniqueLockCaptured( StateFlag.load() ) );\
MY_ASSERT( SharedLockUsersNum( StateFlag.load() ) > 0 );}
#else
#define VALIDATE_STATE( s )
#define VALIDATE_UNIQUE_LOCK
#define VALIDATE_SHARED_LOCK
#endif

		inline uint64_t MakeStateMask( bool unique_lock_captured,
		                               uint32_t unique_lock_waiters,
		                               uint32_t shared_lock_users,
		                               uint32_t shared_lock_waiters )
		{
			MY_ASSERT( UniqueLockFlag == ( uint64_t ) 1 << 63 );
			MY_ASSERT( unique_lock_waiters <= 0x1FFFFF );
			MY_ASSERT( shared_lock_users   <= 0x1FFFFF );
			MY_ASSERT( shared_lock_waiters <= 0x1FFFFF );

			uint64_t res = unique_lock_captured ? UniqueLockFlag : 0;
			res |= ( ( uint64_t ) unique_lock_waiters ) << 42;
			res |= ( ( uint64_t ) shared_lock_users   ) << 21;
			res |=   ( uint64_t ) shared_lock_waiters;
			return res;
		}

		SharedMutex::SharedMutex(): StateFlag( 0 ), LockWaiters( 0, 0xFF ), SharedLockWaiters( 0, 0xFF ) {}

		SharedMutex::~SharedMutex()
		{
			MY_ASSERT( !LockWaiters.Pop() );
			MY_ASSERT( !SharedLockWaiters.Pop() );
			MY_ASSERT( StateFlag.load() == 0 );
		}
		
		void SharedMutex::AwakeCoro( bool for_unique_lock, bool by_push )
		{
			LockFree::DigitsQueue &waiters = for_unique_lock ? LockWaiters : SharedLockWaiters;
			Coroutine *coro_ptr = ( Coroutine* ) waiters.Pop();
			MY_ASSERT( coro_ptr != nullptr );

			if( by_push )
			{
				PostToSrv( *coro_ptr );
			}
			else
			{
				bool res = coro_ptr->SwitchTo();
				MY_ASSERT( res );
			}
		}
		
		bool SharedMutex::TryLock()
		{
			uint64_t expected = 0;
			bool res = StateFlag.compare_exchange_strong( expected, UniqueLockFlag );
			VALIDATE_STATE( StateFlag.load() );
			MY_ASSERT( UniqueLockCaptured( StateFlag.load() ) || !res );
			return res;
		}

		void SharedMutex::Lock()
		{
			// Предполагаем, что блокировка свободна и никто на неё не претендует
			VALIDATE_STATE( StateFlag.load() );
			if( TryLock() )
			{
				// Угадали
				return;
			}

			// Не угадали
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Must be called from service coroutine" );
			}

			std::function<void()> task = [ this, cur_coro_ptr ]()
			{
				// Добавляем указатель на текущую сопрограмму в очередь "ждунов"
				LockWaiters.Push( cur_coro_ptr );

				// Увеличиваем счётчик сопрограмм, ожидающих монопольную блокировку
				bool captured = false;
				uint64_t new_state = 0;
				uint64_t cur_state = StateFlag.load();

				do
				{
					VALIDATE_STATE( cur_state );
					
					if( cur_state == 0 )
					{
						// Блокировка свободна и никто на неё не претендует
						captured = true;
						new_state = UniqueLockFlag;
					}
					else
					{
						// Блокировка занята (монопольная или разделяемая) - увеличиваем счётчик сопрограмм,
						// ожидающих получения монопольной блокировки
						MY_ASSERT( UniqueLockCaptured( cur_state ) == ( SharedLockUsersNum( cur_state ) == 0 ) );
						captured = false;
						new_state = MakeStateMask( UniqueLockCaptured( cur_state ),
						                           UniqueLockWaitersNum( cur_state ) + 1,
						                           SharedLockUsersNum( cur_state ),
						                           SharedLockWaitersNum( cur_state ) );
					}
					MY_ASSERT( captured == ( new_state == UniqueLockFlag ) );
					MY_ASSERT( captured || ( UniqueLockWaitersNum( new_state ) > 0 ) );
					VALIDATE_STATE( new_state );
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
				MY_ASSERT( !captured || ( ( cur_state == 0 ) && ( new_state == UniqueLockFlag ) ) );
				VALIDATE_STATE( StateFlag.load() );

				if( captured )
				{
					VALIDATE_UNIQUE_LOCK;
					
					// Текущий поток захватил монопольную блокировку: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					// Счётчик будет уменьшен при освобождении блокировки
					AwakeCoro( true, false );
				}
			}; // std::function<void()> task = [ this, cur_coro_ptr ]()

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );
			
			VALIDATE_UNIQUE_LOCK;
		} // void SharedMutex::Lock()
		
		bool SharedMutex::TrySharedLock()
		{
			uint64_t cur_state = StateFlag.load();
			uint64_t new_state = 0;

			do
			{
				VALIDATE_STATE( cur_state );
				if( !UniqueLockFree( cur_state ) )
				{
					// Кто-то владеет монопольной блокировкой, либо претендует на неё
					return false;
				}

				MY_ASSERT( ( cur_state >> 42 ) == 0 );
				MY_ASSERT( !UniqueLockCaptured( cur_state ) );
				MY_ASSERT( UniqueLockWaitersNum( cur_state ) == 0 );
				MY_ASSERT( SharedLockWaitersNum( cur_state ) == 0 );
				new_state = MakeStateMask( false, 0, SharedLockUsersNum( cur_state ) + 1, 0 );
			}
			while( StateFlag.compare_exchange_weak( cur_state, new_state ) );
			
			VALIDATE_STATE( cur_state );
			VALIDATE_STATE( new_state );
			MY_ASSERT( UniqueLockFree( cur_state ) );
			MY_ASSERT( UniqueLockFree( new_state ) );
			MY_ASSERT( SharedLockUsersNum( new_state ) > 0 );
			MY_ASSERT( SharedLockWaitersNum( cur_state ) == 0 );
			MY_ASSERT( SharedLockWaitersNum( new_state ) == 0 );
			VALIDATE_SHARED_LOCK;

			return true;
		} // bool SharedMutex::TrySharedLock()

		void SharedMutex::SharedLock()
		{
			// Предполагаем, что никто не владеет монопольной блокировкой
			// и не претендует на неё
			if( TrySharedLock() )
			{
				// Угадали
				VALIDATE_STATE( StateFlag.load() );
				return;
			}

			// Не угадали
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Must be called from service coroutine" );
			}

			std::function<void()> task = [ this, cur_coro_ptr ]()
			{
				// Добавляем указатель на текущую сопрограмму в очередь "ждунов"
				SharedLockWaiters.Push( cur_coro_ptr );

				// Увеличиваем счётчик сопрограмм, ожидающих разделяемую блокировку
				uint64_t cur_state = StateFlag.load();
				uint64_t new_state = 0;

				bool captured = false;

				do
				{
					VALIDATE_STATE( cur_state );
					if( UniqueLockFree( cur_state ) )
					{
						// Никто не владеет монопольной блокировкой и не претендует на неё
						captured = true;
						
						MY_ASSERT( ( cur_state >> 42 ) == 0 );
						MY_ASSERT( !UniqueLockCaptured( cur_state ) );
						MY_ASSERT( UniqueLockWaitersNum( cur_state ) == 0 );
						MY_ASSERT( SharedLockWaitersNum( cur_state ) == 0 );
						new_state = MakeStateMask( false, 0, SharedLockUsersNum( cur_state ) + 1, 0 );
					}
					else
					{
						// Монопольная блокировка занята, либо есть претенденты на неё:
						// увеличиваем счётчик сопрограмм,
						// ожидающих получения разделяемой блокировки
						captured = false;
						new_state = MakeStateMask( UniqueLockCaptured( cur_state ),
						                           UniqueLockWaitersNum( cur_state ),
						                           SharedLockUsersNum( cur_state ),
						                           SharedLockWaitersNum( cur_state ) + 1 );
					}
					VALIDATE_STATE( new_state );
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );

				if( captured )
				{
					VALIDATE_SHARED_LOCK;
					
					// Текущий поток захватил разделяемую блокировку: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					// Счётчик будет уменьшен при освобождении блокировки
					AwakeCoro( false, false );
				}
			}; // std::function<void()> task = [ this, cur_coro_ptr ]()

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );
			
			VALIDATE_SHARED_LOCK;
		} // void SharedMutex::SharedLock()

		void SharedMutex::Unlock()
		{
			uint64_t cur_state = StateFlag.load();
			uint64_t new_state;
			VALIDATE_STATE( cur_state );
			
			if( cur_state == 0 )
			{
				// Блокировка свободна
				MY_ASSERT( false );
				return;
			}

			int64_t get_coros = 0;

			if( UniqueLockCaptured( cur_state ) )
			{
				// Была захвачена монопольная блокировка
				do
				{
					VALIDATE_STATE( cur_state );
					MY_ASSERT( UniqueLockCaptured( cur_state ) );

					const uint32_t unique_waiters_num = UniqueLockWaitersNum( cur_state );
					const uint32_t shared_waiters_num = SharedLockWaitersNum( cur_state );
					
					if( unique_waiters_num == 0 )
					{
						// Никто не претендует на монопольную блокировку,
						// а на разделяемую желающие, возможно, есть
						get_coros = shared_waiters_num;
						new_state = MakeStateMask( false, 0, shared_waiters_num, 0 );
					}
					else
					{
						// Есть желающие на монопольную блокировку
						get_coros = -1;
						MY_ASSERT( UniqueLockCaptured( cur_state ) );
						MY_ASSERT( unique_waiters_num > 0 );
						new_state = MakeStateMask( true, unique_waiters_num - 1,
						                           0, shared_waiters_num );
					}
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
				VALIDATE_STATE( cur_state );
				VALIDATE_STATE( new_state );
			}
			else
			{
				// Была захвачена разделяемая блокировка
				do
				{
					VALIDATE_STATE( cur_state );
					MY_ASSERT( SharedLockUsersNum( cur_state ) > 0 );

					// Уменьшаем счётчик пользователей разделяемой блокировки
					const uint32_t cur_users_num = SharedLockUsersNum( cur_state );
					const uint32_t unique_waiters_num = UniqueLockWaitersNum( cur_state );
					const uint32_t shared_waiters_num = SharedLockWaitersNum( cur_state );
					
					MY_ASSERT( cur_users_num > 0 );

					if( ( cur_users_num == 1 ) && ( unique_waiters_num > 0 ) )
					{
						// Текущая сопрограмма - последняя, кто
						// владеет разделяемой блокировкой, и есть
						// очередь на монопольную блокировку
						get_coros = -1;
						new_state = MakeStateMask( true, unique_waiters_num - 1,
						                           0, shared_waiters_num );
					}
					else
					{
						get_coros = 0;
						new_state = MakeStateMask( false, unique_waiters_num, cur_users_num - 1, shared_waiters_num );
					}
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
			}
			
			VALIDATE_STATE( StateFlag.load() );

			if( get_coros > 0 )
			{
				// Пробуждаем get_coros сопрограмм, ждущих разделяемую блокировку
				VALIDATE_SHARED_LOCK;
				for( int64_t t = 0; t < get_coros; ++t )
				{
					AwakeCoro( false, true );
				}
			}
			else if( get_coros < 0 )
			{
				// Пробуждаем одну сопрограмму, ждущую монопольную блокировку
				VALIDATE_UNIQUE_LOCK;
				AwakeCoro( true, true );

			}
		} // void SharedMutex::Unlock()

#undef VALIDATE_STATE

		//-----------------------------------------------------------------------------------------

		bool Semaphore::TryDecrement()
		{
			uint64_t cur_value = Counter.load();
			while( cur_value > 0 )
			{
				if( Counter.compare_exchange_weak( cur_value, cur_value - 1 ) )
				{
					// Счётчик был больше нуля - уменьшили на 1
					return true;
				}
			}

			return false;
		} // bool Semaphore::TryDecrement()

		void Semaphore::AwakeCoro( Coroutine *coro_ptr )
		{
			MY_ASSERT( coro_ptr != nullptr );
			PostToSrv( *coro_ptr );
		}

		Semaphore::Semaphore( uint64_t init_val ): Counter( init_val ), Waiters( 0, 0xFF ) {}
		
		Semaphore::~Semaphore()
		{
			MY_ASSERT( Counter.load() == 0 );
			MY_ASSERT( !Waiters.Pop() );
		}

		void Semaphore::Push()
		{
			// Смотрим, есть ли в очереди ожидающие сопрограммы
			Coroutine *coro_ptr = ( Coroutine* ) Waiters.Pop();
			if( coro_ptr != nullptr )
			{
				// Есть: говорим основной сопрограмме переключиться
				// на первую из них и на этом всё
				AwakeCoro( coro_ptr );
				return;
			}

			// Очередь пуста (была, по крайней мере): увеличиваем счётчик
			// и смотрим, не появились ли сопрограммы в очереди
			++Counter;
			coro_ptr = ( Coroutine* ) Waiters.Pop();
			if( coro_ptr == nullptr )
			{
				// Не появились
				return;
			}

			// Появились новые "ждуны": пробуем уменьшить счётчик и
			// сообщить основной сопрограмме о необходимости переключиться
			// на одного из "ждунов"

			if( TryDecrement() )
			{
				// Успешно уменьшили счётчик: передаём
				// задание основной сопрограмме
				AwakeCoro( coro_ptr );
				return;
			} // if( TryDecrement() )

			// Не удалось уменьшить счётчик: кто-то другой его уже обнулил.
			// Возвращаем "ждуна" обратно в очередь (в конец)
			MY_ASSERT( coro_ptr != nullptr );
			Waiters.Push( coro_ptr );
		} // void Semaphore::Push()

		void Semaphore::Pop()
		{
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Must be called from service coroutine" );
			}

			if( TryDecrement() )
			{
				// Успешно уменьшили счётчик на 1
				return;
			}

			// Счётчик нулевой (по крайней мере, был) - ждём его увеличения
			std::function<void()> task = [ this, cur_coro_ptr ]()
			{
				// Добавляем указатель на текущую сопрограмму в очередь "ждунов"
				Waiters.Push( cur_coro_ptr );

				// Пробуем уменьшить счётчик (вдруг уже был увеличен)
				if( TryDecrement() )
				{
					// Удалось: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					Coroutine *coro_ptr = ( Coroutine* ) Waiters.Pop();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// увеличится счётчик)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );
		} // void Semaphore::Pop()

		//-----------------------------------------------------------------------------------------

		Event::Event(): StateFlag( 0 ), Waiters( 0, 0xFF ) {}
		
		Event::~Event()
		{
			MY_ASSERT( StateFlag.load() == 0 );
			MY_ASSERT( !Waiters.Pop() );
		}

		void Event::Set()
		{
			// Выставляем значение "событие активно"
			// и получаем предыдущее состояние.
			int64_t val = StateFlag.exchange( -1 );

			// Если есть сопрограммы, ожидающие активности, пробуждаем их
			for( ; val > 0; --val )
			{
				Coroutine *coro_ptr = ( Coroutine* ) Waiters.Pop();
				MY_ASSERT( coro_ptr != nullptr );
				PostToSrv( *coro_ptr );
			} // for( int64_t val = StateFlag.exchange( -1 ); val > 0; --val )
		}

		void Event::Reset()
		{
			int64_t flag = -1;
			StateFlag.compare_exchange_strong( flag, 0 );
		}

		void Event::Wait()
		{
			if( StateFlag.load() == -1 )
			{
				// Событие активно
				return;
			}

			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Must be called from service coroutine" );
			}

			// Событие неактивно (по крайней мере, было) - ждём его активности
			std::function<void()> task = [ this, cur_coro_ptr ]()
			{
				// Добавляем указатель на текущую сопрограмму в очередь "ждунов"
				Waiters.Push( cur_coro_ptr );

				// Увеличиваем счётчик "ждунов"
				int64_t state = StateFlag.load();
				bool become_active = false;

				do
				{
					if( state == -1 )
					{
						become_active = true;
						break;
					}
				}
				while( !StateFlag.compare_exchange_weak( state, state + 1 ) );

				// Проверяем, не стало ли событие активно
				if( become_active )
				{
					// Стало: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					Coroutine *coro_ptr = ( Coroutine* ) Waiters.Pop();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// увеличится счётчик)
			SetPostTaskAndSwitchToMainCoro( std::move( task ) );
		} // void Event::Wait()
	} // namespace CoroService
} // namespace Bicycle
