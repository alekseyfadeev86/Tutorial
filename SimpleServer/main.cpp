#include <iostream>
#include "Server.h"

#ifdef _DEBUG
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

void ExitOnError()
{
	int err = errno;
	if( err != 0 )
	{
		printf( "Error %i: %s\n", err, strerror( err ) );
		fflush( stdout );
		exit( 1 );
	}
}

void PrintIfError()
{
	int err = errno;
	if( err != 0 )
	{
		printf( "Error %i: %s\n", err, strerror( err ) );
		fflush( stdout );
	}
}

#else
#error "убрать"
#endif

int main()
{
#ifdef _DEBUG
	if( 0 )
	{
	int ep_fd = epoll_create1( EPOLL_CLOEXEC );
	ExitOnError();

	int sock_fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	ExitOnError();

	sockaddr_in addr;
	inet_aton( "127.0.0.1", &addr.sin_addr );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( 46000 );
	connect( sock_fd, ( const sockaddr* ) &addr, sizeof( addr ) );
	ExitOnError();

	epoll_event ev;
	ev.data.u64 = 12345;
	ev.events = EPOLLIN | EPOLLET;
	epoll_ctl( ep_fd, EPOLL_CTL_ADD, sock_fd, &ev );
	ExitOnError();

	std::thread th_( [ & ]()
	{
		std::this_thread::sleep_for( std::chrono::seconds( 3 ) );
		shutdown( sock_fd, SHUT_RD );
		//close( ep_fd );
		printf( "закрыто\n" );
		fflush( stdout );
	});

	epoll_event evs[ 10 ];
	char buf[ 101 ] = { 0 };
	int res = epoll_wait( ep_fd, evs, 10, -1 );
	if( res > 0 )
	{
		bool chk = ( evs[ 0 ].events == EPOLLIN );
		res = recv( sock_fd, buf, 100, 0 );
		if( res > 0 )
		{
			printf( "received data: %s\n", buf );
			fflush( stdout );
		}
		else if( res == 0 )
		{
			printf( "conn closed\n" );
			fflush( stdout );
		}
		else
		{
			ExitOnError();
		}
	}
	else
	{
		PrintIfError();
	}

	/*recv( sock_fd, buf, 100, 0 );
	ExitOnError();
	printf( "received data: %s\n", buf );*/

	th_.join();
	close( sock_fd );
	close( ep_fd );
	return 0;
	} // if(...
#else
#error "убрать отладочный код"
#endif

	try
	{
		Server srv( "127.0.0.1", 45000 );
		auto th_num = std::thread::hardware_concurrency();
		if( th_num == 0 )
		{
			th_num = 4;
		}

		auto handler = [ &srv ]()
		{
			srv.Execute();
		};
		std::vector<std::thread> threads( th_num );
		for( auto &th : threads )
		{
			th = std::thread( handler );
		}

		std::this_thread::sleep_for( std::chrono::minutes( 10 ) );
		srv.Close();

		for( auto &th : threads )
		{
			if( th.joinable() )
			{
				th.join();
			}
		}
	}
	catch( const std::exception &exc )
	{
		std::cout << "Ошибка: " << exc.what() << std::endl;
	}

	return 0;
}

