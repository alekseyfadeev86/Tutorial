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
			ThrowIfNeed();
#else
			ThrowIfNeed( GetSystemErrorByCode( pthread_key_create( &Key, nullptr ) ) );
#endif
		}

		ThreadLocal::~ThreadLocal()
		{
#ifdef _WIN32
			TlsFree( Key );
			MY_ASSERT( GetLastError() == 0 );
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
			ThrowIfNeed();
#else
			res = pthread_getspecific( Key );
#endif

			return res;
		}

		void ThreadLocal::Set( void *ptr )
		{
#ifdef _WIN32
			TlsSetValue( Key, ptr );
			ThrowIfNeed();
#else
			ThrowIfNeed( pthread_setspecific( Key, ptr ) );
#endif
		}

		//-----------------------------------------------------------------------------------------

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
			auto task_coro_ptr = ( std::pair<CoroTaskType, Coroutine*>* ) param;

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
				MY_ASSERT( cur_coro->StateFlag.load() <= 3 );
				cur_coro->StateFlag &= 0xFE;
			}

			info->CurrentCoro = task_coro_ptr->second;

			// Выполняем функцию сопрограммы
			Coroutine *new_coro = task_coro_ptr->first ? task_coro_ptr->first() : nullptr;

			// Помечаем сопрограмму как выполненную
			task_coro_ptr->second->StateFlag |= 0x2;

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
			ThrowIfNeed();
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

			// Помечаем текущую сопрограмму как занятую
			StateFlag.store( 1 );
		}

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
				throw std::invalid_argument( "Incorrect coroutine task" );
			}

			CoroFuncParams.first = std::move( task );
			CoroFuncParams.second = this;

#ifdef _WIN32
			// Создаём новое волокно
			FiberPtr = CreateFiber( EditStackSize( stack_sz ), &CoroutineFunc, &CoroFuncParams );
			ThrowIfNeed();
			MY_ASSERT( FiberPtr != nullptr );
#else
			if( getcontext( &Context ) != 0 )
			{
				// Ошибка получения контекста текущего потока
				ThrowIfNeed();
				MY_ASSERT( false );
			}

			// Создаём новый контекст
			Context.uc_stack.ss_sp = Stack.Ptr;
			Context.uc_stack.ss_size = Stack.Sz;
			Context.uc_link = nullptr;
			makecontext( &Context, ( void(*)() ) &CoroutineFunc, 1, &CoroFuncParams );
#endif
		}

		Coroutine::~Coroutine()
		{
			const uint8_t state_flag = StateFlag.exchange( 2 );

			CoroInfo *coro_info = ( CoroInfo* ) Internal.Get();

			const bool is_cur_coro = ( coro_info != nullptr ) && ( coro_info->CurrentCoro == this );

			MY_ASSERT( is_cur_coro == ( ( 1 & state_flag ) == 1 ) );

#ifdef _WIN32
			MY_ASSERT( FiberPtr != nullptr );
#else
			MY_ASSERT( CreatedFromThread == ( Stack.Ptr == nullptr ) );
#endif

			if( CreatedFromThread )
			{
				// Удаляемая сопрограмма получена из потока
				MY_ASSERT( state_flag < 2 );
				MY_ASSERT( coro_info != nullptr );
				if( !is_cur_coro )
				{
					// Пытаемся удалить основную сопрограмму из другой сопрограммы,
					// либо из потока (не-сопрограммы)
					MY_ASSERT( false );
					std::terminate();
				}

				Internal.Set( nullptr );
				delete coro_info;

#ifdef _WIN32
				// Преобразуем волокно в поток
				BOOL res = ConvertFiberToThread();
				MY_ASSERT( res != 0 );
#endif
			}
			else
			{
				if( Started && ( state_flag != 2 ) )
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

			uint8_t cur_state = 0;
			if( !StateFlag.compare_exchange_strong( cur_state, 1 ) )
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
			cur_coro = coro_info != nullptr ? coro_info->CurrentCoro : nullptr;
			MY_ASSERT( coro_info != nullptr );
			MY_ASSERT( cur_coro != coro_info->NextCoro );
			MY_ASSERT( cur_coro != nullptr );

			// Сбрасываем у предыдущей сопрограммы флаг "сопрограмма работает"
			cur_coro->StateFlag &= 0xFE;

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
			return StateFlag.load() == 2;
		}

		Coroutine* GetCurrentCoro()
		{
			CoroInfo *coro_info = ( CoroInfo* ) Coroutine::Internal.Get();
			return coro_info != nullptr ? coro_info->CurrentCoro : nullptr;
		}
	} // namespace Coro
} // namespace Bicycle
