#include "Server.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

inline void ThrowIfNeed()
{
	int err = errno;
	if( err != 0 )
	{
		throw std::runtime_error( strerror( err ) );
	}
}

InfoKeeper::InfoKeeper()
{}

InfoKeeper::~InfoKeeper()
{}

void InfoKeeper::Add( DescriptorInfo *ptr )
{
	std::lock_guard<std::mutex> lock( Mutex );
	Data.insert( ptr );
}

void InfoKeeper::Remove( DescriptorInfo *ptr )
{
	std::lock_guard<std::mutex> lock( Mutex );
	Data.erase( ptr );
}

std::set<DescriptorInfo*> InfoKeeper::Release()
{
	std::set<DescriptorInfo*> result;

	{
		std::lock_guard<std::mutex> lock( Mutex );
		result = std::move( Data );
	}

	return result;
}

Server::Server( const string &ip,
				uint16_t port ): EpollDescriptor( -1 )
{
	int ep_fd = -1;
	int sock_fd = -1;
	int pipe_fds[ 2 ] = { -1, -1 };

	try
	{
		ep_fd = epoll_create1( EPOLL_CLOEXEC );
		ThrowIfNeed();

		pipe( pipe_fds );
		ThrowIfNeed();

		sock_fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		ThrowIfNeed();

		sockaddr_in addr;
		inet_aton( ip.c_str(), &addr.sin_addr );
		addr.sin_family = AF_INET;
		addr.sin_port = htons( port );
		bind( sock_fd, ( const sockaddr* ) &addr, sizeof( addr ) );
		ThrowIfNeed();

		listen( sock_fd, 63 );
		ThrowIfNeed();

		fcntl( sock_fd, F_SETFL, O_NONBLOCK );
		ThrowIfNeed();

		epoll_event ev;
		ev.data.ptr = ( void* ) &AcceptorInfo;
		ev.events = EPOLLIN | EPOLLET;
		epoll_ctl( ep_fd, EPOLL_CTL_ADD, sock_fd, &ev );
		ThrowIfNeed();

		epoll_event ev_pipe;
		ev_pipe.data.ptr = 0;
		ev_pipe.events = EPOLLIN;
		epoll_ctl( ep_fd, EPOLL_CTL_ADD, pipe_fds[ 0 ], &ev_pipe );
		ThrowIfNeed();

		EpollDescriptor = ep_fd;
		AcceptorInfo.Descriptor = sock_fd;
		Pipe[ 0 ] = pipe_fds[ 0 ];
		Pipe[ 1 ] = pipe_fds[ 1 ];
	}
	catch( ... )
	{
		close( pipe_fds[ 0 ] );
		close( pipe_fds[ 1 ] );
		close( sock_fd );
		close( ep_fd );
		throw;
	}
}

Server::~Server()
{
	Close();
	close( Pipe[ 0 ] );
}

void Server::Execute()
{
	while( 1 )
	{
		epoll_event ev;
		int rs = epoll_wait( EpollDescriptor, &ev, 1, -1 );
		if( rs > 0 )
		{
			printf( "Дождались\n" );
			fflush( stdout );

			if( ev.data.ptr == 0 )
			{
				// Всё, закругляемся
				break;
			}
			else if( ev.data.ptr == &AcceptorInfo )
			{
				if( ev.events & EPOLLERR )
				{
#ifndef _DEBUG
#error "обработать"
#endif
				}
				else if( ev.events & EPOLLIN )
				{
					// Входящее соединение
					printf( "Acceptor\n" );
					fflush( stdout );
					while( 1 )
					{
						int new_conn = -1;
						sockaddr_in addr;
						socklen_t addr_sz = sizeof( addr );
						new_conn = accept( AcceptorInfo.Descriptor, ( sockaddr* ) &addr, &addr_sz );
						if( new_conn != -1 )
						{
							printf( "Accepted\n" );
							fflush( stdout );

							// Принято входящее соединение
							while( 1 )
							{
								fcntl( new_conn, F_SETFL, O_NONBLOCK );
								if( errno != EINTR )
								{
									break;
								}
							}

							if( errno != 0 )
							{
								// Не удалось перевести сокет в неблокирующий режим
								printf( "fcntl %s\n", strerror( errno ) );
								fflush( stdout );
								close( new_conn );
								continue;
							}

							auto new_ptr = new DescriptorInfo();
							new_ptr->Descriptor = new_conn;
							epoll_event ev;
							ev.data.ptr = ( void* ) new_ptr;
							ev.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
							if( epoll_ctl( EpollDescriptor, EPOLL_CTL_ADD, new_conn, &ev ) == 0 )
							{
								// Добавляем указатель на структуру в хранилище
								ConnectionsInfo.Add( new_ptr );
							}
							else
							{
								// Какая-то ошибка при привязке сокета к epoll
								printf( "epoll_ctl( new_conn )\n" );
								fflush( stdout );
								close( new_conn );
								delete new_ptr;
							}
						}
						else
						{
							// Приняли все входящие соединения
							break;
						}
					} // while( 1 )
				}
			} // if( ev.data.ptr == &AcceptorInfo )
			else
			{
				printf( "Conn\n" );
				fflush( stdout );

				DescriptorInfo *ptr = ( DescriptorInfo* ) ev.data.ptr;
				uint32_t epollin_flag = ptr->HangedUp ? 0 : ( uint32_t ) EPOLLIN;

				// Активность на одном из соединений
				if( ev.events & ( EPOLLERR | EPOLLHUP ) )
				{
					// Ошибка на сокете
					close( ptr->Descriptor );
					ConnectionsInfo.Remove( ptr );
					delete ptr;
					printf( "EPOLLERR | EPOLLHUP\n" );
					fflush( stdout );
					continue;
				}
				else if( ev.events & EPOLLRDHUP )
				{
					// Соединение было закрыто для записи на том конце
					epollin_flag = 0;
					ptr->HangedUp = true;
				}

				if( ev.events & EPOLLIN )
				{
					// Появились данные для чтения
					int read_rs = -1;
					do
					{
						char buf[ 1025 ] = { 0 };
						read_rs = recv( ptr->Descriptor, buf, 1024, 0 );
						if( read_rs > 0 )
						{
							ptr->DataToSend += ( string ) buf;
						}
					}
					while( read_rs > 0 );
				}

				if( ( ev.events & EPOLLOUT ) && !ptr->DataToSend.empty() )
				{
					// Соединение готово принять данные, и есть, что отправлять
					int send_rs = -1;
					size_t sended_bytes = 0;
					size_t data_sz = ptr->DataToSend.length();

					do
					{
						send_rs = send( ptr->Descriptor,
										ptr->DataToSend.c_str() + sended_bytes,
										data_sz - sended_bytes, 0 );
						if( send_rs > 0 )
						{
							sended_bytes += send_rs;
						}
					}
					while( ( send_rs > 0 ) && ( data_sz > sended_bytes ) );
					ptr->DataToSend.erase( 0, sended_bytes );
				}

				if( ptr->HangedUp && ptr->DataToSend.empty() )
				{
					// Соединение больше ничего не пришлёт и отправлять ему нечего - закрываем
					DescriptorInfo *ptr = ( DescriptorInfo* ) ev.data.ptr;
					close( ptr->Descriptor );
					ConnectionsInfo.Remove( ptr );
					delete ptr;
					continue;
				}

				// Перерегистрируем сокет в epoll-е
				ev.events = epollin_flag |
							EPOLLONESHOT |
							EPOLLRDHUP;

				if( !ptr->DataToSend.empty() )
				{
					ev.events |= EPOLLOUT;
				}

				if( epoll_ctl( EpollDescriptor, EPOLL_CTL_MOD, ptr->Descriptor, &ev ) != 0 )
				{
					// Какая-то ошибка при привязке сокета к epoll
					printf( "erpoll_ctl 2: %s\n", strerror( errno ) );
					fflush( stdout );
					DescriptorInfo *ptr = ( DescriptorInfo* ) ev.data.ptr;
					close( ptr->Descriptor );
					ConnectionsInfo.Remove( ptr );
					delete ptr;
				}
			}
		}
		else if( errno != EINTR )
		{
#ifndef _DEBUG
#error "? что делать ?"
#endif
			printf( "Execute error: %s\n", strerror( errno ) );
			break;
		}
	}
}

void Server::Close()
{
	char c = 123;
	write( Pipe[ 1 ], &c, 1 );
	close( Pipe[ 1 ] );
	close( AcceptorInfo.Descriptor );
	close( EpollDescriptor );

	auto ptrs = ConnectionsInfo.Release();
	for( DescriptorInfo *ptr : ptrs )
	{
		if( ptr )
		{
			close( ptr->Descriptor );
			delete ptr;
		}
	}
}
