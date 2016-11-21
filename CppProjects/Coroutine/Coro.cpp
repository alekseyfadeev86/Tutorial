#include "Coro.h"
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#endif

namespace Coro
{
#ifdef _WIN32
	static const err_code_t Success = ERROR_SUCCESS;
#else
	static const err_code_t Success = 0;
#endif

	Exception::Exception( err_code_t err_code ): std::runtime_error( strerror( err_code ) ),
	                                             ErrorCode( err_code )
	{}

	Exception::Exception( err_code_t err_code,
	                      const string &what ): std::runtime_error( what ),
	                                            ErrorCode( err_code )
	{}

	inline void ThrowIfNeed( err_code_t err )
	{
		if( err != Success )
		{
			throw Exception( err );
		}
	}

	inline void ThrowIfNeed()
	{
#ifdef _WIN32
		ThrowIfNeed( GetLastError() );
#else
		ThrowIfNeed( errno );
#endif
	}

	ThreadLocal::ThreadLocal()
	{
#ifdef _WIN32
		Key = TlsAlloc();
		ThrowIfNeed();
#else
		ThrowIfNeed( pthread_key_create( &Key, nullptr ) );
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
#else
		res = pthread_getspecific(  Key );
#endif

		ThrowIfNeed();
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

	ThreadLocal Coroutine::CurrentCoroutine;
	ThreadLocal Coroutine::NewCoroutine;

#ifdef _WIN32
	VOID CALLBACK Coroutine::CoroutineFunc( PVOID param )
#else
	Coroutine::Array::Array( size_t sz ): Ptr( nullptr ), Sz( 0 )
	{
		if( sz > 0 )
		{
			Ptr = new char[ sz ];
			Sz = sz;
		}
	}

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

		Coroutine *cur_coro = ( Coroutine* ) CurrentCoroutine.Get();
		if( cur_coro != nullptr )
		{
			// Сбрасываем флаг "занятости" предыдущей сопрограммы
			cur_coro->RunFlag.clear();
		}

		CurrentCoroutine.Set( ( void* ) task_coro_ptr->second );
		MY_ASSERT( ( Coroutine* ) CurrentCoroutine.Get() == task_coro_ptr->second );

		Coroutine *new_coro = task_coro_ptr->first ? task_coro_ptr->first() : nullptr;
		if( new_coro != nullptr )
		{
			// Переключаемся на новую сопрограмму
			new_coro->SwitchTo();

			// Если попали сюда - перейти не удалось, либо new_coro - текущая сопрограмма
			MY_ASSERT( false );
		}

		std::terminate();
	}

	Coroutine::Coroutine():
#ifdef _WIN32
	                        CreatedFromThread( true )
#else
							Stack()
#endif
	{
		if( CurrentCoroutine.Get() != nullptr )
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
			throw Exception( errno );
		}
#endif

		// Задаём указатель на текущую сопрограмму
		CurrentCoroutine.Set( ( void* ) this );
		MY_ASSERT( ( Coroutine* ) CurrentCoroutine.Get() == this );

		// Помечаем текущую сопрограмму как занятую
		RunFlag.test_and_set();
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
						  size_t stack_sz ):
#ifdef _WIN32
	                                         CreatedFromThread( false )
#else
	                                         Stack( EditStackSize( stack_sz ) )
#endif
	{
		if( !task )
		{
			throw std::invalid_argument( "Incorrect coroutine task" );
		}

		RunFlag.clear();

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
			throw Exception( errno );
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
#ifdef _WIN32
		MY_ASSERT( FiberPtr != nullptr );
		if( !RunFlag.test_and_set() )
		{
			// Удаляем волокно
			DeleteFiber( FiberPtr );
		}
		else if( !CreatedFromThread )
		{
			// Пытаемся удалить волокно, которое выполняется в данный момент
			MY_ASSERT( false );
			std::terminate();
		}
#endif
	}

	bool Coroutine::SwitchTo()
	{
		Coroutine *cur_coro = ( Coroutine* ) CurrentCoroutine.Get();

		if( cur_coro == this )
		{
			// Пытаемся перейти из текущей сопрограммы в неё же
			return true;
		}
		else if( RunFlag.test_and_set() )
		{
			// Сопрограмма уже выполняется
			return false;
		}

		// Нужно, чтобы обращаться к объекту после смены контекста
		NewCoroutine.Set( ( void* ) this );

#ifdef _WIN32
		// Если поток ещё не вызывал её - не сможем перейти на другую сопрограмму
		ConvertThreadToFiber( nullptr );
		SwitchToFiber( FiberPtr );
#else
		if( cur_coro == nullptr )
		{
			ucontext_t local_con;
			swapcontext( &local_con, &Context );
		}
		else
		{
			swapcontext( &( cur_coro->Context ), &Context );
		}

		ThrowIfNeed();
#endif

		// !!! Если попали сюда, то контекст другой:
		// this и остальные локальные переменные имеют неизвестные значения !!!
		cur_coro = ( Coroutine* ) CurrentCoroutine.Get();
		MY_ASSERT( cur_coro != ( Coroutine* ) NewCoroutine.Get() );
		if( cur_coro != nullptr )
		{
			cur_coro->RunFlag.clear();
		}

		CurrentCoroutine.Set( NewCoroutine.Get() );
		MY_ASSERT( CurrentCoroutine.Get() == NewCoroutine.Get() );
		NewCoroutine.Set( nullptr );

		return true;
	}
} // namespace Coro
