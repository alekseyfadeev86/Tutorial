#include "Coro.hpp"
#include <string.h>

namespace Bicycle
{
	namespace Coro
	{
		ThreadLocal::ThreadLocal()
		{
#ifdef _WIN32
			Key = TlsAlloc();
			if( Key == TLS_OUT_OF_INDEXES )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
			}
#else
			ThrowIfNeed( GetSystemErrorByCode( pthread_key_create( &Key, nullptr ) ) );
#endif
		}

		ThreadLocal::~ThreadLocal()
		{
#ifdef _WIN32
			BOOL res = TlsFree( Key );
			MY_ASSERT( res != 0 );
#else
			int res = pthread_key_delete( Key );
			MY_ASSERT( res == 0 );
#endif
		}

		void* ThreadLocal::Get() const
		{
			void *res = nullptr;

#ifdef _WIN32
			res = TlsGetValue( Key );
			if( res == 0 )
			{
				// Возможно ошибка, а возможно, там просто хранится 0
				ThrowIfNeed();
			}
#else
			res = pthread_getspecific( Key );
#endif

			return res;
		}

		void ThreadLocal::Set( void *ptr )
		{
#ifdef _WIN32
			if( TlsSetValue( Key, ptr ) == FALSE )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
			}
#else
			ThrowIfNeed( pthread_setspecific( Key, ptr ) );
#endif
		}

		//-----------------------------------------------------------------------------------------

		/// Флаг "Сопрограмма выполняется"
		const uint8_t InProgressFlag = 1;
		
		/// Флаг "Сопрограмма завершена"
		const uint8_t FinishedFlag = 2;
		
		struct CoroInfo
		{
			/// Указатель на выполняемую в данный момент сопрограмму
			Coroutine *CurrentCoro;

			/// Указатель на сопрограмму, на которую переключаемся (используется только при переключении контекста)
			Coroutine *NextCoro;

			CoroInfo( const CoroInfo& ) = delete;
			CoroInfo& operator=( const CoroInfo& ) = delete;

			CoroInfo( Coroutine &main_coro ): CurrentCoro( &main_coro ),
			                                  NextCoro( nullptr )
			{}
		};

		ThreadLocal Coroutine::Internal;

#ifdef _WIN32
		VOID CALLBACK Coroutine::CoroutineFunc( PVOID param )
#else
		Coroutine::Array::Array( size_t sz ): Ptr( sz > 0 ? new char[ sz ] : nullptr ), Sz( sz )
		{}

		Coroutine::Array::~Array()
		{
			if( Ptr != nullptr )
			{
				delete [] Ptr;
			}
		}

		void Coroutine::CoroutineFunc( void *param )
#endif
		{
			auto task_coro_ptr = ( coro_func_params_t* ) param;

			MY_ASSERT( task_coro_ptr != nullptr );
			MY_ASSERT( task_coro_ptr->second != nullptr );
			MY_ASSERT( task_coro_ptr->first );

			CoroInfo *info = ( CoroInfo* ) Internal.Get();
			if( info == nullptr )
			{
				// Вызвали функцию не из сопрограммы
				MY_ASSERT( false );
				std::terminate();
			}
			Coroutine *cur_coro = info->CurrentCoro;

			if( cur_coro != nullptr )
			{
				// Сбрасываем флаг "занятости" предыдущей сопрограммы
				MY_ASSERT( ( cur_coro->StateFlag.load() & ~( InProgressFlag | FinishedFlag ) ) == 0 );
				cur_coro->StateFlag &= ~InProgressFlag;
			}

			info->CurrentCoro = task_coro_ptr->second;

			// Выполняем функцию сопрограммы
			Coroutine *new_coro = task_coro_ptr->first ? task_coro_ptr->first() : nullptr;

			// Помечаем сопрограмму как выполненную
			task_coro_ptr->second->StateFlag |= FinishedFlag;

			MY_ASSERT( new_coro != nullptr );
			if( new_coro != nullptr )
			{
				// Переключаемся на новую сопрограмму
				new_coro->SwitchTo();

				// Если попали сюда - перейти не удалось, либо new_coro - текущая сопрограмма
				MY_ASSERT( false );
			}

			MY_ASSERT( false );
			std::terminate();
		} //Coroutine::CoroutineFunc

		Coroutine::Coroutine(): StateFlag( 0 ), CreatedFromThread( true ), Started( true )
		{
			if( Internal.Get() != nullptr )
			{
				// Ошибка: запускаем из-под сопрограммы
				throw Exception( ErrorCodes::CoroToCoro,
				                 "Cannot convert coroutine to coroutine" );
			}

#ifdef _WIN32
			// Преобразуем текущий поток в волокно
			FiberPtr = ConvertThreadToFiber( nullptr );
			if( FiberPtr == NULL )
			{
				ThrowIfNeed();
			}
			MY_ASSERT( FiberPtr != nullptr );
#else
			if( getcontext( &Context ) != 0 )
			{
				// Ошибка получения контекста текущего потока
				ThrowIfNeed();
				MY_ASSERT( false );
			}
#endif

			// Создаём структуру CoroInfo
			CoroInfo *new_coro_info = new CoroInfo( *this );
			MY_ASSERT( new_coro_info->CurrentCoro == this );
			MY_ASSERT( new_coro_info->NextCoro == nullptr );
			Internal.Set( ( void* ) new_coro_info );
			MY_ASSERT( Internal.Get() == ( void* ) new_coro_info );

			// Помечаем текущую сопрограмму как выполняющуюся в данный момент
			StateFlag.store( InProgressFlag );
		} // Coroutine::Coroutine()

		inline size_t EditStackSize( size_t stack_sz )
		{
#ifdef _WIN32
			// Если 0 - размер стека будет выбран CreateFiber-ом автоматически
			return stack_sz > 10*1024 ? stack_sz : 0;
#else
			return stack_sz > SIGSTKSZ ? stack_sz : SIGSTKSZ;
#endif
		}

		Coroutine::Coroutine( CoroTaskType task,
							  size_t stack_sz ): StateFlag( 0 ),
		                                         CreatedFromThread( false ), Started( false )
#ifndef _WIN32
		                                         , Stack( EditStackSize( stack_sz ) )
#endif
		{
			if( !task )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "Incorrect coroutine task" );
			}

			CoroFuncParams.first = std::move( task );
			CoroFuncParams.second = this;

#ifdef _WIN32
			// Создаём новое волокно
			FiberPtr = CreateFiber( EditStackSize( stack_sz ), &CoroutineFunc, &CoroFuncParams );
			if( FiberPtr == NULL )
			{
				ThrowIfNeed();
			}
			MY_ASSERT( FiberPtr != nullptr );
#else
			if( getcontext( &Context ) != 0 )
			{
				// Ошибка получения контекста текущего потока
				ThrowIfNeed();
				MY_ASSERT( false );
			}

			// Создаём новый контекст
			Context.uc_stack.ss_sp   = Stack.Ptr;
			Context.uc_stack.ss_size = Stack.Sz;
			Context.uc_link = nullptr; // Куда передаётся управление после завершения работы
			makecontext( &Context, ( void(*)() ) &CoroutineFunc, 1, &CoroFuncParams );
#endif
		} // Coroutine::Coroutine

		Coroutine::~Coroutine()
		{
			const uint8_t state_flag = StateFlag.exchange( FinishedFlag );

			CoroInfo *coro_info = ( CoroInfo* ) Internal.Get();

			// Удаляемая сопрограмма - та, что выполняется сейчас
			const bool is_cur_coro = ( coro_info != nullptr ) && ( coro_info->CurrentCoro == this );

			MY_ASSERT( is_cur_coro == ( ( InProgressFlag & state_flag ) == InProgressFlag ) );

#ifdef _WIN32
			MY_ASSERT( FiberPtr != nullptr );
#else
			MY_ASSERT( CreatedFromThread == ( Stack.Ptr == nullptr ) );
#endif

			if( CreatedFromThread )
			{
				// Удаляемая сопрограмма получена из потока ("основная" сопрограмма)
				MY_ASSERT( state_flag < FinishedFlag );
				MY_ASSERT( coro_info != nullptr );
				if( !is_cur_coro )
				{
					// Пытаемся удалить основную сопрограмму из другой сопрограммы,
					// либо из потока (не-сопрограммы)
					MY_ASSERT( false );
					std::terminate();
				}

				// Удаляем "основную" сопрограмму во время выполнения её самой
				// (в итоге сопрограмма продолжит работу как поток)
				Internal.Set( nullptr );
				delete coro_info;

#ifdef _WIN32
				// Преобразуем волокно в поток
				BOOL res = ConvertFiberToThread();
				MY_ASSERT( res != FALSE );
#endif
			}
			else
			{
				// Удаляемая сопрограмма создана не из потока (не "основная" сопрограмма)
				if( Started && ( state_flag != FinishedFlag ) )
				{
					// Пытаемся удалить запущенную, но незавершённую сопрограмму
					MY_ASSERT( false );
					std::terminate();
				}

#ifdef _WIN32
				// Удаляем волокно
				DeleteFiber( FiberPtr );
#endif
			}
		} // Coroutine::~Coroutine()

		bool Coroutine::SwitchTo( Coroutine **prev_coro )
		{
			CoroInfo *coro_info = ( CoroInfo* ) Internal.Get();
			Coroutine *cur_coro = coro_info != nullptr ? coro_info->CurrentCoro : nullptr;

			if( cur_coro == nullptr )
			{
				// Пытаемся перейти из потока в сопрограмму
				MY_ASSERT( coro_info == nullptr );
				throw Exception( ErrorCodes::FromThreadToCoro,
				                 "Cannot jump from thread to coroutine" );
			}

			if( cur_coro == this )
			{
				// Пытаемся перейти из текущей сопрограммы в неё же
				if( prev_coro != nullptr )
				{
					*prev_coro = cur_coro;
				}

				return true;
			}

			// Считаем, что сопрограмма не выполняется и не была завершена
			uint8_t cur_state = 0;
			if( !StateFlag.compare_exchange_strong( cur_state, InProgressFlag ) )
			{
				// Сопрограмма уже выполняется или завершена
				return false;
			}

			Started = true;

			// Нужно, чтобы обращаться к объекту после смены контекста
			coro_info->NextCoro = this;

#ifdef _WIN32
			// Если поток ещё не вызывал её - не сможем перейти на другую сопрограмму
			//ConvertThreadToFiber( nullptr );
			MY_ASSERT( ConvertThreadToFiber( nullptr ) == NULL );
			SwitchToFiber( FiberPtr );
#else
			MY_ASSERT( cur_coro != nullptr );
			if( swapcontext( &( cur_coro->Context ), &Context ) != 0 )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
			}
#endif

			// !!! Если попали сюда, то контекст другой:
			// this и остальные локальные переменные имеют неизвестные значения !!!
			coro_info = ( CoroInfo* ) Internal.Get();
			MY_ASSERT( coro_info != nullptr );
			
			cur_coro = coro_info->CurrentCoro;
			MY_ASSERT( cur_coro != coro_info->NextCoro );
			MY_ASSERT( cur_coro != nullptr );

			// Сбрасываем у предыдущей сопрограммы флаг "сопрограмма работает"
			cur_coro->StateFlag &= ~InProgressFlag;

			// Запоминаем указатель на текущую сопрограмму
			coro_info->CurrentCoro = coro_info->NextCoro;
			coro_info->NextCoro = nullptr;

			if( prev_coro != nullptr )
			{
				*prev_coro = cur_coro;
			}
			return true;
		} // bool Coroutine::SwitchTo( Coroutine **prev_coro )

		bool Coroutine::IsDone() const
		{
			return StateFlag.load() == FinishedFlag;
		}

		Coroutine* GetCurrentCoro()
		{
			CoroInfo *coro_info = ( CoroInfo* ) Coroutine::Internal.Get();
			return coro_info != nullptr ? coro_info->CurrentCoro : nullptr;
		}
	} // namespace Coro
} // namespace Bicycle
