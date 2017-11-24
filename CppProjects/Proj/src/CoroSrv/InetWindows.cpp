#include <Ws2tcpip.h>
#include <MSWSock.h>
#include "CoroSrv/Inet.hpp"

namespace Bicycle
{
	namespace CoroService
	{
		inline err_code_t GetLastSockErrorCode()
		{
			auto err_code = WSAGetLastError();
			if( err_code != ERROR_SUCCESS )
			{
				WSASetLastError( ERROR_SUCCESS );
			}
			return err_code;
		}

		inline Error GetLastSockError()
		{
			return GetSystemErrorByCode( GetLastSockErrorCode() );
		} // inline Error GetLastSockError()

		string Ip4Addr::GetIp( Error &err ) const
		{
			err = Error();
			char res[ 21 ];
			if( inet_ntop( AF_INET, ( void* ) &Addr.sin_addr, res, 21 ) == nullptr )
			{
				// Ошибка
				err = GetLastSockError();
			}

			return res;
		} // string Ip4Addr::GetIp( Error &err ) const

		void Ip4Addr::SetIp( const string &ip, Error &err )
		{
			INT res = inet_pton( AF_INET, ip.c_str(), ( void* ) &Addr.sin_addr );
			if( res == 1 )
			{
				// Успех
				return;
			}

			err = Error();
			if( res == 0 )
			{
				// Неверное значение ip
				err = GetSystemErrorByCode( ERROR_INVALID_PARAMETER );
			}
			else
			{
				err = GetLastSockError();
			}
		} // void Ip4Addr::SetIp( const string &ip, Error &err ) const

		//-------------------------------------------------------------------------------

		HANDLE BasicSocket::OpenNewDescriptor( Error &err )
		{
			SOCKET new_sock = CreateNewSocket();
			if( new_sock == INVALID_SOCKET )
			{
				// Ошибка
				err = GetLastSockError();
				return INVALID_HANDLE_VALUE;
			}

			err = Error();
			return ( HANDLE ) new_sock;
		}

		void BasicSocket::CloseDescriptor( HANDLE fd, Error &err )
		{
			err = closesocket( ( SOCKET ) fd ) == 0 ? Error() : GetLastSockError();
		}

		BasicSocket::BasicSocket()
		{
			WSADATA wsa_data;
			ThrowIfNeed( GetSystemErrorByCode( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) ) );
		}

		BasicSocket::~BasicSocket()
		{
			Error err;
			Close( err );
			MY_ASSERT( !err );

			WSACleanup();
			MY_ASSERT( WSAGetLastError() == ErrorCodes::Success );
		}

		void BasicSocket::Bind( const Ip4Addr &addr, Error &err )
		{
			LockGuard<SharedSpinLock> lock( FdLock );
			SOCKET sock = Fd != INVALID_HANDLE_VALUE ? ( SOCKET ) Fd : INVALID_SOCKET;

			if( sock == INVALID_SOCKET )
			{
				// Сокет не открыт
				err.Code = ErrorCodes::NotOpen;
				err.What = "Socket is not open";
				return;
			}

			int bind_res = bind( sock,
								 ( const struct sockaddr* ) &addr.Addr,
								 sizeof( addr.Addr ) );
			err = bind_res != 0 ? GetLastSockError() : Error();
		}

		//-------------------------------------------------------------------------------

		SOCKET UdpSocket::CreateNewSocket()
		{
			return socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		}

		size_t UdpSocket::SendTo( const ConstBufferType &data, const Ip4Addr &addr, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Нечего отправлять
				return 0;
			}

			size_t res = 0;

			WSABUF wsa_buf;
			wsa_buf.buf = ( CHAR* ) data.first;
			wsa_buf.len = data.second;
			int sz = sizeof( addr.Addr );
			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );
				auto i_res = WSASendTo( ( SOCKET ) fd, &wsa_buf, 1, nullptr, 0,
				                        ( const sockaddr* ) &addr.Addr, sz,
				                        ( LPWSAOVERLAPPED ) &task_struct, nullptr );
				return i_res != 0 ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			err = ExecuteIoTask( task, res );

			return res;
		}

		size_t UdpSocket::RecvFrom( const BufferType &data, Ip4Addr &addr, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Нечего отправлять
				return 0;
			}

			size_t res = 0;

			WSABUF wsa_buf;
			wsa_buf.buf = ( CHAR* ) data.first;
			wsa_buf.len = data.second;
			int sz = sizeof( addr.Addr );
			DWORD flags = 0;

			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );
				auto i_res = WSARecvFrom( ( SOCKET ) fd, &wsa_buf, 1, nullptr, &flags,
				                          ( sockaddr* ) &addr.Addr, &sz,
				                          ( LPWSAOVERLAPPED ) &task_struct, nullptr );

				return i_res != 0 ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			err = ExecuteIoTask( task, res );

			return res;
		}

		//-------------------------------------------------------------------------------

		SOCKET TcpSocket::CreateNewSocket()
		{
			return socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		}

		void TcpConnection::Connect( const Ip4Addr &addr, Error &err )
		{
			DWORD bytes_rcvd = 0;
			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );

				// Получаем указатель на функцию ConnectEx
				GUID GuidConnectEx = WSAID_CONNECTEX;
				LPFN_CONNECTEX lpfnConnectEx = NULL;
				auto i_res = WSAIoctl( ( SOCKET ) fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
				                       ( LPVOID ) &GuidConnectEx, sizeof( GuidConnectEx ),
				                       ( LPVOID ) &lpfnConnectEx, sizeof( lpfnConnectEx ), 
				                       &bytes_rcvd, NULL, NULL );
				if( i_res != 0 )
				{
					return GetLastSockErrorCode();
				}
				MY_ASSERT( lpfnConnectEx != nullptr );

				// Выполняем асинхронное подключение
				i_res = lpfnConnectEx( ( SOCKET ) fd, ( const sockaddr* ) &( addr.Addr ),
				                       sizeof( addr.Addr ), ( PVOID ) nullptr, 0,
				                       ( LPDWORD ) nullptr, ( LPOVERLAPPED ) &task_struct );
				return i_res == FALSE ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			size_t fake_sz = 0;
			err = ExecuteIoTask( task, fake_sz );
		} // void TcpConnection::Connect( const Ip4Addr &addr, Error &err )

		size_t TcpConnection::Send( const ConstBufferType &data, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Нечего отправлять
				return 0;
			}

			size_t res = 0;

			WSABUF wsa_buf;
			wsa_buf.buf = ( CHAR* ) data.first;
			wsa_buf.len = data.second;
			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );
				auto i_res = WSASend( ( SOCKET ) fd, &wsa_buf, 1, nullptr, 0,
				                      ( LPWSAOVERLAPPED ) &task_struct, nullptr );

				return i_res != 0 ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			err = ExecuteIoTask( task, res );

			return res;
		}

		size_t TcpConnection::Recv( const BufferType &data, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Нечего отправлять
				return 0;
			}

			size_t res = 0;

			WSABUF wsa_buf;
			wsa_buf.buf = ( CHAR* ) data.first;
			wsa_buf.len = data.second;
			DWORD flags = 0;

			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );
				auto i_res = WSARecv( ( SOCKET ) fd, &wsa_buf, 1, nullptr, &flags,
				                      ( LPWSAOVERLAPPED ) &task_struct, nullptr );

				return i_res != 0 ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			err = ExecuteIoTask( task, res );

			return res;
		}

		void TcpAcceptor::Listen( uint16_t backlog, Error &err )
		{
			SharedLockGuard<SharedSpinLock> lock( FdLock );
			err = listen( ( SOCKET ) Fd, ( int ) backlog ) == 0 ? Error() : GetLastSockError();
		}

		void TcpAcceptor::Accept( TcpConnection &conn, Ip4Addr &addr, Error &err )
		{
			SOCKET new_sock = CreateNewSocket();
			if( new_sock == INVALID_SOCKET )
			{
				err = GetLastSockError();
				return;
			}

			err = RegisterNewDescriptor( ( HANDLE ) new_sock );
			if( err )
			{
				return;
			}

			static const size_t AddrSz = sizeof( sockaddr_in ) + 16;
			static const size_t BufSize = 2*AddrSz;
			char buf[ BufSize ] = { 0 };
			DWORD bytes_rcvd = 0;

			IoTaskType task = [ & ]( HANDLE fd, IocpStruct &task_struct ) -> err_code_t
			{
				MY_ASSERT( fd != INVALID_HANDLE_VALUE );
				MY_ASSERT( ( SOCKET ) fd != INVALID_SOCKET );

				// Получаем указатель на функцию AcceptEx
				LPFN_ACCEPTEX lpfnAcceptEx = NULL;
				GUID GuidAcceptEx = WSAID_ACCEPTEX;
				auto i_res = WSAIoctl( ( SOCKET ) fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
				                       ( LPVOID ) &GuidAcceptEx, sizeof( GuidAcceptEx ),
				                       ( LPVOID ) &lpfnAcceptEx, sizeof( lpfnAcceptEx ),
				                       &bytes_rcvd, NULL, NULL );
				if( i_res != 0 )
				{
					return GetLastSockErrorCode();
				}
				MY_ASSERT( lpfnAcceptEx != nullptr );

				i_res = lpfnAcceptEx( ( SOCKET ) fd, new_sock, buf, 0,
				                       AddrSz, AddrSz, &bytes_rcvd,
				                       ( LPOVERLAPPED ) &task_struct );
				return i_res == FALSE ? GetLastSockErrorCode() : ErrorCodes::Success;
			};
			size_t fake_sz = 0;
			err = ExecuteIoTask( task, fake_sz );

			if( !err )
			{
				LockGuard<SharedSpinLock> lock( conn.FdLock );
				if( conn.Fd == INVALID_HANDLE_VALUE )
				{
					conn.Fd = ( HANDLE ) new_sock;
				}
				else
				{
					err.Code = ErrorCodes::AlreadyOpen;
					err.What = "Socket already opened";
				}
			}
		} // void TcpAcceptor::Accept( TcpConnection &conn, Ip4Addr &addr, Error &err )
	} // namespace CoroService
} // namespace Bicycle

