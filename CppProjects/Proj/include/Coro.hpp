#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <ucontext.h>
#include <pthread.h>
#endif

#include "Errors.hpp"
#include <functional>
#include <atomic>

#ifndef MY_ASSERT
#define MY_ASSERT( EXPR )
#endif

namespace Bicycle
{
	namespace ErrorCodes
	{
		/// Пытаемся преобразовать сопрограмму в сопрограмму
		const err_code_t CoroToCoro = 0xFFFFFFFD;

		/// Пытаемся перейти из потока в сопрограмму
		const err_code_t FromThreadToCoro = 0xFFFFFFFE;
	}

	namespace Coro
	{
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

		//-----------------------------------------------------------------------------------------
		
		class Coroutine;

		/// Тип задачи сопрограммы. Результат - сопрограмма, на которую нужно переключиться после завершения
		typedef std::function<Coroutine*()> CoroTaskType;

		/// Класс сопрограммы
		class Coroutine
		{
			friend Coroutine* GetCurrentCoro();

			private:
				/// Хранит потокозависимый указатель на "внутреннюю" структуру (для служебных целей)
				static ThreadLocal Internal;

				/// Флаг текущего состояния
				std::atomic<uint8_t> StateFlag;

				/// Показывает, что сопрограмма была создана из потока
				const bool CreatedFromThread;

				/// Показывает, была ли запущена сопрограмма
				bool Started;
				
				typedef std::pair<CoroTaskType, Coroutine*> coro_func_params_t;

				/// Параметры функции сопрограммы
				coro_func_params_t CoroFuncParams;

#ifdef _WIN32
				/// Указатель на волокно, соответствующее сопрограмме
				LPVOID FiberPtr;

				static VOID CALLBACK CoroutineFunc( PVOID param );
#else
				/// Контекст сопрограммы
				ucontext_t Context;

				struct Array
				{
					char* const Ptr;
					const size_t Sz;

					Array( const Array& ) = delete;
					Array& operator=( const Array& ) = delete;

					Array( size_t sz = 0 );
					~Array();
				};

				/// Стек сопрограммы
				Array Stack;

				static void CoroutineFunc( void *param );
#endif

			public:
				Coroutine( const Coroutine& ) = delete;
				Coroutine& operator=( const Coroutine& ) = delete;

				/**
				 * @brief Coroutine создание сопрограммы из текущей функции и стека
				 * !!! Должна быть удалена из того же потока, в которм была создана !!!
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
				 * @param prev_coro если ненулевой и операция выполнена успешно,
				 * по адресу prev_coro будет записан указатель на сопрограмму, из
				 * которой был совершён переход.
				 * @return успешность операции. Провал может быть в двух случаях:
				 * функция сопрограммы была выполнена до конца или сопрограмма уже
				 * выполняется другим потоком
				 */
				bool SwitchTo( Coroutine **prev_coro = nullptr );

				/// Показывает, завершена ли сопрограмма
				bool IsDone() const;
		};

		/// Возвращает указатель на текущую сопрограмму
		Coroutine* GetCurrentCoro();
	} // namespace Coro
} // namespace Bicycle
