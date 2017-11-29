#include "CoroSrv/BasicDescriptor.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

namespace Bicycle
{
	namespace CoroService
	{
		void BasicDescriptor::CloseDescriptor( int fd, Error &err )
		{
			err = close( fd ) != 0 ? GetLastSystemError() : Error();
		}

		Error BasicDescriptor::InitAndRegisterNewDescriptor( int fd, const desc_ptr_t &desc_data_ptr )
		{
			MY_ASSERT( desc_data_ptr );
			MY_ASSERT( desc_data_ptr->Fd == -1 );

			// Переводим дескриптор в неблокирующий режим
			int fl = fcntl( fd, F_GETFL );
			if( ( fl == -1 ) || ( fcntl( fd, F_SETFL, fl | O_NONBLOCK ) != 0 ) )
			{
				return GetLastSystemError();
			}

			// Привязываем дескриптор к epoll-ам
			epoll_event ev_data;
			ev_data.data.ptr = ( void* ) desc_data_ptr.get();
			ev_data.events = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLET | EPOLLRDHUP;

			if( epoll_ctl( SrvRef.EpollFd, EPOLL_CTL_ADD, fd, &ev_data ) != 0 )
			{
				return GetLastSystemError();
			}

			// Записываем дескриптор в структуру, обнуляем её счётчики сработки epoll_wait
			desc_data_ptr->Fd = fd;
			desc_data_ptr->ReadQueue.second.clear();
			desc_data_ptr->WriteQueue.second.clear();
			desc_data_ptr->ReadOobQueue.second.clear();

			return Error();
		}

		Error BasicDescriptor::ExecuteIoTask( const IoTaskType &task,
		                                      IoTaskTypeEnum task_type )
		{
			if( SrvRef.MustBeStopped.load() )
			{
				// Сервис должен быть остановлен
				return Error( ErrorCodes::SrvStop, "Service is closing" );
			}

			if( !task )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "Incorrect task" );
			}

			// Указатель на текущую сопрограмму
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				MY_ASSERT( false );
				return Error( ErrorCodes::NotInsideSrvCoro,
				              "Incorrect function call: not inside service coroutine" );
			}

			MY_ASSERT( DescriptorData );
			SharedLocker<SharedSpinLock> lock( DescriptorData->Lock, true );
			MY_ASSERT( lock );
			MY_ASSERT( lock.Locked() );
			if( DescriptorData->Fd == -1 )
			{
				// Дескриптор не открыт
				return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
			}

			DescriptorStruct *desc_ptr = DescriptorData.get();

			MY_ASSERT( desc_ptr != nullptr );
			MY_ASSERT( ( ( uint8_t ) task_type ) < 3 );

			// Ошибка завершения
			Error err;

			// Указатель на список EpWaitStruct-ов, соответствующих
			// сопрограммам, ожидающим готовности дескриптора к
			// выполнению операции типа task_type
			EpWaitList *queue_ptr = nullptr;

			// Указатель на счётчик срабатываний epoll_wait-а
			// для типа задач task_type
			std::atomic_flag *flag_ptr = nullptr;

			EpWaitStruct ep_waiter( *cur_coro_ptr );
			MY_ASSERT( &( ep_waiter.CoroRef ) == cur_coro_ptr );
			MY_ASSERT( ep_waiter.LastEpollEvents == 0 );
			MY_ASSERT( !ep_waiter.WasCancelled );

			uint32_t cur_ep_ev = 0;
			switch( task_type )
			{
				case IoTaskTypeEnum::Read:
					queue_ptr = &( desc_ptr->ReadQueue.first );
					flag_ptr = &( desc_ptr->ReadQueue.second );
					cur_ep_ev = EPOLLIN;
					break;

				case IoTaskTypeEnum::Write:
					queue_ptr = &( desc_ptr->WriteQueue.first );
					flag_ptr = &( desc_ptr->WriteQueue.second );
					cur_ep_ev = EPOLLOUT;
					break;

				case IoTaskTypeEnum::ReadOob:
					queue_ptr = &( desc_ptr->ReadOobQueue.first );
					flag_ptr = &( desc_ptr->ReadOobQueue.second );
					cur_ep_ev = EPOLLPRI;
					break;
			}
			MY_ASSERT( queue_ptr != nullptr );
			MY_ASSERT( flag_ptr != nullptr );

			while( !err )
			{
				// Пробуем выполнить задачу
				MY_ASSERT( desc_ptr != nullptr );
				MY_ASSERT( !ep_waiter.WasCancelled );

				if( !lock )
				{
					// Такое будет, если вышли на второй "виток" цикла
					lock = SharedLocker<SharedSpinLock>( desc_ptr->Lock, true );

					if( DescriptorData->Fd == -1 )
					{
						// Дескриптор не открыт
						return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
					}
				}

				MY_ASSERT( lock );
				MY_ASSERT( lock.Locked() );

				// Пробуем выполнить задачу ввода-вывода
				do
				{
					// В случае прерывания сигналом, повторяем попытку
					flag_ptr->test_and_set(); // Взводим флаг, что сработки epoll_wait-а не было
					err = GetSystemErrorByCode( task( desc_ptr->Fd ) );
				}
				while( err.Code == EINTR );

				if( ( err.Code != EAGAIN ) &&
				    ( err.Code != EWOULDBLOCK ) )
				{
					// Операция завершена (успешно или нет - другой вопрос)
					break;
				}


				// Дескриптор не готов к выполнению требуемой операции,
				// ожидаем готовности с помощью epoll-а
				auto poster = GetPoster();
				MY_ASSERT( poster );
				std::function<void()> epoll_task = [ &err, &ep_waiter, poster, &lock,
													 desc_ptr, queue_ptr,
													 flag_ptr, cur_ep_ev ]
				{
					// Этот код выполняется из основной сопрограммы потока
					MY_ASSERT( lock );
					MY_ASSERT( lock.Locked() );
					MY_ASSERT( desc_ptr->Fd != -1 );
					MY_ASSERT( queue_ptr != nullptr );
					MY_ASSERT( flag_ptr != nullptr );

					EpWaitList::Unsafe waiters;
					{
						SharedLocker<SharedSpinLock> local_lock( std::move( lock ) );
	
						MY_ASSERT( !lock );
						MY_ASSERT( local_lock );
						MY_ASSERT( local_lock.Locked() );
	
						err = Error();
	
						// Добавляем элемент в очередь сопрограмм, ожидающих готовности дескриптора
						ep_waiter.LastEpollEvents = 0;
						MY_ASSERT( !ep_waiter.WasCancelled );
	
						if( !flag_ptr->test_and_set() )
						{
							// Было срабатывание epoll_wait-а
							local_lock.Unlock();
							bool switch_res = ep_waiter.CoroRef.SwitchTo();
							MY_ASSERT( switch_res );
							return;
						}
						
						if( !queue_ptr->Push( &ep_waiter ) )
						{
							// Добавили ep_waiter в список, но он был уже не пуст - выходим
							return;
						}
	
						// !!! с этого момента нельзя обращаться к переменным из стека ExecuteIoTask !!!
						// (другой поток мог уже перейти на ту сопрограмму)
						// переменные, переданные сюда копированием, пользовать можно, в т.ч., queue_ptr,
						// flag_ptr, которые ссылаются на поля DescriptorStruct-а, который 100% жив,
						// т.к. его блокировка не была отпущена
	
						// Проверяем флаг срабатываний epoll-а
						if( flag_ptr->test_and_set() )
						{
							// Флаг был установлен, срабатываний epoll_wait-а не было - выходим
							return;
						}
	
						waiters = queue_ptr->Release();
					} // SharedLocker<SharedSpinLock> local_lock( std::move( lock ) );
					
					if( !waiters )
					{
						// Список пуст: либо обработан по сработке epoll_wait-а,
						// либо при вызове Cancel или Close
						return;
					}

					EpWaitStruct *waiter_ptr = waiters.Pop();
					MY_ASSERT( waiter_ptr != nullptr );

					EpWaitStruct *ptr = nullptr;
					while( waiters )
					{
						ptr = waiters.Pop();
						poster( ptr->CoroRef );
					}

					bool res = waiter_ptr->CoroRef.SwitchTo();
					MY_ASSERT( res );

					// Сюда попадаем уже после смены контекста - остаётся только уйти
					return;
				}; // std::function<void()> epoll_task

				// Переходим в основную сопрограмму и настраиваем epoll.
				// Сюда возвращаемся, когда дескриптор будет готов к работе
				// или закрыт, либо в случае ошибки
				SetPostTaskAndSwitchToMainCoro( std::move( epoll_task ) );
				
				MY_ASSERT( !lock );

				if( ep_waiter.WasCancelled )
				{
					// Задача была отменена
					err.Code = ErrorCodes::OperationAborted;
					err.What = "Operation was aborted";
				}

				// TODO: ? обрабатывать EPOLLHUP и EPOLLRDHUP в ep_waiter.LastEpollEvents ?
//				else if( ( ep_waiter.LastEpollEvents & EPOLLHUP ) != 0 )
//				{
//					// Hang up happened on the associated file descriptor
//					// EPOLLHUP — закрытие файлового дескриптора
//				}
//				else if( ( ep_waiter.LastEpollEvents & EPOLLRDHUP ) != 0 )
//				{
//					// Stream socket peer closed connection, or shut down writing half of connection
//				}
			} // while( !err )

			return err;
		} // Error BasicDescriptor::ExecuteIoTask

		BasicDescriptor::BasicDescriptor(): AbstractCloser(),
		                                    DescriptorData( new DescriptorStruct,
		                                                    [ this ]( DescriptorStruct *ptr ){ SrvRef.DeleteQueue.Delete( ptr ); } )
		{
			// При удалении DescriptorStruct-а shared_ptr-ом
			// вместо собственно удаления структура будет помещена
			// в очередь на отложенное удаление
			MY_ASSERT( DescriptorData );
			DescriptorData->Fd = -1;
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

			MY_ASSERT( DescriptorData );
			LockGuard<SharedSpinLock> lock( DescriptorData->Lock );
			if( DescriptorData->Fd != -1 )
			{
				// Дескриптор уже открыт
				MY_ASSERT( DescriptorData->Fd != -1 );
				err.Code = ErrorCodes::AlreadyOpen;
				err.What = "Descriptor already open";
				return;
			}

			int new_fd = OpenNewDescriptor( err );
			if( err )
			{
				// Ошибка открытия нового дескриптора
				return;
			}
			MY_ASSERT( new_fd != -1 );

			err = InitAndRegisterNewDescriptor( new_fd, DescriptorData );
			MY_ASSERT( DescriptorData );
			MY_ASSERT( err || ( DescriptorData->Fd == new_fd ) );
			if( err )
			{
				Error e;
				CloseDescriptor( DescriptorData->Fd, e );
				DescriptorData->Fd = -1;
				return;
			}
		} // void BasicDescriptor::Open( Error &err )

		void BasicDescriptor::Close( Error &err )
		{
			err = Error();
			LockFree::ForwardList<EpWaitStruct*>::Unsafe coros;

			MY_ASSERT( DescriptorData );
			LockGuard<SharedSpinLock> lock( DescriptorData->Lock );
			if( DescriptorData->Fd == -1 )
			{
				return;
			}

			coros.Push( DescriptorData->ReadQueue.first.Release() );
			coros.Push( DescriptorData->WriteQueue.first.Release() );
			coros.Push( DescriptorData->ReadOobQueue.first.Release() );

			// Закрываем старый дескриптор
			int old_fd = DescriptorData->Fd;
			DescriptorData->Fd = -1;
			CloseDescriptor( old_fd, err );

			EpWaitStruct *ptr = nullptr;
			while( coros )
			{
				ptr = coros.Pop();
				MY_ASSERT( ptr != nullptr );
				ptr->WasCancelled = true;
				PostToSrv( ptr->CoroRef );
			}
		} // void BasicDescriptor::Close( Error &err )

		void BasicDescriptor::Cancel( Error &err )
		{
			err = Error();

			MY_ASSERT( DescriptorData );
			LockFree::ForwardList<EpWaitStruct*>::Unsafe coros;

			LockGuard<SharedSpinLock> lock( DescriptorData->Lock );
			coros.Push( DescriptorData->ReadQueue.first.Release() );
			coros.Push( DescriptorData->WriteQueue.first.Release() );
			coros.Push( DescriptorData->ReadOobQueue.first.Release() );

			EpWaitStruct *ptr = nullptr;
			while( coros )
			{
				ptr = coros.Pop();
				MY_ASSERT( ptr != nullptr );
				ptr->WasCancelled = true;
				PostToSrv( ptr->CoroRef );
			}
		} // void BasicDescriptor::Cancel( Error &err )

		bool BasicDescriptor::IsOpen() const
		{
			MY_ASSERT( DescriptorData );
			SharedLockGuard<SharedSpinLock> lock( DescriptorData->Lock );
			return DescriptorData->Fd != -1;
		}
	} // namespace CoroService
} // namespace Bicycle
