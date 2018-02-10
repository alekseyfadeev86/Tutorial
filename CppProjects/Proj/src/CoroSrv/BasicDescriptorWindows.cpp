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
			auto timer_ptr = ( ( timeout_msec != TimeoutInfinite ) && ( timeout_msec != 0 ) ) ? ( TimerQueuePtr ? TimerQueuePtr : TimeTasksQueue::GetQueue() ) : std::shared_ptr<TimeTasksQueue>();
			TimeTasksQueue::task_type timer_task_ptr;

			if( timeout_msec != TimeoutInfinite )
			{
				// Формируем задачу для таймера
				io_task_struct_ptr.reset( new IocpStruct );
				const std::weak_ptr<IocpStruct> io_task_struct_wptr( io_task_struct_ptr );

				HANDLE fd;
				{
					SharedLockGuard<SharedSpinLock> lock( FdLock );
					fd = Fd;
				}

				auto timer_task = [ fd, io_task_struct_wptr ]()
				{
					auto ptr = io_task_struct_wptr.lock();
					if( ptr )
					{
						// Отменяем задачу ввода-вывода
						BOOL res = CancelIoEx( fd, &ptr->Ov );
						MY_ASSERT( ( res != FALSE ) || ( WSAGetLastError() == ERROR_NOT_FOUND ) || ( WSAGetLastError() == ERROR_INVALID_HANDLE ) );
					}
				};
					
				timer_task_ptr.reset( new CancellableTask( timer_task ) );
			} // if( timer_ptr )

			MY_ASSERT( !( timer_ptr && !io_task_struct_ptr ) );
			MY_ASSERT( ( bool ) io_task_struct_ptr == ( bool ) timer_task_ptr );
			MY_ASSERT( ( bool ) io_task_struct_ptr == ( timeout_msec != TimeoutInfinite ) );

			IocpStruct &task_struct = io_task_struct_ptr ? *io_task_struct_ptr : io_task_struct_stat;
			memset( ( void* ) &task_struct.Ov, 0, sizeof( task_struct.Ov ) );
			task_struct.ErrorCode = ErrorCodes::Success;
			task_struct.Coro = GetCurrentCoro();
			task_struct.IoSize = 0;
			
			std::function<void()> coro_task = [ &task, &task_struct, this, timeout_msec,
			                                    timer_task_ptr, timer_ptr ]() mutable
			{
				MY_ASSERT( ( timeout_msec == TimeoutInfinite ) != ( bool ) timer_task_ptr );
				MY_ASSERT( !( timer_ptr && !timer_task_ptr ) );

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
				
				// !!! Если попали сюда, то обращаться к полям BasicDescriptor-а,
				// task-у и task_struct-у небезопасно, т.к. есть шанс, что другой
				// поток выполняет ту сопрограмму и вышел из функции !!!
				if( timer_ptr )
				{
					// Взводим таймер отмены операции
					MY_ASSERT( timeout_msec != TimeoutInfinite );
					MY_ASSERT( timer_task_ptr );
					timer_ptr->ExecuteAfter( timer_task_ptr, std::chrono::microseconds( timeout_msec ) );
				}
				else if( timer_task_ptr )
				{
					// Неблокирующая операция - выполняем немедленную отмену
					MY_ASSERT( timeout_msec == 0 );
					( *timer_task_ptr )();
				}

				// Задача ещё не выполнена, результат будет получен через IOCP
			};
			
			// Переходим в основную сопрограмму, выполняем там задачу и затем, когда будет готов результат, возвращаемся обратно
			SetPostTaskAndSwitchToMainCoro( std::move( coro_task ) );

			Error err( GetSystemErrorByCode( task_struct.ErrorCode ) );
			if( err.Code == ErrorCodes::NotOpen )
			{
				err.What = "Descriptor is not open";
			}

			io_size = task_struct.IoSize;

			if( timer_task_ptr )
			{
				// Отменяем задачу таймера, если она ещё не была выполнена
				MY_ASSERT( timer_ptr || ( timeout_msec == 0 ) );

				if( timer_task_ptr->IsCancelled() )
				{
					if( err.Code == ERROR_OPERATION_ABORTED )
					{
						// Таймаут
						err.Code = ErrorCodes::TimeoutExpired;
						err.What = "Timeout expired";
					}
				}
				else
				{
					timer_task_ptr->Cancel();
				}

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
		    TimerQueuePtr( InitTimer( read_timeout_microsec, write_timeout_microsec ) )
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

			err.Reset();

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
			err.Reset();

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
			err.Reset();

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
