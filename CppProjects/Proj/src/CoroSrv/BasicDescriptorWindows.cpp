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

		Error BasicDescriptor::ExecuteIoTask( const IoTaskType &task, size_t &io_size, uint64_t timeout_msec )
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

			IocpStruct io_task_struct_stat;
			std::shared_ptr<IocpStruct> io_task_struct_ptr;

			// Необходимо хранить указатель на таймер именно здесь, а не в coro_task-е,
			// чтобы он 100% был жив, пока задача не завершится, либо не будет отменена
			const auto timer_ptr = ( timeout_msec != Infinite ) ? ( TimerQueuePtr ? TimerQueuePtr : TimeTasksQueue::GetQueue() ) : std::shared_ptr<TimeTasksQueue>();
			TimeTasksQueue::task_type timer_task_ptr;

			if( timer_ptr )
			{
				// Формируем задачу для таймера
				MY_ASSERT( timeout_msec != Infinite );
				io_task_struct_ptr.reset( new IocpStruct );
				const std::weak_ptr<IocpStruct> io_task_struct_wptr( io_task_struct_ptr );

				auto timer_task = [ fd, io_task_struct_wptr ]()
				{
					auto ptr = io_task_struct_wptr.lock();
					if( ptr )
					{
						// Отменяем задачу ввода-вывода
						BOOL res = CancelIoEx( fd, ( LPOVERLAPPED ) ptr.get() );
						MY_ASSERT( ( res != FALSE ) || ( WSAGetLastError() == ERROR_NOT_FOUND ) || ( WSAGetLastError() == ERROR_INVALID_HANDLE ) );
					}
				};
					
				timer_task_ptr.reset( new CancellableTask( timer_task ) );
			} // if( timer_ptr )

			MY_ASSERT( ( bool ) io_task_struct_ptr == ( bool ) timer_ptr );
			MY_ASSERT( ( bool ) io_task_struct_ptr == ( bool ) timer_task_ptr );
			MY_ASSERT( ( bool ) io_task_struct_ptr == ( timeout_msec != Infinite ) );

			IocpStruct &task_struct = io_task_struct_ptr ? *io_task_struct_ptr : io_task_struct_stat;
			memset( ( void* ) &task_struct.Ov, 0, sizeof( task_struct.Ov ) );
			task_struct.ErrorCode = ErrorCodes::Success;
			task_struct.Coro = GetCurrentCoro();
			task_struct.IoSize = 0;
			
			std::function<void()> coro_task = [ &task, &task_struct, this, timeout_msec, timer_task_ptr, timer_ptr ]()
			{
				MY_ASSERT( ( timeout_msec == Infinite ) != ( bool ) timer_task_ptr );
				MY_ASSERT( ( bool ) timer_task_ptr == ( bool ) timer_ptr );

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
					timer_ptr.reset();
					timer_task_ptr.reset();
					task_struct.ErrorCode = err;
					bool res = task_struct.Coro->SwitchTo();
					MY_ASSERT( res );
					return;
				}
				else if( timer_ptr )
				{
					// Взводим таймер отмены операции
					MY_ASSERT( timeout_msec != Infinite );
					MY_ASSERT( timer_task_ptr );
					timer_ptr->Post( timer_task_ptr, timeout_msec );
				}

				// Задача ещё не выполнена, результат будет получен через IOCP
			};
			
			// Переходим в основную сопрограмму, выполняем там задачу и затем, когда будет готов результат, возвращаемся обратно
			SetPostTaskAndSwitchToMainCoro( &coro_task );

			Error err( GetSystemErrorByCode( task_struct.ErrorCode ) );
			if( task_struct.ErrorCode == ErrorCodes::NotOpen )
			{
				err.What = "Descriptor is not open";
			}

			io_size = task_struct.IoSize;

			if( timer_task_ptr )
			{
				// Отменяем задачу таймера, если она ещё не была выполнена
				MY_ASSERT( timer_ptr );
				timer_task_ptr->Cancel();
				timer_task_ptr.reset();
			}

			return err;
		}

		BasicDescriptor::BasicDescriptor( uint64_t read_timeout_microsec,
		                                  uint64_t write_timeout_microsec ):
		    AbstractCloser(),
		    ReadTimeoutMicrosec( read_timeout_microsec ),
		    WriteTimeoutMicrosec( write_timeout_microsec ),
		    Fd( INVALID_HANDLE_VALUE ),
		    TimerQueuePtr( InitTimeout( read_timeout_microsec, write_timeout_microsec ) )
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
