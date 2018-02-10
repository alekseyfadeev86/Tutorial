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
		                                                   WasCancelled( false ) {}
		
		template <bool UseTimeout>
		struct ElemType
		{};
		
		template <typename T>
		Coroutine* ExecOrCancel( T elem, bool is_cancel );
		
		template <>
		struct ElemType<false>
		{
			typedef EpWaitStruct* Type;
		};
		template<>
		Coroutine* ExecOrCancel<ElemType<false>::Type>( ElemType<false>::Type elem, bool is_cancel )
		{
			if( elem == nullptr )
			{
				return nullptr;
			}
			else if( is_cancel )
			{
				elem->WasCancelled = true;
			}
			
			return &( elem->CoroRef );
		}
		
		template <>
		struct ElemType<true>
		{
			typedef std::weak_ptr<CoroKeeper> Type;
		};
		template<>
		Coroutine* ExecOrCancel<ElemType<true>::Type>( ElemType<true>::Type ptr, bool is_cancel )
		{
			auto elem = ptr.lock();
			return elem ? ( is_cancel ? elem->MarkCancel() : elem->Release() ) : nullptr;
		}
		
		template<bool ReadWithTimeout, bool WriteWithTimeout>
		struct EpollWorker: public DescriptorEpollWorker
		{
			private:
				template <typename T>
				struct Converter
				{
					static T* Conv( T *ptr ) noexcept
					{
						return ptr;
					}
				
					template <typename T2>
					static T* Conv( T2* ) noexcept
					{
						return nullptr;
					}
				};
				
				template <typename T>
				static void WorkList( std::pair<LockFree::ForwardList<T>, std::atomic_flag> &in,
				                      coro_list_t &out, bool cancel )
				{
					// Сбрасываем флаг, чтобы показать, что epoll сработал
					in.second.clear();
					
					typename LockFree::ForwardList<T>::Unsafe l = in.first.Release();
					Coroutine *coro_ptr = nullptr;
					while( l )
					{
						coro_ptr = ExecOrCancel<T>( l.Pop(), cancel );
						if( coro_ptr != nullptr )
						{
							out.Push( coro_ptr );
						}
					}
				}
				
			public:
				typedef typename ElemType<ReadWithTimeout>::Type  ReadElemType;
				typedef typename ElemType<WriteWithTimeout>::Type WriteElemType;
				typedef typename ElemType<ReadWithTimeout>::Type  ReadOobElemType;
					
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
						WorkList<ReadElemType>( ReadQueue, res, false );
					}

					if( ( events_mask & EPOLLOUT ) != 0 )
					{
						WorkList<WriteElemType>( WriteQueue, res, false );
					}

					if( ( events_mask & EPOLLPRI ) != 0 )
					{
						WorkList<ReadOobElemType>( ReadOobQueue, res, false );
					}
					
					return res;
				}
				
				virtual coro_list_t Cancel() override final
				{
					coro_list_t res;
					
					WorkList<ReadElemType>( ReadQueue, res, true );
					WorkList<WriteElemType>( WriteQueue, res, true );
					WorkList<ReadOobElemType>( ReadOobQueue, res, true );
					
					return res;
				}
				
				virtual std::pair<list_t*, list_ex_t*> GetListPtr( IoTaskTypeEnum task_type ) noexcept override final
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

		Error BasicDescriptor::InitAndRegisterNewDescriptor( int fd, const AbstractEpollWorker &worker_ref )
		{
			// Переводим дескриптор в неблокирующий режим
			int fl = fcntl( fd, F_GETFL );
			if( ( fl == -1 ) || ( fcntl( fd, F_SETFL, fl | O_NONBLOCK ) != 0 ) )
			{
				return GetLastSystemError();
			}

			// Привязываем дескриптор к epoll-ам
			epoll_event ev_data;
			ev_data.data.ptr = ( void* ) &worker_ref;
			ev_data.events = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLET | EPOLLRDHUP;

			if( epoll_ctl( SrvRef.EpollFd, EPOLL_CTL_ADD, fd, &ev_data ) != 0 )
			{
				return GetLastSystemError();
			}

			return Error();
		}
		
		inline Error GetTaskState( const EpWaitStruct &waiter,
		                           const std::shared_ptr<CoroKeeper> &coro_keeper_ptr )
		{
			const TaskState state = coro_keeper_ptr ? coro_keeper_ptr->GetState() : ( waiter.WasCancelled ? TaskState::Cancelled : TaskState::InProc );
			MY_ASSERT( state != TaskState::Worked );
			
			Error err;
				
			if( state == TaskState::Cancelled )
			{
				// Задача была отменена
				err.Code = ErrorCodes::OperationAborted;
				err.What = "Operation was aborted";
			}
			else if( state == TaskState::TimeoutExpired )
			{
				// Таймаут сработал
				err.Code = ErrorCodes::TimeoutExpired;
				err.What = "Timeout expired";
			}
			
			return err;
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
			
			const uint64_t timeout_microsec = task_type == IoTaskTypeEnum::Write ? WriteTimeoutMicrosec : ReadTimeoutMicrosec;
			if( timeout_microsec == 0 )
			{
				// Неблокирующая операция
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
					err_code = task( Fd );
				}
				while( err_code == EINTR );

				if( ( err_code == EAGAIN ) ||
				    ( err_code == EWOULDBLOCK ) )
				{
					// Дескриптор не готов к выполнению операции
					return Error( ErrorCodes::TimeoutExpired, "TimeoutExpired" );
				}
				
				// Операция завершена (успешно или нет)
				return GetSystemErrorByCode( err_code );
			} // if( timeout_microsec == 0 )

			// Data - константа, поэтому синхронизировать доступ к ней не надо
			// (хранимый указатель 100% не поменяется, будет помещён в очередь
			// на удаление только, когда соотв. BasicDescriptor будет удалён)
			MY_ASSERT( Data );
			
			const auto data_ptr = Data.get();
			MY_ASSERT( data_ptr != nullptr );
			
			// Получаем 2 указателя на список элементов.
			// Если first - ненулевой, то не используем таймер, иначе - используем
			// (из них всегда только один нулевой, не больше и не меньше)
			auto list_ptrs = data_ptr->GetListPtr( task_type );
			MY_ASSERT( ( list_ptrs.first == nullptr ) != ( list_ptrs.second == nullptr ) );
			
			// Указатель на текущую сопрограмму
			Coroutine* const cur_coro_ptr = GetCurrentCoro();
			if( cur_coro_ptr == nullptr )
			{
				MY_ASSERT( false );
				return Error( ErrorCodes::NotInsideSrvCoro,
				              "Incorrect function call: not inside service coroutine" );
			}
			
			EpWaitStruct waiter( *cur_coro_ptr );
			std::shared_ptr<CoroKeeper> coro_keeper_ptr;
			TimeTasksQueue::task_type timer_task_ptr;
			
			MY_ASSERT( ( list_ptrs.first  != nullptr ) == ( timeout_microsec == TimeoutInfinite ) );
			MY_ASSERT( ( list_ptrs.second != nullptr ) == ( timeout_microsec != TimeoutInfinite ) );
			MY_ASSERT( ( list_ptrs.second != nullptr ) == ( bool ) TimerQueuePtr );
			
			// Получаем функтор добавления ссылки на сопрограмму в очередь сервиса
			// (poster передаётся в задачу, выполняемую в основной сопрограмме)
			const auto poster = GetPoster();
			MY_ASSERT( poster );
			
			// Получаем указатель на таймер
			MY_ASSERT( timeout_microsec != 0 );

			// Необходимо храить указатель на таймер именно здесь, а не в coro_task-е,
			// чтобы он 100% был жив, пока задача не завершится, либо не будет отменена
			const auto timer_ptr = ( timeout_microsec != TimeoutInfinite ) ? ( TimerQueuePtr ? TimerQueuePtr : TimeTasksQueue::GetQueue() ) : std::shared_ptr<TimeTasksQueue>();
			MY_ASSERT( ( timeout_microsec != TimeoutInfinite ) == ( bool ) timer_ptr );
			
			if( list_ptrs.second != nullptr )
			{
				// DescriptorEpollWorker использует список CoroKeeper-ов
				// (возможно использование таймера):
				// инициализируем CoroKeeper
				coro_keeper_ptr.reset( new CoroKeeper( nullptr ) );
				std::weak_ptr<CoroKeeper> coro_wptr( coro_keeper_ptr );
				
				if( timer_ptr )
				{
					// Используется таймер: таймаут - не беконечность и не 0
					auto timer_task = [ coro_wptr, poster ]()
					{
						auto ptr = coro_wptr.lock();
						if( !ptr )
						{
							// Задача уже была выполнена,
							// соотв. элемент удалён
							return;
						}
						
						auto coro_ptr = ptr->MarkTimeout();
						if( coro_ptr )
						{
							// Задача отменена по таймауту
							poster( *coro_ptr );
						}
					};
					
					timer_task_ptr.reset( new CancellableTask( timer_task ) );
				} // timer_ptr
				
				MY_ASSERT( coro_keeper_ptr );
				MY_ASSERT( coro_keeper_ptr->GetState() == TaskState::InProc );
				MY_ASSERT( ( bool ) timer_task_ptr == ( bool ) timer_ptr );
				MY_ASSERT( !timer_task_ptr || !timer_task_ptr->IsCancelled() );
			} // if( list_ptrs.second != nullptr )
			
			// Ошибка завершения
			Error err;
			std::atomic_flag &flag_ref = ( list_ptrs.first != nullptr ) ? list_ptrs.first->second : list_ptrs.second->second;
			bool use_timer = ( bool ) timer_ptr;
			
			while( !err )
			{
				// Захватываем блокировку, чтобы никто не закрыл дескриптор
				SharedLocker<SharedSpinLock> lock( Lock, true );
				MY_ASSERT( lock );
				MY_ASSERT( lock.Locked() );
				if( Fd == -1 )
				{
					// Дескриптор не открыт
					return Error( ErrorCodes::NotOpen, "Descriptor is not open" );
				}
				MY_ASSERT( !coro_keeper_ptr || ( coro_keeper_ptr->GetState() != TaskState::Worked ) );
	
				int &fd_ref = Fd;
				
				// Пробуем выполнить задачу ввода-вывода
				do
				{
					// В случае прерывания сигналом, повторяем попытку
					flag_ref.test_and_set(); // Взводим флаг, что сработки epoll_wait-а не было
					err = GetSystemErrorByCode( task( fd_ref ) );
				}
				while( err.Code == EINTR );

				if( ( err.Code != EAGAIN ) &&
				    ( err.Code != EWOULDBLOCK ) )
				{
					// Операция завершена (успешно или нет - другой вопрос)
					break;
				}
				
				err.Reset();
				MY_ASSERT( !err );
				
				// Проверяем, не была ли задача отменена
				if( coro_keeper_ptr )
				{
					// Используется coro_keeper_ptr - waiter-пустышка
					MY_ASSERT( coro_keeper_ptr->Release() == nullptr );
					if( !coro_keeper_ptr->Reset( *cur_coro_ptr ) )
					{
						// Сработал таймер, либо операция была отменена
						err = GetTaskState( waiter, coro_keeper_ptr );
						
						// Задача помечена как выполненная, либо в процессе, если err == false
						MY_ASSERT( err );
					} // if( !coro_keeper_ptr->Reset( *cur_coro_ptr ) )
					
					MY_ASSERT( coro_keeper_ptr->GetState() == TaskState::InProc );
				} // if( coro_keeper_ptr )
				else
				{
					err = GetTaskState( waiter, coro_keeper_ptr );
				}
				
				if( err )
				{
					break;
				}
				
				// Дескриптор не готов к выполнению требуемой операции,
				// ожидаем готовности с помощью epoll-а
				std::function<void()> epoll_task = [ &err, &waiter, coro_keeper_ptr, list_ptrs, data_ptr,
				                                     poster, &lock, &fd_ref, timer_ptr, timer_task_ptr,
				                                     timeout_microsec, &flag_ref, use_timer ]
				{
					// Этот код выполняется из основной сопрограммы потока
					MY_ASSERT( lock );
					MY_ASSERT( lock.Locked() );
					MY_ASSERT( ( list_ptrs.first != nullptr ) != ( list_ptrs.second != nullptr ) );
					
					if( use_timer )
					{
						// Добавляем задачу в таймер
						MY_ASSERT( timer_ptr );
						MY_ASSERT( list_ptrs.second != nullptr );
						MY_ASSERT( coro_keeper_ptr );
						MY_ASSERT( timeout_microsec != 0 );
						MY_ASSERT( timeout_microsec != TimeoutInfinite );
						timer_ptr->ExecuteAfter( timer_task_ptr, std::chrono::microseconds( timeout_microsec ) );
					}

					AbstractEpollWorker::coro_list_t waiters;
					{
						SharedLocker<SharedSpinLock> local_lock( std::move( lock ) );
	
						MY_ASSERT( !lock );
						MY_ASSERT( local_lock );
						MY_ASSERT( local_lock.Locked() );
	
						err.Reset();
	
						// Добавляем элемент в очередь сопрограмм, ожидающих готовности дескриптора
						MY_ASSERT( !waiter.WasCancelled );
	
						if( !flag_ref.test_and_set() )
						{
							// Было срабатывание epoll_wait-а
							local_lock.Unlock();
							bool switch_res = waiter.CoroRef.SwitchTo();
							MY_ASSERT( switch_res );
							return;
						}
						
						bool is_first = false;
						if( list_ptrs.first != nullptr )
						{
							// Таймер не используется
							is_first = list_ptrs.first->first.Push( &waiter );
						}
						else
						{
							// Таймер используется
							MY_ASSERT( list_ptrs.second != nullptr );
							MY_ASSERT( coro_keeper_ptr );
							is_first = list_ptrs.second->first.Push( coro_keeper_ptr );
						}
						
						if( !is_first )
						{
							// Добавили соотв. элемент в нужный список, но он был уже не пуст - выходим
							return;
						}
	
						// !!! с этого момента нельзя обращаться к переменным из стека ExecuteIoTask !!!
						// (другой поток мог уже перейти на ту сопрограмму)
						// переменные, переданные сюда копированием, пользовать можно, в т.ч.,
						// flag_ref, которые ссылаются на поля BasicDescriptor-а, который 100% жив,
						// т.к. его блокировка не была отпущена
	
						// Проверяем флаг срабатываний epoll-а
						if( flag_ref.test_and_set() )
						{
							// Флаг был установлен, срабатываний epoll_wait-а не было - выходим
							return;
						}
						
						// Были срабатывания epoll_wait-а
						MY_ASSERT( data_ptr != nullptr );
						waiters = data_ptr->Work( EPOLLIN | EPOLLOUT | EPOLLPRI );
					} // SharedLocker<SharedSpinLock> local_lock( std::move( lock ) );
					
					if( !waiters )
					{
						// Список пуст: либо обработан по сработке epoll_wait-а,
						// либо при вызове Cancel или Close
						return;
					}

					Coroutine *coro_ptr = waiters.Pop();
					MY_ASSERT( coro_ptr != nullptr );

					Coroutine *ptr = nullptr;
					while( waiters )
					{
						ptr = waiters.Pop();
						MY_ASSERT( ptr != nullptr );
						poster( *ptr );
					}

					bool res = coro_ptr->SwitchTo();
					MY_ASSERT( res );

					// Сюда попадаем уже после смены контекста - остаётся только уйти
					return;
				}; // std::function<void()> epoll_task

				// Переходим в основную сопрограмму и настраиваем epoll.
				// Сюда возвращаемся, когда дескриптор будет готов к работе
				// или закрыт, либо в случае ошибки
				SetPostTaskAndSwitchToMainCoro( std::move( epoll_task ) );
				use_timer = false;
				
				MY_ASSERT( !lock );
				MY_ASSERT( !coro_keeper_ptr || ( coro_keeper_ptr->Release() == nullptr ) );
				MY_ASSERT( !coro_keeper_ptr || ( coro_keeper_ptr->GetState() != TaskState::Worked ) );
				
				if( !err )
				{
					// Проверяем, не была ли задача отменена
					err = GetTaskState( waiter, coro_keeper_ptr );
				}
				// TODO: ? обрабатывать EPOLLHUP и EPOLLRDHUP epoll_wait-а ?
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
			
			if( timer_task_ptr )
			{
				// Отменяем задачу таймера, если она ещё не была выполнена
				timer_task_ptr->Cancel();
				timer_task_ptr.reset();
			}

			return err;
		} // Error BasicDescriptor::ExecuteIoTask

		BasicDescriptor::desc_ptr_t BasicDescriptor::InitDataPtr( uint64_t rd_timeout, uint64_t wr_timeout )
		{
			DescriptorEpollWorker *ptr = nullptr;
			
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
			
			Service &srv_ref = SrvRef;
			deleter_t deleter = [ &srv_ref ]( DescriptorEpollWorker *ptr ){ srv_ref.DeleteQueue.Delete( ptr ); };
			return desc_ptr_t( ( DescriptorEpollWorker* ) ptr, deleter );
		}
		
		BasicDescriptor::BasicDescriptor( uint64_t read_timeout_microsec,
		                                  uint64_t write_timeout_microsec ):
		    AbstractCloser(),
		    ReadTimeoutMicrosec( read_timeout_microsec ),
		    WriteTimeoutMicrosec( write_timeout_microsec ),
		    Data( InitDataPtr( read_timeout_microsec, write_timeout_microsec ) ),
		    Fd( -1 ),
		    TimerQueuePtr( InitTimer( read_timeout_microsec, write_timeout_microsec ) )
		{
			// При удалении DescriptorStruct-а shared_ptr-ом
			// вместо собственно удаления структура будет помещена
			// в очередь на отложенное удаление
			MY_ASSERT( Data );
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

			err.Reset();

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

			MY_ASSERT( Data );
			err = InitAndRegisterNewDescriptor( new_fd, *Data );
			
			if( err )
			{
				Error e;
				CloseDescriptor( new_fd, e );
				new_fd = -1;
				return;
			}
			
			Fd = new_fd;
		} // void BasicDescriptor::Open( Error &err )

		void BasicDescriptor::Close( Error &err )
		{
			err.Reset();
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
				
				// Отменяем операции, поставленные в очередь
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
			err.Reset();

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
			SharedLockGuard<SharedSpinLock> lock( Lock );
			return Fd != -1;
		}
	} // namespace CoroService
} // namespace Bicycle
