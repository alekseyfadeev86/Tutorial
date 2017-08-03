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
		const uint32_t EpollWorkMask = 0x1;
		const uint32_t TaskWorkMask = 0x2;
		const uint32_t DefEventMask = EPOLLET | EPOLLRDHUP;

		inline void CheckOperationSuccess( int res )
		{
			if( res != 0 )
			{
				ThrowIfNeed();
				MY_ASSERT( false );
				throw Exception( ErrorCodes::UnknownError, "Unknown error" );
			}
		}
		
		void Service::WorkPosted()
		{
			uint8_t coro_list_num = 0;
			Error err;
			int res = -1;
			
			do
			{
				res = read( PostPipe[ 0 ], &coro_list_num, 1 );
				if( res < 1 )
				{
					err = GetLastSystemError();
					if( err.Code == EAGAIN )
					{
						// Нет готовых сопрограмм (нас опередили)
						return;
					}
				} // if( res < 1 )

				MY_ASSERT( res <= 1 );
				MY_ASSERT( res != 0 );
				MY_ASSERT( ( res != -1 ) || ( err.Code == EINTR ) ); // Прервано сигналом
			}
			while( res < 1 );
			MY_ASSERT( coro_list_num < 8 );

			auto coros_to_exec = CoroutinesToExecute[ coro_list_num ].Release();
			MY_ASSERT( coros_to_exec );
			size_t coros_count = coros_to_exec.Reverse();
			MY_ASSERT( coros_count > 0 );

			Coroutine *coro_ptr = nullptr;
			while( coros_to_exec )
			{
				coro_ptr = coros_to_exec.Pop();
				if( coro_ptr != nullptr )
				{
					// Переключаемся на сопрограмму
					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );
					
					// Выполняем задачи, "оставленные" дочерней сопрограммой
					ExecLeftTasks();
				}
				else if( CoroCount.load() == 0 )
				{
					// Был извлечён нулевой указатель на сопрограмму - означает завершение цикла
					// Добавляем указатель снова, чтобы другие потоки тоже получили
					// уведомление о необходимости завершить работу, и выходим
					MY_ASSERT( !coros_to_exec );
					Post( nullptr );
				}
			} // while( coros_to_exec )
		} // void Service::WorkPosted()
		
		void Service::WorkEpoll( EpWaitListWithFlag &coros_list, uint32_t evs_mask )
		{
			coros_list.second.clear();
			EpWaitList::Unsafe coros = coros_list.first.Release();
			if( !coros )
			{
				return;
			}
			
			// Запоминаем первый элемент списка
			EpWaitStruct *ep_wait_ptr = coros.Pop();
			MY_ASSERT( ep_wait_ptr != nullptr );

			// А все остальные помещаем в Post
			while( coros )
			{
				auto ptr = coros.Pop();
				MY_ASSERT( ptr != nullptr );

				// Запоминаем события epoll-а, добавляем указатель на сопрограмму в очередь
				ptr->LastEpollEvents = evs_mask;
				Post( &ptr->CoroRef );
			}

			// Запоминаем события epoll-а, переходим в сопрограмму
			MY_ASSERT( ep_wait_ptr != nullptr );
			ep_wait_ptr->LastEpollEvents = evs_mask;
			bool coro_switch_res = ep_wait_ptr->CoroRef.SwitchTo();
			MY_ASSERT( coro_switch_res );

			// Выполняем задачу, "оставленную" дочерней сопрограммой
			ExecLeftTasks();
		} // void Service::WorkEpoll( EpWaitList &coros_list, uint32_t evs_mask )

		void Service::Initialize()
		{
			// Создаём анонимный неблокирующий канал для добавления в очередь готовых к исполнению задач
			CheckOperationSuccess( pipe2( PostPipe, O_NONBLOCK ) );
			MY_ASSERT( PostPipe[ 0 ] != -1 );
			MY_ASSERT( PostPipe[ 1 ] != -1 );

			// Создаём объект epoll
			EpollFd = epoll_create1( EPOLL_CLOEXEC );
			ThrowIfNeed();
			if( EpollFd == -1 )
			{
				MY_ASSERT( false );
				throw Exception( ErrorCodes::UnknownError, "Unknown error" );
			}

			// Привязка "читающего конца" анонимного канала к epoll-у
			epoll_event ev_data;
			ev_data.data.ptr = nullptr;
			ev_data.events = EPOLLIN;
			CheckOperationSuccess( epoll_ctl( EpollFd, EPOLL_CTL_ADD, PostPipe[ 0 ], &ev_data ) );
		} // void Service::Initialize()

		void Service::Close()
		{
			close( PostPipe[ 0 ] );
			close( PostPipe[ 1 ] );
			close( EpollFd );
		}

		void Service::Post( Coroutine *coro_ptr )
		{
			const uint8_t list_num = ( CoroListNum++ ) % 8;
			MY_ASSERT( list_num < 8 );
			LockFree::ForwardList<Coroutine*> &coro_list_ref = CoroutinesToExecute[ list_num ];

			if( !coro_list_ref.Push( coro_ptr ) )
			{
				// Список был не пустой - выходим
				return;
			}

			// Список был пуст - записываем 1 байт в канал, чтобы
			// epoll среагировал
			int res = -1;

			do
			{
				res = write( PostPipe[ 1 ], &list_num, 1 );
				MY_ASSERT( res <= 1 );
				MY_ASSERT( res != 0 );
				MY_ASSERT( ( res != -1 ) || ( errno == EINTR ) ); // Прервано сигналом
			}
			while( res < 1 );
		} // void Service::Post( Coroutine *coro_ptr )

		void Service::Execute()
		{
			static const uint8_t EventArraySize = 0x20;
			epoll_event events_data[ EventArraySize ];
			
			// Захватываем "эпоху" (пока она захвачена - 100% никто
			// не удалит структуры, на которые указывают элементы events_data)
			auto epoch = DeleteQueue.EpochAcquire();
			
			while( CoroCount.load() > 0 )
			{
				// Удаляем указатели на закрытые дескрипторы из списка (если нужно)
				RemoveClosedDescriptors();

				size_t eps_sz = EventArraySize;
				uint64_t threads_num = WorkThreadsCount.load();
				if( threads_num > 1 )
				{
					size_t sz = CoroCount.load() / threads_num + 1;
					if( sz < EventArraySize )
					{
						eps_sz = sz;
					}
				}
				MY_ASSERT( eps_sz >= 1 );
				MY_ASSERT( eps_sz <= EventArraySize );

				int res = epoll_wait( EpollFd, events_data, eps_sz, -1 );
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
					// Такого быть не должно
					MY_ASSERT( false );
					continue;
				}
				MY_ASSERT( res <= EventArraySize );

				for( uint8_t ev_num = 0, evs_count = res; ev_num < evs_count; ++ev_num )
				{
					DescriptorStruct *ptr = ( DescriptorStruct* ) events_data[ ev_num ].data.ptr;
					if( ptr == nullptr )
					{
						// Событие на PostPipe[ 0 ]: считываем байт,
						// чтобы epoll перестал срабатывать на канал
						WorkPosted();
						
						continue;
					} // if( ptr == nullptr )
					
					// События готовности на одном из дескрипторов
					static const uint32_t ErrMask = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
					static const uint32_t TaskMask = EPOLLIN | EPOLLOUT | EPOLLPRI;

					uint32_t evs = events_data[ ev_num ].events;
					if( ( evs & ErrMask ) != 0 )
					{
						// Есть события ошибки - дёргаем все сопрограммы-"ждуны"
						// (ожидающие готовности на чтение, запись и чтение
						// внеполосных данных, пусть сами разбираются с ошибками)
						evs = TaskMask;
					}
					else
					{
						// Ошибок нет, отсекаем только нужные нам события
						evs &= TaskMask;
					}
					MY_ASSERT( ( evs & ErrMask ) == 0 );

					if( ( evs & EPOLLIN ) != 0 )
					{
						WorkEpoll( ptr->ReadQueue, evs );
					}

					if( ( evs & EPOLLOUT ) != 0 )
					{
						WorkEpoll( ptr->WriteQueue, evs );
					}

					if( ( evs & EPOLLPRI ) != 0 )
					{
						WorkEpoll( ptr->ReadOobQueue, evs );
					}
				} // for( uint8_t ev_num = 0, evs_count = res; ev_num < evs_count; ++t )
				
				// Обновляем эпоху
				DeleteQueue.UpdateEpoch( epoch );
				
				// Удаляем объекты из очереди
				DeleteQueue.ClearIfNeed();
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
				Service *srv_ptr = &SrvRef;
				std::function<void()> epoll_task = [ &err, &ep_waiter, srv_ptr, &lock,
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
						srv_ptr->Post( &( ptr->CoroRef ) );
					}

					bool res = waiter_ptr->CoroRef.SwitchTo();
					MY_ASSERT( res );

					// Сюда попадаем уже после смены контекста - остаётся только уйти
					return;
				}; // std::function<void()> epoll_task = [ & ]

				// Переходим в основную сопрограмму и настраиваем epoll.
				// Сюда возвращаемся, когда дескриптор будет готов к работе
				// или закрыт, либо в случае ошибки
				SetPostTaskAndSwitchToMainCoro( &epoll_task );
				
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
