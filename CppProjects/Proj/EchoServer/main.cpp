#include "CoroService.hpp"
#include <iostream>
#include <thread>
#include <csignal>

using namespace Bicycle;

// Размер буфера ввода-вывода
const size_t BufferSize = 1024;

const size_t StackSize = BufferSize + 1024;

void udp_server( const CoroService::Ip4Addr &srv_addr )
{
	using namespace CoroService;
	std::cout << "Protocol: UDP" << std::endl;

	std::shared_ptr<UdpSocket> sock_ptr;
	try
	{
		sock_ptr.reset( new UdpSocket );
		sock_ptr->Open();
		sock_ptr->Bind( srv_addr );

		auto task = [ sock_ptr ]()
		{
			try
			{
				uint8_t buf[ BufferSize ];
				ConstBufferType send_buf( buf, BufferSize );
				BufferType read_buf( buf, BufferSize );
				Ip4Addr sender_addr;

				size_t send_res = 0;
				while( true )
				{
					// Чтение
					send_buf.second = sock_ptr->RecvFrom( read_buf, sender_addr );
					if( send_buf.second == 0 )
					{
						std::cerr << "0 bytes was received" << std::endl;
						break;
					}

					// Отправка прочитанного
					send_res = sock_ptr->SendTo( send_buf, sender_addr );
					if( send_res != send_buf.second )
					{
						std::cerr << send_buf.second << " bytes received, but ";
						std::cerr << send_res << "bytes were sended" << std::endl;
					}
				} // while( true )
			}
			catch( const std::exception &exc )
			{
				if( sock_ptr->IsOpen() )
				{
                                        std::cerr << "Data exchange error: " << exc.what() << std::endl;
				}
			}
		}; // auto task = [ sock_ptr ]()

		for( uint8_t t = 0; t < 10; ++t )
		{
			ThrowIfNeed( Go( task, StackSize ) );
		}
	}
	catch( const std::exception &exc )
	{
		std::cerr << "Start error: " << exc.what() << std::endl;

		if( sock_ptr )
		{
			Error err;
			sock_ptr->Close( err );
		}
	}
} // void udp_server( const CoroService::Ip4Addr &srv_addr )

void tcp_server( const CoroService::Ip4Addr &srv_addr )
{
	using namespace CoroService;
	std::cout << "Protocol: TCP" << std::endl;

	try
	{
		Ip4Addr conn_addr;
		TcpAcceptor acceptor;
		acceptor.Open();
		acceptor.Bind( srv_addr );
		acceptor.Listen( 64 );

		auto conn_task = []( std::shared_ptr<TcpConnection> conn_ptr )
		{
                        //std::cout << "New connection was accepted" << std::endl;

			try
			{
				uint8_t buf[ BufferSize ];
				BufferType read_buf( buf, BufferSize );
				ConstBufferType send_buf;
				size_t send_sz = 0;

				while( true )
				{
					// Считываем данные
					send_buf.second = conn_ptr->Recv( read_buf );
					if( send_buf.second == 0 )
					{
						// Соединение было закрыто на том конце
                                                //std::cout << "Connection was closed by remote side" << std::endl;
						conn_ptr->Close();
						break;
					}

					// Отпарвляем полученное
					send_buf.first = buf;
					while( send_buf.second > 0 )
					{
						send_sz = conn_ptr->Send( send_buf );
						if( send_sz == 0 )
						{
							std::cerr << "Connection error: 0 bytes were sended" << std::endl;
						}

						send_buf.first += send_sz;
						send_buf.second -= send_sz;
					} // while( send_buf.second > 0 )
				} // while( true )
			}
			catch( const std::exception &exc )
			{
				if( conn_ptr->IsOpen() )
				{
					std::cerr << "Connection error: " << exc.what() << std::endl;
				}
			}
		}; // auto conn_task = []( std::shared_ptr<TcpConnection> conn_ptr )

		while( true )
		{
			std::shared_ptr<TcpConnection> conn_ptr( new TcpConnection );
			acceptor.Accept( *conn_ptr, conn_addr );
			ThrowIfNeed( Go( [ conn_ptr, conn_task ]() { conn_task( conn_ptr ); } ) );
		}
	}
	catch( const std::exception &exc )
	{
		std::cerr << "Acception cycle error: " << exc.what() << std::endl;
	}
} // void tcp_server( CoroService::Ip4Addr &srv_addr )

std::atomic<Bicycle::CoroService::Service*> service_ptr( nullptr );
void terminate( int )
{
	std::cout << "Server stopping..." << std::endl;
	auto ptr = service_ptr.exchange( nullptr );
	if( ptr != nullptr )
	{
		ptr->Stop();
	}
}

int main( int argc, char **argv )
{
	// Параметры по умолчанию
	string host = "127.0.0.1";
	uint16_t port_num = 44000;
	uint8_t threads_num = 4;
	bool is_tcp = true;

	// Разбираем параметры командной строки
	for( int t = 1; t < ( argc - 1 ); t += 2 )
	{
		string arg_name = argv[ t ];
		const char *arg_val = argv[ t + 1 ];

		if( ( arg_name == "-h" ) || ( arg_name == "--host" ) )
		{
			host = arg_val;
		}
		else if( ( arg_name == "-p" ) || ( arg_name == "--port" ) )
		{
			int64_t val = std::atoi( arg_val );
			if( ( val > 0 ) && ( val <= 0xFFFF ) )
			{
				port_num = ( uint16_t ) val;
			}
		}
		else if( ( arg_name == "-t" ) || ( arg_name == "--threads" ) )
		{
			int64_t val = std::atoi( arg_val );
			if( ( val > 0 ) && ( val <= 0xFF ) )
			{
				threads_num = ( uint8_t ) val;
			}
		}
		else if( arg_name == "--proto" )
		{
			is_tcp = ( string ) arg_val == "tcp";
		}
		else
		{
			--t;
		}
	} // for( int t = 1; t < ( argc - 1 ); t += 2 )

	try
	{
		// Формируем адрес
		CoroService::Ip4Addr addr;
		addr.SetIp( host );
		addr.SetPortNum( port_num );

		std::function<void()> task( [ &addr, is_tcp ]()
		{
			if( is_tcp )
			{
				tcp_server( addr );
			}
			else
			{
				udp_server( addr );
			}
		});

		// Создаём сервис сопрограмм
		CoroService::Service service;
		if( !service.Restart() )
		{
			std::cerr << "Unknown error while service start" << std::endl;
			return 1;
		}
		service_ptr.store( &service );
		signal( SIGINT, &terminate );

		// Добавляем сопрограмму в сервис
		ThrowIfNeed( service.AddCoro( task, StackSize ) );

		// Запускаем сервер
		std::vector<std::thread> threads;
		threads.reserve( threads_num );

		std::cout << "Server started on " << addr.GetIp() << ":" << addr.GetPortNum() << std::endl;
		std::cout << "Working threads number: " << ( uint16_t ) threads_num << std::endl;
		auto thread_task = [ &service ]()
		{
			try
			{
				service.Run();
			}
			catch( const std::exception &exc )
			{
				std::cerr << "Error while run: " << exc.what() << std::endl;
			}
		};

		for( uint8_t t = 0; t < threads_num; ++t )
		{
			threads.emplace_back( thread_task );
		}

		// Ожидаем завершения рабочих потоков
		for( auto &th : threads )
		{
			th.join();
		}

		std::cout << "Server stopped" << std::endl;
	}
	catch( const std::exception &exc )
	{
		std::cerr << exc.what() << std::endl;
	}

	return 0;
}
#include <memory>
#include <stdio.h>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <assert.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

void print_err( const char *err_msg )
{
	int err_code = errno;
	if( err_code != 0 )
	{
		const char *def_err_msg = "Ошибка";
		if( err_msg == nullptr )
		{
			err_msg = def_err_msg;
		}
		const char *s_err = strerror( errno );
		printf( "%s %i: %s\n", err_msg, errno, s_err );
		fflush( stdout );
		std::terminate();
	}
}

void check( int rs, const char *err_msg )
{
	if( rs != 0 )
	{
		print_err( err_msg );
	}
}

int main1( int argc, char *argv[] )
{
	const char *s_ip = "127.0.0.1";
	uint16_t port_num = 46000;

	if( argc > 1 )
	{
		s_ip = argv[ 1 ];
		if( argc > 2 )
		{
			port_num = atoi( argv[ 2 ] );
		}
	}

	int ep_fd = epoll_create1( EPOLL_CLOEXEC );
	assert( ep_fd != -1 );

	int accept_fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	assert( accept_fd != -1 );
	if( accept_fd == -1 )
	{
		print_err( "Ошибка accept-а" );
	}

	int fl = fcntl( accept_fd, F_GETFL );
	if( ( fl == -1 ) || ( fcntl( accept_fd, F_SETFL, fl | O_NONBLOCK ) != 0 ) )
	{
		print_err( "Ошибка fcntl" );
	}

	const bool reconfig_epoll = false;
	const int64_t EpollEvs = EPOLLIN | ( reconfig_epoll ? EPOLLONESHOT : EPOLLET );
	epoll_event ev_data;
	ev_data.data.fd = -1;
	ev_data.events = EpollEvs;
	int res = epoll_ctl( ep_fd, EPOLL_CTL_ADD, accept_fd, &ev_data );
	assert( res == 0 );
	check( res, "Ошибка epollctl (accept_fd)" );

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons( port_num );
	inet_pton( AF_INET, s_ip, ( void* ) &addr.sin_addr );
	res = bind( accept_fd, ( struct sockaddr* ) &addr, sizeof( addr ) );
	check( res, "Ошибка bind" );

	res = listen( accept_fd, 63 );
	check( res, "Ошибка listen" );
	std::atomic<uint64_t> N( 0 );

	auto thread_task = [ accept_fd, ep_fd, &N ]()
	{
		static const size_t buf_sz = 100;
		static const size_t ep_sz = 0x10;

		char buf[ buf_sz ] = { 0 };
		epoll_event evs[ ep_sz ];

		sockaddr_in conn_addr;
		socklen_t conn_addr_sz = sizeof( addr );

		Bicycle::Coro::Coroutine main_coro;
		Bicycle::Coro::Coroutine sec_coro( [ &main_coro ]() -> Bicycle::Coro::Coroutine*
		{
			while( 1 )
			{
				Bicycle::Coro::GetCurrentCoro();
				main_coro.SwitchTo();
			}
			return &main_coro;
		}, 10*1024 );

		int res;

		while( 1 )
		{
			int epoll_res = -1;
			do
			{
				epoll_res = epoll_wait( ep_fd, evs, ep_sz, -1 );
			}
			while( ( epoll_res == -1 ) && ( errno == EINTR ) );

			if( epoll_res < 0 )
			{
				print_err( "Ошибка epoll_wait" );
			}

			for( int ep_n = 0; ep_n < epoll_res; ++ep_n )
			{
				delete [] ( new uint64_t[ 4 ] );

				const int fd = evs[ ep_n ].data.fd;
				//const uint32_t ev_mask = evs[ ep_n ].events;

				if( fd == -1 )
				{
					// Входящие подключения
					{
						Bicycle::Coro::Coroutine tmp_coro( [ &main_coro ]() -> Bicycle::Coro::Coroutine*
						{
							Bicycle::Coro::GetCurrentCoro();
							return &main_coro;
						}, 10*1024 );
						tmp_coro.SwitchTo();
					}

					while( true )
					{
						int new_conn = accept( accept_fd, ( sockaddr* ) &conn_addr, &conn_addr_sz );
						if( new_conn < 0 )
						{
							// Ошибка принятия нового соединения
							if( errno != EAGAIN )
							{
								print_err( "Ошибка accept" );
							}
							break;
						}

						++N;
						epoll_event new_conn_ev;
						new_conn_ev.data.fd = new_conn;
						new_conn_ev.events = EpollEvs;
						res = epoll_ctl( ep_fd, EPOLL_CTL_ADD, new_conn_ev.data.fd, &new_conn_ev );
						check( res, "Ошибка epoll_ctl( new_conn )" );
					} // while( true )

					if( reconfig_epoll )
					{
						epoll_event accept_ev_data;
						accept_ev_data.data.fd = -1;
						accept_ev_data.events = EpollEvs;
						int res = epoll_ctl( ep_fd, EPOLL_CTL_MOD, accept_fd, &accept_ev_data );
						check( res, "Ошибка epollctl (accept_fd, повторный)" );
					}
				}
				else
				{
					// Входящие данные
					sec_coro.SwitchTo();
					bool config_epoll = true;
					while( config_epoll )
					{
						// Считываем данные
						int recv_res = recv( fd, ( void* ) buf, buf_sz, MSG_NOSIGNAL | MSG_DONTWAIT );
						if( recv_res <= 0 )
						{
							// Косяк
							if( recv_res == 0 )
							{
								// Соединение закрыто на той стороне
								close( fd );
								config_epoll = false;
								--N;
							}
							else if( errno != EAGAIN )
							{
								// EAGAIN - Тупо считали всё, что было (а тут другая ошибка)
								printf( "Косяк считывания %i: %s\n", errno, strerror( errno ) );
								close( fd );
								config_epoll = false;
								--N;
							}

							break;
						} // if( recv_res <= 0 )

						// Отправляем данные
						for( size_t send_sz = 0, total_sz = recv_res; send_sz < total_sz; )
						{
							int send_res = send( fd, ( const void* ) ( buf + send_sz ), total_sz - send_sz, MSG_NOSIGNAL );
							if( send_res <= 0 )
							{
								printf( "Косяк записи %i: %s\n", errno, strerror( errno ) );
								config_epoll = false;
								close( fd );
								--N;
								break;
							} // if( send_sz <= 0 )

							send_sz += ( size_t ) send_res;
						} // for( size_t send_sz = 0, total_sz = recv_res; send_sz < total_sz; )
					} // while( config_epoll )

					if( config_epoll && reconfig_epoll )
					{
						// Настраиваем epoll
						epoll_event ev_data;
						ev_data.data.fd = fd;
						ev_data.events = EpollEvs;
						int res = epoll_ctl( ep_fd, EPOLL_CTL_MOD, ev_data.data.fd, &ev_data );
						check( res, "Ошибка epollctl (fd, повторный)" );
					}
				} // if( fd == -1 ) ... else
			} // for( int ep_n = 0; ep_n < epoll_res; ++ep_n )
		} // while( 1 )
	}; // auto thread_task = [ accept_fd, ep_fd ]()

	const size_t threads_num = 4;
	std::thread threads[ threads_num ];
	for( size_t t = 0; t < threads_num; ++t )
	{
		threads[ t ] = std::thread( thread_task );
	}

	for( size_t t = 0; t < threads_num; ++t )
	{
		threads[ t ].join();
	}

	return 0;
}

/*
 * ASIO echo-server code
#include <boost/asio.hpp>
#include <thread>
#include <iostream>

struct conn_struct
{
        boost::asio::ip::tcp::socket sock;
        uint8_t read_buffer[ 1024 ];
        size_t offset_sz;
        size_t size_to_send;

        conn_struct( boost::asio::io_service &srv ): sock( srv ) {}
};

void read_by_conn( std::shared_ptr<conn_struct> conn_ptr );
void send_by_conn( std::shared_ptr<conn_struct> conn_ptr )
{
        if( conn_ptr )
        {
                if( conn_ptr->size_to_send > 0 )
                {
                        auto buf = boost::asio::buffer( conn_ptr->read_buffer + conn_ptr->offset_sz,
                                                        conn_ptr->size_to_send );
                        conn_ptr->sock.async_write_some( buf, [ conn_ptr ]( const boost::system::error_code &ec, size_t sz )
                        {
                                if( ec || ( sz == 0 ) )
                                {
                                        std::cerr << "Send error: " << ( ec ? ec.message() : "0 bytes sended" ) << std::endl;
                                        boost::system::error_code e;
                                        conn_ptr->sock.close( e );
                                        return;
                                }

                                conn_ptr->offset_sz += sz;
                                conn_ptr->size_to_send -= sz;
                                send_by_conn( conn_ptr );
                        });
                }
                else
                {
                        read_by_conn( conn_ptr );
                }
        }
} // void send_by_conn( std::shared_ptr<conn_struct> conn_ptr )

void read_by_conn( std::shared_ptr<conn_struct> conn_ptr )
{
        if( conn_ptr )
        {
                auto buf = boost::asio::buffer( conn_ptr->read_buffer, sizeof( conn_ptr->read_buffer ) );
                conn_ptr->sock.async_read_some( buf, [ conn_ptr ]( const boost::system::error_code &ec, size_t sz )
                {
                        if( ec )
                        {
                                //std::cerr << "Read error: " << ec.message() << std::endl;
                                boost::system::error_code e;
                                conn_ptr->sock.close( e );
                                return;
                        }
                        else if( sz == 0 )
                        {
                                boost::system::error_code e;
                                conn_ptr->sock.close( e );
                                return;
                        }

                        conn_ptr->offset_sz = 0;
                        conn_ptr->size_to_send = sz;
                        send_by_conn( conn_ptr );
                });
        }
} // void read_by_conn( std::shared_ptr<conn_struct> conn_ptr )

void handle_accept( boost::asio::ip::tcp::acceptor &acceptor,
                    std::shared_ptr<conn_struct> conn_ptr,
                    const boost::system::error_code &err )
{
        if( err )
        {
                std::cerr << "Accept error: " << err.message() << std::endl;
                return;
        }

        std::shared_ptr<conn_struct> new_conn_ptr( new conn_struct( acceptor.get_io_service() ) );
        acceptor.async_accept( new_conn_ptr->sock,
                               [ new_conn_ptr, &acceptor ]( const boost::system::error_code &e )
        {
                handle_accept( acceptor, new_conn_ptr, e );
        });

        read_by_conn( conn_ptr );
} // void handle_accept

void echo_server( const std::string &host, uint16_t port )
{
        using namespace boost::asio;

        try
        {
                io_service service;
                ip::tcp::acceptor acceptor( service );
                auto addr = ip::address::from_string( "127.0.0.1" );
                ip::tcp::endpoint ep( addr, 45000 );
                acceptor.open( ep.protocol() );
                acceptor.bind( ep );
                acceptor.listen( 64 );
                handle_accept( acceptor, std::shared_ptr<conn_struct>(), boost::system::error_code() );

                std::thread threads[ 4 ];
                for( auto &th : threads )
                {
                        th = std::thread( [ &service ]{ service.run(); } );
                }

                for( auto &th : threads )
                {
                        th.join();
                }
        }
        catch( const std::exception &exc )
        {
                std::cout << exc.what() << std::endl;
        }

} // void echo_server( const string &host, uint16_t port )
*/
