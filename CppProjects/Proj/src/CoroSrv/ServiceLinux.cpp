#include "CoroService.hpp"
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
		inline void CheckOperationSuccess( int res )
		{
			if( res != 0 )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
				throw Exception( ErrorCodes::UnknownError, "Unknown error" );
			}
		}

		void Service::Initialize()
		{
			// Создаём анонимный неблокирующий канал для добавления в очередь готовых к исполнению задач
			CheckOperationSuccess( pipe2( PostPipe, O_NONBLOCK ) );
			MY_ASSERT( PostPipe[ 0 ] != -1 );
			MY_ASSERT( PostPipe[ 1 ] != -1 );

			// Создаём объекты epoll
			for( size_t t = 0; t < 4; ++t )
			{
				EpollFds[ t ] = epoll_create1( EPOLL_CLOEXEC );
				ThrowIfNeed();
				if( EpollFds[ t ] == -1 )
				{
					MY_ASSERT( false );
					throw Exception( ErrorCodes::UnknownError, "Unknown error" );
				}

				if( t > 0 )
				{
					// Привязываем epoll к основному
					epoll_event ev_data;
					ev_data.data.u64 = t;
					ev_data.events = EPOLLIN;
					CheckOperationSuccess( epoll_ctl( EpollFds[ 0 ], EPOLL_CTL_ADD, EpollFds[ t ], &ev_data ) );
				}
			}

			// Привязка "читающего конца" анонимного канала к epoll-у
			epoll_event ev_data;
			ev_data.data.u64 = 0;
			ev_data.events = EPOLLIN;
			CheckOperationSuccess( epoll_ctl( EpollFds[ 0 ], EPOLL_CTL_ADD, PostPipe[ 0 ], &ev_data ) );
		} // void Service::Initialize()

		void Service::Close()
		{
			close( PostPipe[ 0 ] );
			close( PostPipe[ 1 ] );

			for( size_t t = 0; t < 4; ++t )
			{
				close( EpollFds[ t ] );
			}
		}

		void Service::Post( Coroutine *coro_ptr )
		{
			LockGuard<SpinLock> lock( CorosMutex );
			bool need_to_write = CoroutinesToExeceute.empty();
			CoroutinesToExeceute.push_back( coro_ptr );
			if( !need_to_write )
			{
				// Записываем 1 байт только, если очередь была пуста
				return;
			}

			// Очередь была пуста - записываем 1 байт в канал, чтобы
			// epoll среагировал
			char c = 123;
			int res = -1;

			do
			{
				res = write( PostPipe[ 1 ], &c, 1 );
				MY_ASSERT( res <= 1 );
				MY_ASSERT( res != 0 );
				MY_ASSERT( ( res != -1 ) || ( errno == EINTR ) ); // Прервано сигналом
			}
			while( res < 1 );
		} // void Service::Post( Coroutine *coro_ptr )

		void Service::Execute()
		{
			epoll_event ev_data;
			while( CoroCount.load() > 0 )
			{
				// Удаляем указатели на закрытые дескрипторы из списка (если нужно)
				RemoveClosedDescriptors();

				// Подчищаем очередь на удаление
				// (удаляем что можно)
				DeleteQueue.Clear();

				int res = epoll_wait( EpollFds[ 0 ], &ev_data, 1, -1 );
				if( res == -1 )
				{
					Error err = GetLastSystemError();
					if( err.Code == EINTR )
					{
						// Ожидание прервано сигналом
						continue;
					}
					ThrowIfNeed( err );
				}
				else if( res == 0 )
				{
					MY_ASSERT( false );
					continue;
				}
				MY_ASSERT( res == 1 );


				// Получено событие epoll - обрабатываем
				if( ev_data.data.u64 == 0 )
				{
					// Событие на PostPipe[ 0 ]
					Coroutine *coro_to_exec_ptr = nullptr;
					{
						LockGuard<SpinLock> lock( CorosMutex );
						if( CoroutinesToExeceute.empty() )
						{
							// Сопрограмм нет
							continue;
						}

						coro_to_exec_ptr = CoroutinesToExeceute.front();
						CoroutinesToExeceute.pop_front();

						if( CoroutinesToExeceute.empty() )
						{
							// Была извлечена последняя сопрограмма - считываем байт,
							// чтобы epoll перестал срабатывать на канал
							char c = 0;
							do
							{
								int res = read( PostPipe[ 0 ], &c, 1 );
								MY_ASSERT( res <= 1 );
								MY_ASSERT( res != 0 );
								MY_ASSERT( ( res != -1 ) || ( ( c = ( char ) errno ) == EINTR ) ); // Прервано сигналом
							}
							while( res < 1 );

							MY_ASSERT( c == 123 );
						} // if( CoroutinesToExeceute.empty() )
					}

					if( coro_to_exec_ptr != nullptr )
					{
						// Переходим в готовую к выполнению сопрограмму
						bool res = coro_to_exec_ptr->SwitchTo();
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
				} // if( ev_data.data.ptr == nullptr )
				else
				{
					// Захватываем "эпоху" (пока она захвачена - 100% никто не удалит структуру, на которую указывает ev_data)
					auto epoch = DeleteQueue.EpochAcquire();

					MY_ASSERT( ( ev_data.data.u64 > 0 ) && ( ev_data.data.u64 < 4 ) );
					res = epoll_wait( EpollFds[ ev_data.data.u64 ], &ev_data, 1, 0 );
					if( res == 0 )
					{
						// "Ложное срабатывание" (другой поток уже успел обработать событие)
						continue;
					}
					else if( res == -1 )
					{
						Error err = GetLastSystemError();
						if( err.Code == EINTR )
						{
							// Ожидание прервано сигналом
							continue;
						}
						ThrowIfNeed( err );
					}
					MY_ASSERT( res == 1 );

					if( ev_data.data.ptr == nullptr )
					{
						// Почему-то такое бывает при привязке сокета TCP к epoll-у
						continue;
					}

					// Событие на одном из дескрипторов
					EpWaitList *param = ( EpWaitList* ) ev_data.data.ptr;
					MY_ASSERT( param != nullptr );

					auto waiters = param->Release();

					// "Отпускаем эпоху": с этого момента param может быть удалён
					// другим потоком
					epoch.Release();
					param = nullptr;

#ifdef _DEBUG
					try
					{
#endif
					EpWaitStruct *ptr = nullptr;
					while( waiters )
					{
						ptr = waiters.Pop();
						MY_ASSERT( ptr != nullptr );
						ptr->LastEpollEvents = ev_data.events;
						Post( &ptr->CoroRef );
					} // while( waiters )
#ifdef _DEBUG
					}
					catch( const std::out_of_range& )
					{
						MY_ASSERT( false );
					}
					catch( ... )
					{
						MY_ASSERT( false );
					}
#endif
				} // if( ev_data.data.ptr == nullptr )...else

				// Выполняем задачу, "оставленную" дочерней сопрограммой
				ExecLeftTasks();
			} // while( CoroCount.load() > 0 )
		} // void Service::Execute()

		//-------------------------------------------------------------------------------

		EpWaitStruct::EpWaitStruct( Coroutine &coro_ref ): CoroRef( coro_ref ),
		                                                   LastEpollEvents( 0 ),
		                                                   WasCancelled( false ) {}

		void BasicDescriptor::CloseDescriptor( int fd, Error &err )
		{
			err = close( fd ) != 0 ? GetLastSystemError() : Error();
		}

		Error BasicDescriptor::RegisterNewDescriptor( int fd )
		{
			// Переводим дескриптор в неблокирующий режим
			int fl = fcntl( fd, F_GETFL );
			if( ( fl == -1 ) || ( fcntl( fd, F_SETFL, fl | O_NONBLOCK ) != 0 ) )
			{
				return GetLastSystemError();
			}

			// Привязываем дескриптор к epoll-ам
			epoll_event ev_data;
			ev_data.data.ptr = nullptr;
			ev_data.events = 0;

			for( uint8_t t = 1; t < 4; ++t )
			{
				if( epoll_ctl( SrvRef.EpollFds[ t ], EPOLL_CTL_ADD, fd, &ev_data ) != 0 )
				{
					return GetLastSystemError();
				}
			}

			return Error();
		} // Error BasicDescriptor::RegisterNewDescriptor( int fd )

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

			std::shared_ptr<DescriptorStruct> desc_data;
			{
				// Копируем разделяемый указатель на дескриптор и его очереди
				SharedLockGuard<SharedSpinLock> lock( DescriptorLock );
				desc_data = DescriptorData;
			}

			if( !desc_data )
			{
				// Дескриптор не открыт
				return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
			}

			MY_ASSERT( ( ( uint8_t ) task_type ) < 3 );

			// Ошибка завершения
			Error err;

			// Указатель на список EpWaitStruct-ов, соответствующих
			// сопрогрраммам, ожидающим готовности дескриптора к
			// выполнению операции типа task_type
			EpWaitList *queue_ptr = nullptr;

			// Битовая маска событий дескриптора, которых ждёт epoll
			int epoll_mode = EPOLLONESHOT | EPOLLRDHUP;

			// Дескриптор epoll-а, соответствующего task_type-у
			int epoll_fd = SrvRef.EpollFds[ ( uint8_t ) task_type + 1 ];
			MY_ASSERT( epoll_fd != -1 );

			// Указатель на текущую сопрограмму
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				MY_ASSERT( false );
				return Error( ErrorCodes::NotInsideSrvCoro,
				              "Incorrect function call: not inside service coroutine" );
			}

			EpWaitStruct ep_waiter( *cur_coro_ptr );
			MY_ASSERT( &( ep_waiter.CoroRef ) == cur_coro_ptr );
			MY_ASSERT( ep_waiter.LastEpollEvents == 0 );
			MY_ASSERT( !ep_waiter.WasCancelled );

			switch( task_type )
			{
				case IoTaskTypeEnum::Read:
					queue_ptr = &( desc_data->ReadQueue );
					epoll_mode |= EPOLLIN;
					break;

				case IoTaskTypeEnum::Write:
					queue_ptr = &( desc_data->WriteQueue );
					epoll_mode |= EPOLLOUT;
					break;

				case IoTaskTypeEnum::ReadOob:
					queue_ptr = &( desc_data->ReadOobQueue );
					epoll_mode |= EPOLLPRI;
					break;
			}

			MY_ASSERT( queue_ptr != nullptr );

			while( !err )
			{
				// Пробуем выполнить задачу
				{
					// Захватываем разделяемую блокировку на дескриптор
					SharedLockGuard<SharedSpinLock> lock( desc_data->Lock );
					if( desc_data->Fd == -1 )
					{
						// Дескриптор был закрыт
						err.Code = ErrorCodes::NotOpen;
						err.What = "Descriptor was closed";
						break;
					}

					// Пробуем выполнить задачу ввода-вывода
					do
					{
						// В случае прерывания сигналом, повторяем попытку
						err = GetSystemErrorByCode( task( desc_data->Fd ) );
					}
					while( err.Code == EINTR );
				}

				if( ( err.Code != EAGAIN ) &&
				    ( err.Code != EWOULDBLOCK ) )
				{
					// Операция завершена (успешно или нет - другой вопрос)
					break;
				}

				// Дескриптор не готов к выполнению требуемой операции,
				// ожидаем готовности с помощью epoll-а
				std::function<void()> epoll_task = [ & ]
				{
					// Этот код выполняется из основной сопрограммы потока
					err = Error();
					bool success = false;

					{
						// Захватываем разделяемую блокировку дескриптора,
						// настраивает соответствующий задаче дескриптор epoll
						// на ожидание готовности текущего дескриптора
						SharedLockGuard<SharedSpinLock> lock( desc_data->Lock );

						if( desc_data->Fd != -1 )
						{
							// Дескриптор не был закрыт
							MY_ASSERT( queue_ptr != nullptr );
							int epoll_res = -1;

							ep_waiter.LastEpollEvents = 0;
							MY_ASSERT( !ep_waiter.WasCancelled );

							// Добавляем элемент в очередь сопрограмм, ожидающих готовности дескриптора
							if( queue_ptr->Push( &ep_waiter ) )
							{
								// Добавили первый элемент в очередь ожидающих
								// сопрограмм - необходимо настроить epoll
								epoll_event ev_data;
								ev_data.data.ptr = ( void* ) queue_ptr;
								ev_data.events = epoll_mode;
								MY_ASSERT( epoll_fd != -1 );
								epoll_res = epoll_ctl( epoll_fd, EPOLL_CTL_MOD, desc_data->Fd, &ev_data );
							}
							else
							{
								// В списке уже есть другие ожидающие
								// сопрограммы - считаем, что epoll уже настроен
								epoll_res = 0;
							}

							if( epoll_res == 0 )
							{
								// Успех
								success = true;
                                                                MY_ASSERT( !err );
							}
							else
							{
								// Ошибка настройки epoll: запоминаем ошибку, возвращаемся в сопрограмму
								err = GetLastSystemError();

								MY_ASSERT( err );
                                                                MY_ASSERT( !success );
							}
                                                        MY_ASSERT( success == !err );
						} // if( desc_data->Fd != -1 )
						else
						{
							// Дескриптор был закрыт: запоминаем ошибку,
							// возвращаемся в сопрограмму
							err.Code = ErrorCodes::NotOpen;
							err.What = "Descriptor was closed";

                                                        MY_ASSERT( err );
                                                        MY_ASSERT( !success );
                                                        MY_ASSERT( success == !err );
						}
					} // SharedLockGuard<SharedSpinLock> lock( desc_data->Lock );

					MY_ASSERT( success == !err );
					if( !success )
					{
						// Произошла ошибка, возвращаемся в сопрограмму
						bool rs = ep_waiter.CoroRef.SwitchTo();
						MY_ASSERT( rs );
						// !!! Внимание: после перехода сюда из другой сопрограммы внутри epoll_task-а
						// нельзя больше обращаться ни к каким переменным !!!
						return;
					}

					MY_ASSERT( success );
				};

				// Переходим в основную сопрограмму и настраиваем epoll.
				// Сюда возвращаемся, когда дескриптор будет готов к работе
				// или закрыт, либо в случае ошибки
				SetPostTaskAndSwitchToMainCoro( &epoll_task );

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

		std::shared_ptr<DescriptorStruct> BasicDescriptor::NewDescriptorStruct( int fd )
		{
			std::shared_ptr<DescriptorStruct> res;
			if( fd == -1 )
			{
				return res;
			}

			// При удалении DescriptorStruct-а shared_ptr-ом
			// вместо собственно удаления структура будет помещена
			// в очередь на отложенное удаление
			res.reset( new DescriptorStruct,
			[ this ] ( DescriptorStruct *ptr )
			{
				MY_ASSERT( ptr != nullptr );
				{
					LockGuard<SharedSpinLock> lock( ptr->Lock );
					MY_ASSERT( ptr->Fd == -1 ); // Возможно, потом уберу

					if( ptr->Fd != -1 )
					{
						close( ptr->Fd );
					}
				}

				SrvRef.DeleteQueue.Delete( ptr );
			});
			MY_ASSERT( res );
			res->Fd = fd;

			return res;
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
			LockGuard<SharedSpinLock> lock( DescriptorLock );
			if( DescriptorData )
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

			err = RegisterNewDescriptor( new_fd );
			if( err )
			{
				Error e;
				CloseDescriptor( new_fd, e );
				return;
			}

			DescriptorData = NewDescriptorStruct( new_fd );
			MY_ASSERT( DescriptorData );
			MY_ASSERT( DescriptorData->Fd == new_fd );
		}

		void BasicDescriptor::Close( Error &err )
		{
			err = Error();
			LockFree::ForwardList<EpWaitStruct*>::Unsafe coro_queues[ 3 ];
			std::shared_ptr<DescriptorStruct> old_desc_data;

			{
				// Переносим данные дескриптора в локальный буфер
				LockGuard<SharedSpinLock> lock( DescriptorLock );
				old_desc_data = std::move( DescriptorData );
				if( DescriptorData )
				{
					MY_ASSERT( false );
					DescriptorData.reset();
				}
			}

			if( !old_desc_data )
			{
				return;
			}

			{
				// Закрываем старый дескриптор
				LockGuard<SharedSpinLock> lock( old_desc_data->Lock );
				int old_fd = old_desc_data->Fd;
				old_desc_data->Fd = -1;
				CloseDescriptor( old_fd, err );

				coro_queues[ 0 ] = old_desc_data->ReadQueue.Release();
				coro_queues[ 1 ] = old_desc_data->WriteQueue.Release();
				coro_queues[ 2 ] = old_desc_data->ReadOobQueue.Release();
			}

			EpWaitStruct *ptr = nullptr;
			for( auto &waiters : coro_queues )
			{
				while( waiters && !err )
				{
					ptr = waiters.Pop();
					MY_ASSERT( ptr != nullptr );
					ptr->WasCancelled = true;
					PostToSrv( ptr->CoroRef );
				} // while( waiters )
			} // for( auto &waiters : coro_queues )
		} // void BasicDescriptor::Close( Error &err )

		void BasicDescriptor::Cancel( Error &err )
		{
			err = Error();

			std::shared_ptr<DescriptorStruct> desc_data;
			{
				SharedLockGuard<SharedSpinLock> lock( DescriptorLock );

				if( !DescriptorData )
				{
					return;
				}

				desc_data = DescriptorData;
			}
			MY_ASSERT( desc_data );

			LockFree::ForwardList<EpWaitStruct*>::Unsafe coro_queues[ 3 ];
			{
				LockGuard<SharedSpinLock> lock( desc_data->Lock );
				coro_queues[ 0 ] = desc_data->ReadQueue.Release();
				coro_queues[ 1 ] = desc_data->WriteQueue.Release();
				coro_queues[ 2 ] = desc_data->ReadOobQueue.Release();

				if( desc_data->Fd != -1 )
				{
					epoll_event ev_data;
					ev_data.data.ptr = nullptr;
					ev_data.events = 0;
					for( uint8_t t = 1; !err && ( t < 4 ); ++t )
					{
						if( epoll_ctl( SrvRef.EpollFds[ t ], EPOLL_CTL_MOD, DescriptorData->Fd, &ev_data ) != 0 )
						{
							MY_ASSERT( false );
							err = GetLastSystemError();
						}
					}
				} // if( DescriptorData->Fd != -1 )
			} // LockGuard<SharedSpinLock> lock( desc_data->Lock );

			EpWaitStruct *ptr = nullptr;
			for( auto &waiters : coro_queues )
			{
				while( waiters && !err )
				{
					ptr = waiters.Pop();
					MY_ASSERT( ptr != nullptr );
					ptr->WasCancelled = true;
					PostToSrv( ptr->CoroRef );
				} // while( waiters )
			} // for( auto &waiters : coro_queues )
		} // void BasicDescriptor::Cancel( Error &err )

		bool BasicDescriptor::IsOpen() const
		{
			SharedLockGuard<SharedSpinLock> lock( DescriptorLock );
			MY_ASSERT( !( DescriptorData && ( DescriptorData->Fd == -1 ) ) );
			return ( bool ) DescriptorData;
		}
	} // namespace CoroService
} // namespace Bicycle
