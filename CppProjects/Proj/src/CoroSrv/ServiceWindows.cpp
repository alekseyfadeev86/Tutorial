#include <Ws2tcpip.h>
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

		void Service::Post( const std::function<void()> &task )
		{
			LockGuard<SpinLock> lock( TasksMutex );
			PostedTasks.push_back( task );
			while( PostQueuedCompletionStatus( Iocp, 0, 0, nullptr ) == FALSE )
			{
				// Ошибка
				MY_ASSERT( false );
			}
		} // Error Service::Post( const std::function<void()> &task )

		void Service::Execute()
		{
			DWORD bytes_count = 0;
			ULONG_PTR comp_key = 0;
			LPOVERLAPPED pov = nullptr;

			while( 1 )
			{
				BOOL res = GetQueuedCompletionStatus( Iocp, &bytes_count, &comp_key, &pov, INFINITE );
				if( res != FALSE )
				{
					// Успех
					if( pov == nullptr )
					{
						// Была добавлена задача через Post
						MY_ASSERT( bytes_count == 0 );
						MY_ASSERT( comp_key == 0 );
						std::function<void()> task;
						{
							LockGuard<SpinLock> lock( TasksMutex );
							if( PostedTasks.empty() )
							{
								MY_ASSERT( false );
								continue;
							}

							task = std::move( PostedTasks.front() );
							PostedTasks.pop_front();
						}

						if( task )
						{
							// Выполняем задачу
							task();
						}
						else
						{
							// Была извлечена "пустая" задача - означает завершение цикла
							// Добавляем задачу снова, чтобы другие потоки тоже получили
							// уведомление о необходимости завершить работу
							Post( task );
							return;
						}
					} // if( pov == nullptr )
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
			} // while( 1 )
		} // void Service::Execute()

		//-------------------------------------------------------------------------------

		void BasicDescriptor::CloseDescriptor( HANDLE fd, Error &err )
		{
			err = CloseHandle( fd ) == 0 ? GetLastSystemError() : Error();
		}

		Error BasicDescriptor::RegisterNewDescriptor( HANDLE fd )
		{
			if( CreateIoCompletionPort( fd, SrvRef.Iocp, 0, 0 ) == 0 )
			{
				// Ошибка
				return GetLastSystemError();
			}

			return Error();
		} // Error BasicDescriptor::RegisterNewDescriptor( HANDLE fd )

		Error BasicDescriptor::ExecuteIoTask( const IoTaskType &task, size_t &io_size )
		{
			if( SrvRef.MustBeStopped.load() )
			{
				// Сервис должен быть остановлен
				return Error( ErrorCodes::SrvStop, "Service is closing" );
			}

			if( !task )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "Incorrect I/O task" );
			}

			IocpStruct task_struct;
			memset( ( void* ) &task_struct.Ov, 0, sizeof( task_struct.Ov ) );
			task_struct.ErrorCode = ErrorCodes::Success;
			task_struct.Coro = GetCurrentCoro();
			task_struct.IoSize = 0;
			
			std::function<void()> coro_task = [ &task, &task_struct, this ]()
			{
				err_code_t err;
				{
					SharedLockGuard<SharedSpinLock> lock( FdLock );

					if( Fd != INVALID_HANDLE_VALUE )
					{
						err = task( Fd, task_struct );
					}
					else
					{
						err = ErrorCodes::NotOpen;
					}
				}

				if( ( err != ERROR_SUCCESS ) && // err не код успеха
				    ( err != ERROR_IO_PENDING ) ) // err не код ошибки "операция в процессе"
				{
					// Ошибка выполнения - ожидание Iocp не сработает на неё,
					// возвращаемся обратно в сопрограмму
					task_struct.ErrorCode = err;
					bool res = task_struct.Coro->SwitchTo();
					MY_ASSERT( res );
				}
			};
			
			// Переходим в основную сопрограмму, выполняем там задачу и затем, когда будет готов результат, возвращаемся обратно
			SetPostTaskAndSwitchToMainCoro( &coro_task );

			Error err( GetSystemErrorByCode( task_struct.ErrorCode ) );
			if( task_struct.ErrorCode == ErrorCodes::NotOpen )
			{
				err.What = "Descriptor is not open";
			}

			io_size = task_struct.IoSize;
			return err;
		}

		void BasicDescriptor::Open( Error &err )
		{
			if( SrvRef.MustBeStopped.load() )
			{
				// Сервис должен быть остановлен
				err.Code = ErrorCodes::SrvStop;
				err.What = "Service is closing";
				return;
			}

			err = Error();

			LockGuard<SharedSpinLock> lock( FdLock );
			if( Fd != INVALID_HANDLE_VALUE )
			{
				// Дескриптор уже открыт
				err.Code = ErrorCodes::AlreadyOpen;
				err.What = "Descriptor already open";
				return;
			}

			HANDLE new_fd = OpenNewDescriptor( err );

			if( err )
			{
				// Ошибка открытия нового дескриптора
				return;
			}

			err = RegisterNewDescriptor( new_fd );
			if( err )
			{
				Error e;
				CloseDescriptor( new_fd, e );
			}

			Fd = new_fd;
		}

		void BasicDescriptor::Close( Error &err )
		{
			err = Error();

			HANDLE old_fd;
			{
				LockGuard<SharedSpinLock> lock( FdLock );
				old_fd = Fd;
				Fd = INVALID_HANDLE_VALUE;
			}

			if( old_fd != INVALID_HANDLE_VALUE )
			{
				CloseDescriptor( old_fd, err );
			}
		}

		void BasicDescriptor::Cancel( Error &err )
		{
			err = Error();

			SharedLockGuard<SharedSpinLock> lock( FdLock );
			if( Fd == INVALID_HANDLE_VALUE )
			{
				return;
			}

			if( CancelIoEx( Fd, nullptr ) == 0 )
			{
				// Ошибка
				err = GetLastSystemError();
			}
		}

		bool BasicDescriptor::IsOpen() const
		{
			SharedLockGuard<SharedSpinLock> lock( FdLock );
			return Fd != INVALID_HANDLE_VALUE;
		}
	} // namespace CoroService
} // namespace Bicycle
