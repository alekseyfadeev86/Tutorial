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
		EpWaitStruct::EpWaitStruct( Coroutine &coro_ref ): CoroRef( coro_ref ),
		                                                   LastEpollEvents( 0 ),
		                                                   WasCancelled( false ) {}
		
		Coroutine* EpWaitStruct::Execute( uint64_t epoll_events )
		{
			LastEpollEvents = epoll_events;
			return &CoroRef;
		}
		
		Coroutine* EpWaitStruct::Cancel()
		{
			WasCancelled = true;
			return &CoroRef;
		}
		
		bool EpWaitStruct::IsCancelled() const
		{
			return WasCancelled;
		}
		
		uint64_t EpWaitStruct::EpollEvents() const
		{
			return LastEpollEvents;
		}
		
		EpWaitStructEx::EpWaitStructEx( Coroutine &coro_ref ): EpWaitStruct( coro_ref ),
		                                                       WasExecuted( false ) {}
		
		Coroutine* EpWaitStructEx::Execute( uint64_t epoll_events )
		{
			return WasExecuted.exchange( true ) ? nullptr : EpWaitStruct::Execute( epoll_events );
		}
		
		Coroutine* EpWaitStructEx::Cancel()
		{
			return WasExecuted.exchange( true ) ? nullptr : EpWaitStruct::Cancel();
		}
		
		template <bool UseTimeout>
		struct ElemType
		{};
		
		template <typename T>
		Coroutine* ExecOrCancel( Type elem, uint64_t ep_evs, bool is_cancel );
		
		template <>
		struct ElemType<false>
		{
			typedef EpWaitStruct* Type;
		};
		template<>
		Coroutine* ExecOrCancel<ElemType<false>::Type>( ElemType<false>::Type elem, uint64_t ep_evs, bool is_cancel )
		{
			return ( elem != nullptr ) ? ( is_cancel ? elem->Cancel() : elem->Execute( ep_evs ) ) : nullptr;
		}
		
		template <>
		struct ElemType<true>
		{
			typedef std::weak_ptr<EpWaitStructEx> Type;
		};
		template<>
		Coroutine* ExecOrCancel<ElemType<true>::Type>( ElemType<true>::Type ptr, uint64_t ep_evs, bool is_cancel )
		{
			auto elem = ptr.lock();
			return elem ? ( is_cancel ? elem->Cancel() : elem->Execute( ep_evs ) ) : nullptr;
		}
		
		template<bool ReadWithTimeout, bool WriteWithTimeout>
		struct EpollWorker: public DescriptorEpollWorker
		{
			private:
				template <typename T>
				struct Converter
				{
					static T* Conv( T *ptr )
					{
						return ptr;
					}
				
					template <typename T2>
					static T* Conv( T2* )
					{
						return nullptr;
					}
				};
				
				template <typename T>
				static void WorkList( std::pair<LockFree::ForwardList<T>, std::atomic_flag> &in,
				                      coro_list_t &out, uint64_t ep_evs, bool cancel )
				{
					// Сбрасываем флаг, чтобы показать, что epoll сработал
					in.second.clear();
					
					LockFree::ForwardList<T>::Unsafe l = in.first.Release();
					Coroutine coro_ptr = nullptr;
					while( l )
					{
						coro_ptr = ExecOrCancel<T>( l.Pop(), ep_evs, cancel );
						if( coro_ptr != nullptr )
						{
							out.Push( coro_ptr );
						}
					}
				}
				
			public:
				typedef ElemType<ReadWithTimeout>::Type  ReadElemType;
				typedef ElemType<WriteWithTimeout>::Type WriteElemType;
				typedef ElemType<ReadWithTimeout>::Type  ReadOobElemType;
					
				std::pair<LockFree::ForwardList<ReadElemType>,    std::atomic_flag> ReadQueue;
				std::pair<LockFree::ForwardList<WriteElemType>,   std::atomic_flag> WriteQueue;
				std::pair<LockFree::ForwardList<ReadOobElemType>, std::atomic_flag> ReadOobQueue;
					
				virtual coro_list_t Work( uint32_t events_mask ) override final
				{
					coro_list_t res;
					
					// События готовности на одном из дескрипторов
					static const uint32_t ErrMask = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
					static const uint32_t TaskMask = EPOLLIN | EPOLLOUT | EPOLLPRI;

					if( ( events_mask & ErrMask ) != 0 )
					{
						// Есть события ошибки - дёргаем все сопрограммы-"ждуны"
						// (ожидающие готовности на чтение, запись и чтение
						// внеполосных данных, пусть сами разбираются с ошибками)
						events_mask = TaskMask;
					}
					else
					{
						// Ошибок нет, отсекаем только нужные нам события
						events_mask &= TaskMask;
					}
					MY_ASSERT( ( events_mask & ErrMask ) == 0 );

					if( ( events_mask & EPOLLIN ) != 0 )
					{
						WorkList<ReadElemType>( ReadQueue, res, events_mask, false );
					}

					if( ( events_mask & EPOLLOUT ) != 0 )
					{
						WorkList<WriteElemType>( WriteQueue, res, events_mask, false );
					}

					if( ( events_mask & EPOLLPRI ) != 0 )
					{
						WorkList<ReadOobElemType>( ReadOobQueue, res, events_mask, false );
					}
					
					return res;
				}
				
				virtual coro_list_t Cancel() override final
				{
					coro_list_t res;
					
					WorkList<ReadElemType>( ReadQueue, res, 0, true );
					WorkList<WriteElemType>( WriteQueue, res, 0, true );
					WorkList<ReadOobElemType>( ReadOobQueue, res, 0, true );
					
					return res;
				}
				
				virtual std::pair<list_t*, list_ex_t*> GetListPtr( IoTaskTypeEnum task_type ) override final
				{
					std::pair<list_t*, list_ex_t*> res( nullptr, nullptr );
					
					switch( task_type )
					{
						case IoTaskTypeEnum::Read:
							res.first  = Converter<list_t>::Conv( &ReadQueue );
							res.second = Converter<list_ex_t>::Conv( &ReadQueue );
							break;
							
						case IoTaskTypeEnum::Write:
							res.first  = Converter<list_t>::Conv( &WriteQueue );
							res.second = Converter<list_ex_t>::Conv( &WriteQueue );
							break;
							
						case IoTaskTypeEnum::ReadOob:
							res.first  = Converter<list_t>::Conv( &ReadOobQueue );
							res.second = Converter<list_ex_t>::Conv( &ReadOobQueue );
							break;
							
						default:
							MY_ASSERT( false );
							break;
					}
					
					MY_ASSERT( ( res.first == nullptr ) != ( res.second == nullptr ) );
					return res;
				}
		};
		
		//-----------------------------------------------------------------------------------------
		
		void BasicDescriptor::CloseDescriptor( int fd, Error &err )
		{
			err = close( fd ) != 0 ? GetLastSystemError() : Error();
		}

		Error BasicDescriptor::InitAndRegisterNewDescriptor( int fd, const AbstractEpollWorker &worker_ptr )
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
			
			const uint64_t timeout = task_type == IoTaskTypeEnum::Write ? WriteTimeoutMicrosec : ReadTimeoutMicrosec;
			if( timeout == 0 )
			{
				SharedLockGuard<SharedSpinLock> lock( Lock );

				if( Fd == -1 )
				{
					// Дескриптор не открыт
					return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
				}

				// Пробуем выполнить задачу ввода-вывода
				err_code_t err_code;
				do
				{
					// В случае прерывания сигналом, повторяем попытку
					err_code = task( DescriptorData->Fd );
				}
				while( err_code == EINTR );

#ifndef _DEBUG
#error "? вернуть специальную ошибку в случае, если дескриптор не готов ?"
				//if( ( err_code != EAGAIN ) && ( err_code != EWOULDBLOCK ) ){qaz}
#endif
				return GetSystemErrorByCode( err_code );
			} // if( timeout == 0 )

			// Получаем 2 указателя на список элементов.
			// Если first - ненулевой, то не используем таймер, иначе - используем
			// (из них всегда только один нулевой, не больше и не меньше)
			auto list_ptrs = Data->GetListPtr();
			MY_ASSERT( ( list_ptrs.first == nullptr ) != ( list_ptrs.second ) );
			
			// Указатель на текущую сопрограмму
			Coroutine *cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				MY_ASSERT( false );
				return Error( ErrorCodes::NotInsideSrvCoro,
				              "Incorrect function call: not inside service coroutine" );
			}
			
			EpWaitStruct waiter( *cur_coro_ptr );
			EpWaitStruct *waiter_ptr = nullptr;
			std::shared_ptr<EpWaitStructEx> waiter_ex_ptr;
			
			if( list_ptrs.first != nullptr )
			{
				MY_ASSERT( timeout == TimeoutInfinite );
				waiter_ptr = &waiter;
			}
			else
			{
				MY_ASSERT( timeout != TimeoutInfinite );
				MY_ASSERT( TimerQueuePtr );
			}
			
			// Получаем указатель на таймер
			MY_ASSERT( timeout != 0 );
			const auto timer_ptr = ( timeout != TimeoutInfinite ) ? ( TimerQueuePtr ? TimerQueuePtr : TimeTasksQueue::GetQueue() ) : std::shared_ptr<TimeTasksQueue>();
			MY_ASSERT( ( timeout != TimeoutInfinite ) == ( bool ) timer_ptr );
			
			MY_ASSERT( Data );
			SharedLocker<SharedSpinLock> lock( Lock, true );
			MY_ASSERT( lock );
			MY_ASSERT( lock.Locked() );
			if( Fd == -1 )
			{
				// Дескриптор не открыт
				return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
			}

			// Ошибка завершения
			Error err;
			int &fd_ref = Fd;
#error "TODO: переделать (использовать таймер или нет)"

			// Получаем функтор добавления ссылки на сопрограмму в очередь сервиса
			// (poster передаётся в задачу, выполняемую в основной сопрограмме)
			const auto poster = GetPoster();
			MY_ASSERT( poster );
#error "если использовать таймер - сформировать момент времени, когда он будет превышен, и проверять"
			while( !err )
			{
				// Пробуем выполнить задачу
				MY_ASSERT( ( waiter_ptr == nullptr ) != waiter_ex_ptr );
				MY_ASSERT( ( waiter_ptr == nullptr ) || !waiter_ptr->IsCancelled() );
				MY_ASSERT( !waiter_ex_ptr || !waiter_ex_ptr->IsCancelled() );

				if( !lock )
				{
					// Такое будет, если вышли на второй "виток" цикла
					lock = SharedLocker<SharedSpinLock>( Lock, true );

					if( fd_ref == -1 )
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
					err = GetSystemErrorByCode( task( fd_ref ) );
				}
				while( err.Code == EINTR );

				if( ( err.Code != EAGAIN ) &&
				    ( err.Code != EWOULDBLOCK ) )
				{
					// Операция завершена (успешно или нет - другой вопрос)
					break;
				}
				
				if( waiter_ptr == nullptr )
				{
					// EpWaitStructEx предполагает только однократное срабатывание,
					// поэтому, каждый раз нужно создавать новый
					waiter_ex_ptr.reset( new EpWaitStructEx( *cur_coro_ptr ) );
				}
				
				MY_ASSERT( ( waiter_ptr == nullptr ) == ( bool ) waiter_ex_ptr );

				// Дескриптор не готов к выполнению требуемой операции,
				// ожидаем готовности с помощью epoll-а
				std::function<void()> epoll_task = [ &err, waiter_ptr, waiter_ex_ptr, list_ptrs,
				                                     poster, &lock, &fd_ref, timer_ptr ]
				{
					// Этот код выполняется из основной сопрограммы потока
					MY_ASSERT( lock );
					MY_ASSERT( lock.Locked() );
					MY_ASSERT( desc_ptr->Fd != -1 );
					MY_ASSERT( queue_flag_ptr != nullptr );

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
	
						if( !queue_flag_ptr->second.test_and_set() )
						{
							// Было срабатывание epoll_wait-а
							local_lock.Unlock();
							bool switch_res = ep_waiter.CoroRef.SwitchTo();
							MY_ASSERT( switch_res );
							return;
						}
						
						if( !queue_flag_ptr->first.Push( &ep_waiter ) )
						{
							// Добавили ep_waiter в список, но он был уже не пуст - выходим
							return;
						}
	
						// !!! с этого момента нельзя обращаться к переменным из стека ExecuteIoTask !!!
						// (другой поток мог уже перейти на ту сопрограмму)
						// переменные, переданные сюда копированием, пользовать можно, в т.ч.,
						// fd_ref, которые ссылаются на поля BasicDescriptor-а, который 100% жив,
						// т.к. его блокировка не была отпущена
#error "взвести таймер (!!! только, если добавили первую задачу в список и таймаут - не бесконечность !!!)"
	
						// Проверяем флаг срабатываний epoll-а
						if( queue_flag_ptr->second.test_and_set() )
						{
							// Флаг был установлен, срабатываний epoll_wait-а не было - выходим
							return;
						}
	
						waiters = queue_flag_ptr->first.Release();
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

				if( ( ( waiter_ptr != nullptr ) && waiter_ptr->IsCancelled() ) ||
				    ( waiter_ex_ptr && waiter_ex_ptr->IsCancelled() ) )
				{
					// Задача была отменена
					err.Code = ErrorCodes::OperationAborted;
					err.What = "Operation was aborted";
				}
				else if( waiter_ex_ptr )
				{
					MY_ASSERT( waiter_ptr == nullptr );
#error "TODO: отменить задачу таймера (если не удастся - значит, он уже сработал)"
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

		BasicDescriptor::desc_ptr_t BasicDescriptor::InitDataPtr( uint64_t rd_timeout, uint64_t wr_timeout )
		{
			EpollWorker *ptr = nullptr;
			
			if( ( rd_timeout != 0 ) && ( rd_timeout != TimeoutInfinite ) )
			{
				// Используется таймаут на чтение
				if( ( wr_timeout != 0 ) && ( wr_timeout != TimeoutInfinite ) )
				{
					// Используются оба таймаута
					ptr = new EpollWorker<true, true>;
				}
				else
				{
					// Используется только таймаут на чтение
					ptr = new EpollWorker<true, false>;
				}
			}
			else
			{
				// Таймаут на чтение не используется
				if( ( wr_timeout != 0 ) && ( wr_timeout != TimeoutInfinite ) )
				{
					// Используется только таймаут на запись
					ptr = new EpollWorker<false, true>;
				}
				else
				{
					// Таймауты не используются
					ptr = new EpollWorker<false, false>;
				}
			}
			MY_ASSERT( ptr != nullptr );
			
			Service &srv_ref;
			deleter_t deleter = [ &srv_ref ]( DescriptorEpollWorker *ptr ){ SrvRef.DeleteQueue.Delete( ptr ); };
			return desc_ptr_t( ( DescriptorEpollWorker* ) ptr, deleter );
		}
		
		BasicDescriptor::BasicDescriptor( uint64_t read_timeout_microsec,
		                                  uint64_t write_timeout_microsec ):
		    AbstractCloser(),
		    ReadTimeoutMicrosec( read_timeout_microsec ),
		    WriteTimeoutMicrosec( write_timeout_microsec ),
		    Data( InitDataPtr( rd_timeout, wr_timeout ) ),
		    TimerQueuePtr( InitTimer( read_timeout_microsec, write_timeout_microsec ) )
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

			MY_ASSERT( Data );
			LockGuard<SharedSpinLock> lock( Lock );
			if( Fd != -1 )
			{
				// Дескриптор уже открыт
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
			MY_ASSERT( Data );
			MY_ASSERT( err || ( Fd == new_fd ) );
			if( err )
			{
				Error e;
				CloseDescriptor( Fd, e );
				Fd = -1;
				return;
			}
		} // void BasicDescriptor::Open( Error &err )

		void BasicDescriptor::Close( Error &err )
		{
			err = Error();
			LockFree::ForwardList<Coroutine*>::Unsafe coros;
			int old_fd = -1;

			MY_ASSERT( Data );
			{
				LockGuard<SharedSpinLock> lock( Lock );
				if( Fd == -1 )
				{
					// Дескриптор уже закрыт
					return;
				}
				
				// Отменяем операции, поставленные в очередт
				coros = Data->Cancel();
	
				// Закрываем старый дескриптор
				old_fd = Fd;
				Fd = -1;
			}
			
			MY_ASSERT( old_fd != -1 );
			CloseDescriptor( old_fd, err );

			Coroutine *ptr = nullptr;
			while( coros )
			{
				ptr = coros.Pop();
				MY_ASSERT( ptr != nullptr );
				PostToSrv( *ptr );
			}
		} // void BasicDescriptor::Close( Error &err )

		void BasicDescriptor::Cancel( Error &err )
		{
			err = Error();

			MY_ASSERT( Data );
			LockFree::ForwardList<Coroutine*>::Unsafe coros( Data->Cancel() );
			while( coros )
			{
				Coroutine *ptr = coros.Pop();
				MY_ASSERT( ptr != nullptr );
				PostToSrv( *ptr );
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
