#include "CoroSrv/BasicDescriptor.hpp"

namespace Bicycle
{
	namespace CoroService
	{
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

		BasicDescriptor::BasicDescriptor(): AbstractCloser(),
		                                    Fd( INVALID_HANDLE_VALUE )
		{}

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
