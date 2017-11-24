﻿#include <Ws2tcpip.h>
#include "CoroSrv/Service.hpp"

namespace Bicycle
{
	namespace CoroService
	{
		void Service::Initialize()
		{
			// Создаём порт завершения ввода-вывода
			Iocp = CreateIoCompletionPort( INVALID_HANDLE_VALUE, 0, 0, 0 );
			if( Iocp == NULL )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
				throw Exception( ErrorCodes::UnknownError, "Unknown error while creating io completion port" );
			}
		}

		void Service::Close()
		{
			CloseHandle( Iocp );
		}

		void Service::Post( Coroutine *coro_ptr )
		{
			while( PostQueuedCompletionStatus( Iocp, 0, 0xFF, ( LPOVERLAPPED ) coro_ptr ) == FALSE )
			{
				// Ошибка
				MY_ASSERT( false );
			}
		}

		void Service::Execute()
		{
			DWORD bytes_count = 0;
			ULONG_PTR comp_key = 0;
			LPOVERLAPPED pov = nullptr;

			while( CoroCount.load() > 0 )
			{
				// Удаляем указатели на закрытые дескрипторы из списка (если нужно)
				RemoveClosedDescriptors();
				BOOL res = GetQueuedCompletionStatus( Iocp, &bytes_count, &comp_key, &pov, INFINITE );
				if( res != FALSE )
				{
					// Успех
					if( comp_key != 0 )
					{
						// Была добавлена задача через Post
						MY_ASSERT( comp_key == 0xFF );
						MY_ASSERT( bytes_count == 0 );
						Coroutine *coro_ptr = ( Coroutine* ) pov;

						if( coro_ptr != nullptr )
						{
							// Переходим в готовую к выполнению сопрограмму
							bool res = coro_ptr->SwitchTo();
							MY_ASSERT( res );
						}
						else if( CoroCount.load() == 0 )
						{
							// Был извлечён нулевой указатель на сопрограмму - означает завершение цикла
							// Добавляем указатель снова, чтобы другие потоки тоже получили
							// уведомление о необходимости завершить работу, и выходим
							Post( nullptr );
							return;
						}
					} // if( comp_key != 0 )
					else
					{
						// Событие на одном из дескрипторов
						IocpStruct *param = ( IocpStruct* ) pov;
						MY_ASSERT( param != nullptr );
						
						// Сохраняем результаты операции
						param->ErrorCode = ErrorCodes::Success;
						param->IoSize = ( size_t ) bytes_count;

						// Переходим в сопрограмму
						MY_ASSERT( param->Coro != nullptr );
						bool switch_res = param->Coro->SwitchTo();
						MY_ASSERT( switch_res );
					}
				} // if( res != FALSE )
				else
				{
					// Ошибка ожидания Iocp, либо ошибка при выполнении задачи
					err_code_t error_code = GetLastError();
					SetLastError( ErrorCodes::Success );

					if( pov != nullptr )
					{
						// WAIT_TIMEOUT - за заданное время готовых задач не появилось (???)
						// WSAENOTSOCK - бывает после закрытия сокета, либо если подсунуть заведомо неверный дескриптор
						// (соответственно, он уже отвязан от IOCP - ошибка возвращается конкретной функцией (напр., WSARecvFrom))
						// ERROR_ABANDONED_WAIT_0 - Iocp был закрыт
						// ERROR_OPERATION_ABORTED, ERROR_PROCESS_ABORTED - асинхронная задача ввода-вывода была отменена

						// Ошибка выполнения задачи
						IocpStruct *param = ( IocpStruct* ) pov;
						MY_ASSERT( param != nullptr );
						
						// Сохраняем результаты операции
						MY_ASSERT( error_code != ErrorCodes::Success );
						param->ErrorCode = error_code != ErrorCodes::Success ? error_code : ErrorCodes::UnknownError;
						param->IoSize = ( size_t ) bytes_count;

						// Переходим в сопрограмму
						MY_ASSERT( param->Coro != nullptr );
						bool switch_res = param->Coro->SwitchTo();
						MY_ASSERT( switch_res );
					}
					else
					{
						// Ошибка в Iocp
						ThrowIfNeed( GetSystemErrorByCode( error_code ) );
						MY_ASSERT( false );
						throw Exception( ErrorCodes::UnknownError, "Unknown error while Iocp wait" );
					}
				} // if( res != FALSE ) {...} else

				// Выполняем задачу, "оставленную" дочерней сопрограммой
				ExecLeftTasks();
			} // while( CoroCount.load() > 0 )
		} // void Service::Execute()
	} // namespace CoroService
} // namespace Bicycle
