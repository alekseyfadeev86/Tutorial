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

		AbstractEpollWorker::~AbstractEpollWorker()
		{}

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
					AbstractEpollWorker *worker_ptr = ( AbstractEpollWorker* ) events_data[ ev_num ].data.ptr;
					if( worker_ptr == nullptr )
					{
						// Событие на PostPipe[ 0 ]: считываем байт,
						// чтобы epoll перестал срабатывать на канал
						WorkPosted();
						
						continue;
					} // if( ptr == nullptr )
					
					// События готовности на одном из дескрипторов: обрабатываем
					auto coros = worker_ptr->Work( events_data[ ev_num ].events );
					
					Coroutine *coro_ptr = nullptr;
					while( coros )
					{
						coro_ptr = coros.Pop();
						MY_ASSERT( coro_ptr != nullptr );
						
						if( coros )
						{
							Post( coro_ptr );
						}
						else
						{
							bool res = coro_ptr->SwitchTo();
							MY_ASSERT( res );
							
							// Выполняем задачу, "оставленную" дочерней сопрограммой
							ExecLeftTasks();
						}
					}
				} // for( uint8_t ev_num = 0, evs_count = res; ev_num < evs_count; ++t )
				
				// Обновляем эпоху
				DeleteQueue.UpdateEpoch( epoch );
				
				// Удаляем объекты из очереди
				DeleteQueue.ClearIfNeed();
			} // while( CoroCount.load() > 0 )
		} // void Service::Execute()

		//-------------------------------------------------------------------------------

	} // namespace CoroService
} // namespace Bicycle
