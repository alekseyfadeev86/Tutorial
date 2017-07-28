#include "CoroSrv/Sync.hpp"

namespace Bicycle
{
	namespace CoroService
	{
		Mutex::Mutex(): QueueLength( 0 ), LockWaiters( 0xFF ){}

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
					auto ptr = LockWaiters.Pop();
					MY_ASSERT( ptr );

					Coroutine *coro_ptr = *ptr;
					ptr.reset();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( &task );
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
			auto ptr = LockWaiters.Pop();
			MY_ASSERT( ptr );

			Coroutine *coro_ptr = *ptr;
			ptr.reset();
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
		inline bool CheckStateFlag( uint64_t state_flag )
		{
			bool unique_lock_captured = UniqueLockCaptured( state_flag );
			bool unique_lock_waiters  = UniqueLockWaitersNum( state_flag ) > 0;
			bool shared_lock_captured = SharedLockUsersNum( state_flag ) > 0;
			bool shared_lock_waiters  = SharedLockWaitersNum( state_flag ) > 0;

			if( unique_lock_captured )
			{
				return !shared_lock_captured;
			}
			else if( shared_lock_captured )
			{
				MY_ASSERT( !unique_lock_captured );
				return !( !unique_lock_waiters && shared_lock_waiters );
			}
			else
			{
				MY_ASSERT( !unique_lock_captured );
				MY_ASSERT( !shared_lock_captured );
				return !( unique_lock_waiters || shared_lock_waiters );
			}

			return true;
		}
#define VALIDATE_STATE( s ) MY_ASSERT( CheckStateFlag( s ) )
#else
#define VALIDATE_STATE( s )
#endif

		inline uint64_t MakeStateMask( bool unique_lock_captured,
		                               uint32_t unique_lock_waiters,
		                               uint32_t shared_lock_users,
		                               uint32_t shared_lock_waiters )
		{
			MY_ASSERT( unique_lock_waiters <= 0x1FFFFF );
			MY_ASSERT( shared_lock_users <= 0x1FFFFF );
			MY_ASSERT( shared_lock_waiters <= 0x1FFFFF );

			uint64_t res = unique_lock_captured ? UniqueLockFlag : 0;
			res |= ( ( uint64_t ) unique_lock_waiters ) << 42;
			res |= ( ( uint64_t ) shared_lock_users ) << 21;
			res |= ( uint64_t ) shared_lock_waiters;
			return res;
		}

		SharedMutex::SharedMutex(): StateFlag( 0 ), LockWaiters( 0xFF ), SharedLockWaiters( 0xFF ) {}

		void SharedMutex::AwakeCoro( bool for_unique_lock, bool by_push )
		{
			LockFree::Queue<Coroutine*> &waiters = for_unique_lock ? LockWaiters : SharedLockWaiters;
			auto ptr = waiters.Pop();
			MY_ASSERT( ptr );

			Coroutine *coro_ptr = *ptr;
			ptr.reset();
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

		void SharedMutex::Lock()
		{
			// Предполагаем, что блокировка свободна и никто на неё не претендует
			VALIDATE_STATE( StateFlag.load() );
			if( TryLock() )
			{
				// Угадали
				MY_ASSERT( UniqueLockCaptured( StateFlag.load() ) );
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
				LockWaiters.Push( cur_coro_ptr );

				// Увеличиваем счётчик сопрограмм, ожидающих монопольную блокировку
				uint64_t cur_state = StateFlag.load();
				uint64_t new_state = 0;

				bool captured = false;

				do
				{
					VALIDATE_STATE( cur_state );
					if( cur_state == 0 )
					{
						// Блокировка свободна
						captured = true;
						new_state = UniqueLockFlag;
					}
					else
					{
						// Блокировка занята - увеличиваем счётчик сопрограмм,
						// ожидающих получения монопольной блокировки
						MY_ASSERT( UniqueLockCaptured( cur_state ) == ( SharedLockUsersNum( cur_state ) == 0 ) );
						captured = false;
						new_state = MakeStateMask( UniqueLockCaptured( cur_state ),
						                           UniqueLockWaitersNum( cur_state ) + 1,
						                           SharedLockUsersNum( cur_state ),
						                           SharedLockWaitersNum( cur_state ) );
					}
					MY_ASSERT( UniqueLockCaptured( new_state ) || ( UniqueLockWaitersNum( new_state ) > 0 ) );
					VALIDATE_STATE( new_state );
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
				VALIDATE_STATE( StateFlag.load() );
				MY_ASSERT( UniqueLockCaptured( new_state ) == ( SharedLockUsersNum( new_state ) == 0 ) );

				if( captured )
				{
					// Текущий поток захватил монопольную блокировку: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					// Счётчик будет уменьшен при освобождении блокировки
					AwakeCoro( true, false );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( &task );
			VALIDATE_STATE( StateFlag.load() );
			MY_ASSERT( UniqueLockCaptured( StateFlag.load() ) );
			MY_ASSERT( SharedLockUsersNum( StateFlag.load() ) == 0 );
		} // void SharedMutex::Lock()

		bool SharedMutex::TryLock()
		{
			uint64_t expected = 0;
			return StateFlag.compare_exchange_strong( expected, UniqueLockFlag );
		}

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
						MY_ASSERT( UniqueLockCaptured( cur_state ) || ( UniqueLockWaitersNum( cur_state ) > 0 ) );
						MY_ASSERT( UniqueLockCaptured( cur_state ) == ( SharedLockUsersNum( cur_state ) == 0 ) );
						new_state = MakeStateMask( UniqueLockCaptured( cur_state ),
						                           UniqueLockWaitersNum( cur_state ),
						                           SharedLockUsersNum( cur_state ),
						                           SharedLockWaitersNum( cur_state ) + 1 );
					}
					VALIDATE_STATE( new_state );
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
				MY_ASSERT( UniqueLockCaptured( new_state ) == ( SharedLockUsersNum( new_state ) == 0 ) );

				if( captured )
				{
					// Текущий поток захватил разделяемую блокировку: извлекаем первый указатель из очереди
					// (это не обязательно будет cur_coro_ptr) и переключаемся на эту сопрограмму
					// Счётчик будет уменьшен при освобождении блокировки
					AwakeCoro( false, false );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// дойдёт очередь владения блокировкой)
			SetPostTaskAndSwitchToMainCoro( &task );
		} // void SharedMutex::SharedLock()

		bool SharedMutex::TrySharedLock()
		{
			// Если старшие 4 байта флага состояния нулевые, то никто
			// не владеет монопольной блокировкой и не претендует на неё
			uint64_t cur_state = StateFlag.load();
			uint64_t new_state;
			VALIDATE_STATE( cur_state );

			do
			{
				if( !UniqueLockFree( cur_state ) )
				{
					// Кто-то владеет монопольной блокировкой, либо претендует на неё
					return false;
				}

				MY_ASSERT( !UniqueLockCaptured( cur_state ) );
				MY_ASSERT( UniqueLockWaitersNum( cur_state ) == 0 );
				MY_ASSERT( SharedLockWaitersNum( cur_state ) == 0 );
				new_state = MakeStateMask( false, 0, SharedLockUsersNum( cur_state ) + 1, 0 );
			}
			while( StateFlag.compare_exchange_weak( cur_state, new_state ) );
			VALIDATE_STATE( cur_state );
			VALIDATE_STATE( new_state );

			return true;
		}

		void SharedMutex::Unlock()
		{
			uint64_t cur_state = StateFlag.load();
			uint64_t new_state;
			VALIDATE_STATE( cur_state );

			int64_t get_coros = 0;

			if( UniqueLockCaptured( cur_state ) )
			{
				// Была захвачена монопольная блокировка
				do
				{
					VALIDATE_STATE( cur_state );
					MY_ASSERT( UniqueLockCaptured( cur_state ) );
					MY_ASSERT( SharedLockUsersNum( cur_state ) == 0 );

					if( UniqueLockWaitersNum( cur_state ) == 0 )
					{
						// Никто не претендует на монопольную блокировку,
						// а на разделяемую желающие, возможно, есть
						get_coros = SharedLockWaitersNum( cur_state );
						new_state = MakeStateMask( false, 0, get_coros, 0 );
					}
					else
					{
						// Есть желающие на монопольную блокировку
						get_coros = -1;
						MY_ASSERT( UniqueLockCaptured( cur_state ) );
						MY_ASSERT( UniqueLockWaitersNum( cur_state ) > 0 );
						new_state = MakeStateMask( true, UniqueLockWaitersNum( cur_state ) - 1,
						                           0, SharedLockWaitersNum( cur_state ) );
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
					MY_ASSERT( !UniqueLockCaptured( cur_state ) );
					MY_ASSERT( SharedLockUsersNum( cur_state ) > 0 );
					MY_ASSERT( !( ( SharedLockWaitersNum( cur_state ) != 0 ) && ( UniqueLockWaitersNum( cur_state ) == 0 ) ) );

					// Уменьшаем счётчик пользователей разделяемой блокировки
					const uint32_t cur_users_num = SharedLockUsersNum( cur_state );
					const uint32_t unique_waiters_num = UniqueLockWaitersNum( cur_state );
					const uint32_t shared_lock_waiters_num = SharedLockWaitersNum( cur_state );

					if( ( cur_users_num == 1 ) && ( unique_waiters_num > 0 ) )
					{
						// Текущая сопрограмма - последняя, кто
						// владеет разделяемой блокировкой, и есть
						// очередь на монопольную блокировку
						get_coros = -1;
						new_state = MakeStateMask( true, unique_waiters_num - 1, 0, shared_lock_waiters_num );
					}
					else
					{
						get_coros = 0;
						new_state = MakeStateMask( false, unique_waiters_num, cur_users_num - 1, shared_lock_waiters_num );
					}
				}
				while( !StateFlag.compare_exchange_weak( cur_state, new_state ) );
			}

			if( get_coros > 0 )
			{
				// Пробуждаем get_coros сопрограмм, ждущих разделяемую блокировку
				for( int64_t t = 0; t < get_coros; ++t )
				{
					AwakeCoro( false, true );
				}
			}
			else if( get_coros < 0 )
			{
				// Пробуждаем одну сопрограмму, ждущую монопольную блокировку
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

		void Semaphore::AwakeCoro( std::unique_ptr<Coroutine*> &&ptr )
		{
			Coroutine *coro_ptr = *ptr;
			ptr.reset();
			MY_ASSERT( coro_ptr != nullptr );
			PostToSrv( *coro_ptr );
		}

		Semaphore::Semaphore(): Counter( 0 ), Waiters( 0xFF ) {}

		void Semaphore::Push()
		{
			// Смотрим, есть ли в очереди ожидающие сопрограммы
			auto ptr = Waiters.Pop();
			if( ptr )
			{
				// Есть: говорим основной сопрограмме переключиться
				// на первую из них и на этом всё
				AwakeCoro( std::move( ptr ) );
				return;
			}

			// Очередь пуста (была, по крайней мере): увеличиваем счётчик
			// и смотрим, не появились ли сопрограммы в очереди
			++Counter;
			ptr = Waiters.Pop();
			if( !ptr )
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
				AwakeCoro( std::move( ptr ) );
				return;
			} // if( TryDecrement() )

			// Не удалось уменьшить счётчик: кто-то другой его уже обнулил.
			// Возвращаем "ждуна" обратно в очередь (в конец)
			Coroutine *coro_ptr = *ptr;
			ptr.reset();
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
					auto ptr = Waiters.Pop();
					MY_ASSERT( ptr );

					Coroutine *coro_ptr = *ptr;
					ptr.reset();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// увеличится счётчик)
			SetPostTaskAndSwitchToMainCoro( &task );
		} // void Semaphore::Pop()

		//-----------------------------------------------------------------------------------------

		Event::Event(): StateFlag( 0 ), Waiters( 0xFF ) {}

		void Event::Set()
		{
			// Выставляем значение "событие активно"
			// и получаем предыдущее состояние.
			int64_t val = StateFlag.exchange( -1 );

			// Если есть сопрограммы, ожидающие активности, пробуждаем их
			for( ; val > 0; --val )
			{
				auto ptr = Waiters.Pop();
				MY_ASSERT( ptr );

				Coroutine *coro_ptr = *ptr;
				ptr.reset();
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
					auto ptr = Waiters.Pop();
					MY_ASSERT( ptr );

					Coroutine *coro_ptr = *ptr;
					ptr.reset();
					MY_ASSERT( coro_ptr != nullptr );

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
				}
			};

			// Переходим в основную сопрограмму и выполняем task
			// (обратно вернёмся либо из task-а, либо позже, когда
			// увеличится счётчик)
			SetPostTaskAndSwitchToMainCoro( &task );
		} // void Event::Wait()
	} // namespace CoroService
} // namespace Bicycle
