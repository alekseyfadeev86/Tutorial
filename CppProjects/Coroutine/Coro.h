#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <ucontext.h>
#include <pthread.h>
#endif

#include <functional>
#include <atomic>
#include <string>
#include <stdexcept>

#ifdef _DEBUG
#include <assert.h>
#define MY_ASSERT( E ) assert( E )
#else
#define MY_ASSERT( E )
#endif

namespace Coro
{
	using std::string;

#ifdef _WIN32
	typedef DWORD err_code_t;
#else
	typedef int err_code_t;
#endif

	struct Error
	{
		err_code_t Code;
		string What;
	};

	struct Exception: public std::runtime_error
	{
		const err_code_t ErrorCode;
		Exception( err_code_t err_code );
		Exception( err_code_t err_code, const string &what );
	};

	namespace ErrorCodes
	{
		/// Пытаемся преобразовать сопрограмму в сопрограмму
		const err_code_t CoroToCoro = 0xFFFFFFFF;
	}

	/// Класс локального хранилища потока
	class ThreadLocal
	{
		private:
#ifdef _WIN32
			DWORD Key;
#else
			pthread_key_t Key;
#endif

		public:
			ThreadLocal( const ThreadLocal& ) = delete;
			ThreadLocal& operator=( const ThreadLocal& ) = delete;

			ThreadLocal();
			~ThreadLocal();

			void* Get() const;
			void Set( void *ptr );
	};

	class Coroutine;

	/// Тип задачи сопрограммы. Результат - сопрограмма, на которую нужно переключиться после завершения
	typedef std::function<Coroutine*()> CoroTaskType;

	/// Класс сопрограммы
	class Coroutine
	{
		private:
			/// Хранит указатель на объект текущей сопрограммы
			static ThreadLocal CurrentCoroutine;

			/// Указатель на новую сопрограмму при смене контекста
			static ThreadLocal NewCoroutine;

#ifdef _WIN32
			/// Показывает, что сопрограмма была создана из потока
			const bool CreatedFromThread;
#endif

			/// Параметры функции сопрограммы
			std::pair<CoroTaskType, Coroutine*> CoroFuncParams;

#ifdef _WIN32
			/// Указатель на волокно, соответствующее сопрограмме
			LPVOID FiberPtr;

			static VOID CALLBACK CoroutineFunc( PVOID param );
#else
			/// Контекст сопрограммы
			ucontext_t Context;

			struct Array
			{
				char *Ptr;
				size_t Sz;

				Array( size_t sz = 0 );
				~Array();
			};

			/// Стек сопрограммы
			Array Stack;

			static void CoroutineFunc( void *param );
#endif

			/// Флаг, показывающий, выполняется ли уже текущая сопрограмма
			std::atomic_flag RunFlag;

		public:
			Coroutine( const Coroutine& ) = delete;
			Coroutine& operator=( const Coroutine& ) = delete;

			/**
			 * @brief Coroutine создание сопрограммы из текущей функции и стека
			 * @throw Exception в случае ошибки (например, если вызывается из сопрограммы)
			 */
			Coroutine();

			/**
			 * @brief Coroutine создание новой сопрограммы
			 * @param task исполняемая функция
			 * @param stack_sz размер стека сопрограммы
			 * (если stack_sz < SIGSTKSZ, то будет взят равным SIGSTKSZ
			 * @throw std::invalid_argument, если задана "пустая" задача
			 */
			Coroutine( CoroTaskType task, size_t stack_sz );

			~Coroutine();

			/**
			 * @brief SwitchTo переключение контекста потока на данную сопрограмму
			 * @return успешность операции. Провал может быть в двух случаях:
			 * функция сопрограммы была выполнена до конца или сопрограмма уже
			 * выполняется другим потоком
			 */
			bool SwitchTo();
	};
} // namespace Coro
