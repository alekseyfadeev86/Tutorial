#include "CoroSrv/Inet.hpp"
#include "sys/types.h"
#include "sys/socket.h"

namespace Bicycle
{
	namespace CoroService
	{
		string Ip4Addr::GetIp( Error &err ) const
		{
			err.Reset();

			char res[ 21 ];
			if( inet_ntop( AF_INET, ( void* ) &Addr.sin_addr, res, 21 ) == nullptr )
			{
				// Ошибка
				err = GetLastSystemError();
			}

			return res;
		} // string Ip4Addr::GetIp( Error &err ) const

		void Ip4Addr::SetIp( const string &ip, Error &err )
		{
			err.Reset();

			int res = inet_pton( AF_INET, ip.c_str(), ( void* ) &Addr.sin_addr );
			if( res == 1 )
			{
				// Успех
				return;
			}

			if( res == 0 )
			{
				// Неверное значение ip
				err = GetSystemErrorByCode( EINVAL );
			}
			else
			{
				err =  GetLastSystemError();
			}
		} // void Ip4Addr::SetIp( const string &ip, Error &err ) const

		//-------------------------------------------------------------------------------

		int BasicSocket::OpenNewDescriptor( Error &err )
		{
			int new_fd = CreateNewSocket();
			err = new_fd == -1 ? GetLastSystemError() : Error();
			return new_fd;
		}

		void BasicSocket::CloseDescriptor( int fd, Error &err )
		{
			BasicDescriptor::CloseDescriptor( fd, err );
		}

		BasicSocket::BasicSocket( uint64_t read_timeout_microsec,
		                          uint64_t write_timeout_microsec ):
		    BasicDescriptor( read_timeout_microsec,
		                     write_timeout_microsec )
		{}

		BasicSocket::~BasicSocket()
		{
			Error err;
			Close( err );
			MY_ASSERT( !err );
		}

		void BasicSocket::Bind( const Ip4Addr &addr, Error &err )
		{
			MY_ASSERT( Data );
			SharedLockGuard<SharedSpinLock> lock( Lock );
			int bind_res = bind( Fd,
			                     ( struct sockaddr* ) &addr.Addr,
			                     ( socklen_t ) sizeof( addr.Addr ) );
			err = bind_res != 0 ? GetLastSystemError() : Error();
		} // void BasicSocket::Bind( const Ip4Addr &addr, Error &err )

		//-------------------------------------------------------------------------------

		int UdpSocket::CreateNewSocket()
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

			IoTaskType task = [ & ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				auto i_res = sendto( fd, ( const void* ) data.first, data.second,
				                     MSG_NOSIGNAL | MSG_DONTWAIT,
				                     ( const struct sockaddr* ) &addr.Addr,
				                     ( socklen_t ) sizeof( addr.Addr ) );

				if( i_res >= 0 )
				{
					// Успех
					res = ( size_t ) i_res;
					return ErrorCodes::Success;
				}

				res = 0;
				err_code_t err_code = errno;
				errno = 0;
				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Write );

			return res;
		}

		size_t UdpSocket::RecvFrom( const BufferType &data, Ip4Addr &addr, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Некуда записывать полученное
				return 0;
			}

			size_t res = 0;

			IoTaskType task = [ & ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				socklen_t sz = sizeof( addr.Addr );
				auto i_res = recvfrom( fd, ( void* ) data.first, data.second,
				                       MSG_NOSIGNAL | MSG_DONTWAIT,
				                       ( struct sockaddr* ) &addr.Addr, &sz );

				if( i_res >= 0 )
				{
					// Успех
					res = ( size_t ) i_res;
					return ErrorCodes::Success;
				}

				res = 0;
				err_code_t err_code = errno;
				errno = 0;
				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Read );

			return res;
		}

		//-------------------------------------------------------------------------------

		int TcpSocket::CreateNewSocket()
		{
			return socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		}

		void TcpConnection::Connect( const Ip4Addr &addr, Error &err )
		{
			bool was_called = false;
			IoTaskType task = [ this, &addr, &was_called ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				err_code_t err_code = ErrorCodes::Success;
				
				if( !was_called )
				{
					// Вызываем connect для асинхронного подключения
					if( connect( fd, ( const struct sockaddr* ) &( addr.Addr ),
					             ( socklen_t ) sizeof( addr.Addr ) ) != 0 )
					{
						err_code = errno == EINPROGRESS ? EAGAIN : errno;
						errno = 0;
					}
	
					// Если операция не была прервана сигналом, снова дёргать не надо
					was_called = err_code != EINTR;
					
					if( err_code != EAGAIN )
					{
						// Операция завершена (успешно или нет - другой вопрос)
						return err_code;
					}
				}
				
				// Функция connect была вызвана - проверяем состояние сокета
				// (connect выполняется асинхронно, т.к. fd - неблокирующий,
				// и может возникнуть ситуация, когда в процессе подключения
				// возникает ошибка, и epoll_wait реагирует на неё, (пробуждается),
				// как будто сокет готов к записи)
				socklen_t sz = sizeof( err_code );
				if( getsockopt( fd, SOL_SOCKET, SO_ERROR, ( void* ) &err_code, &sz ) == -1 )
				{
					err_code = GetLastSystemError();
				}
				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Write );
		}

		size_t TcpConnection::Send( const ConstBufferType &data, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Нечего отправлять
				return 0;
			}

			size_t res = 0;

			IoTaskType task = [ & ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				auto i_res = send( fd, ( const void* ) data.first, data.second,
				                   MSG_NOSIGNAL | MSG_DONTWAIT );

				if( i_res >= 0 )
				{
					// Успех
					res = ( size_t ) i_res;
					return ErrorCodes::Success;
				}

				res = 0;
				err_code_t err_code = errno;
				errno = 0;
				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Write );

			return res;
		}

		size_t TcpConnection::Recv( const BufferType &data, Error &err )
		{
			if( ( data.first == nullptr ) || ( data.second == 0 ) )
			{
				// Некуда записывать полученное
				return 0;
			}

			size_t res = 0;

			IoTaskType task = [ & ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				auto i_res = recv( fd, ( void* ) data.first, data.second,
				                   MSG_NOSIGNAL | MSG_DONTWAIT );

				if( i_res >= 0 )
				{
					// Успех
					res = ( size_t ) i_res;
					return ErrorCodes::Success;
				}

				res = 0;
				err_code_t err_code = errno;
				errno = 0;
				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Read );

			return res;
		}

		void TcpAcceptor::Listen( uint16_t backlog, Error &err )
		{
			MY_ASSERT( Data );
			SharedLockGuard<SharedSpinLock> lock( Lock );
			err = listen( Fd, ( int ) backlog ) == 0 ? Error() : GetLastSystemError();
		}

		void TcpAcceptor::Accept( TcpConnection &conn, Ip4Addr &addr, Error &err )
		{
			int new_conn = -1;
			IoTaskType task = [ this, &addr, &new_conn ]( int fd ) -> err_code_t
			{
				MY_ASSERT( fd != -1 );
				err_code_t err_code = ErrorCodes::Success;

				socklen_t sz = sizeof( addr.Addr );
				new_conn = accept( fd, ( sockaddr* ) &( addr.Addr ), &sz );

				if( new_conn == -1 )
				{
					err_code = errno;
					errno = 0;
				}

				return err_code;
			};
			err = ExecuteIoTask( task, IoTaskTypeEnum::Read );

			if( !err )
			{
				// Новое соединение успешно принято
				MY_ASSERT( new_conn != -1 );

				MY_ASSERT( conn.Data );
				LockGuard<SharedSpinLock> lock( conn.Lock );
				if( conn.Fd == -1 )
				{
					err = InitAndRegisterNewDescriptor( new_conn, *conn.Data );
					MY_ASSERT( conn.Data );
					if( err )
					{
						// Ошибка привязки нового соединения к epoll-у
						Error e;
						CloseDescriptor( new_conn, e );
						conn.Fd = -1;
						return;
					}
					
					conn.Fd = new_conn;
				}
				else
				{
					// conn уже подключён
					Error e;
					CloseDescriptor( new_conn, e );
					MY_ASSERT( !e );
					err = GetSystemErrorByCode( EISCONN );
				}
			} // if( !err )
		} // void TcpAcceptor::Accept( TcpConnection &conn, Error &err )
	} // namespace CoroService
} // namespace Bicycle
