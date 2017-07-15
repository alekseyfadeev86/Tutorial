#include "CoroSrv/Inet.hpp"

#ifndef _WIN32
#include <string.h> // Для memset
#endif

namespace Bicycle
{
	namespace CoroService
	{
		Ip4Addr::Ip4Addr()
		{
			memset( &Addr, 0, sizeof( Addr ) );
			Addr.sin_family = AF_INET;
		}

		string Ip4Addr::GetIp() const
		{
			Error err;
			string res( GetIp( err ) );
			if( err )
			{
				throw Exception( err.Code, err.What.c_str() );
			}

			return res;
		}

		uint16_t Ip4Addr::GetPortNum() const
		{
			return ntohs( Addr.sin_port );
		}

		void Ip4Addr::SetIp( const string &ip )
		{
			Error err;
			SetIp( ip, err );
			if( err )
			{
				throw Exception( err.Code, err.What.c_str() );
			}
		}

		void Ip4Addr::SetPortNum( uint16_t port )
		{
			Addr.sin_port = htons( port );
		}

		//-------------------------------------------------------------------------------

		void BasicSocket::Bind( const Ip4Addr &addr )
		{
			Error err;
			Bind( addr, err );
			ThrowIfNeed( err );
		}

		size_t UdpSocket::SendTo( const ConstBufferType &data, const Ip4Addr &addr )
		{
			Error err;
			size_t res = SendTo( data, addr, err );
			ThrowIfNeed( err );
			return res;
		}

		size_t UdpSocket::RecvFrom( const BufferType &data, Ip4Addr &addr )
		{
			Error err;
			size_t res = RecvFrom( data, addr, err );
			ThrowIfNeed( err );
			return res;
		}

		//-------------------------------------------------------------------------------

		TcpSocket::TcpSocket() {}

		void TcpConnection::Connect( const Ip4Addr &addr )
		{
			Error err;
			Connect( addr, err );
			ThrowIfNeed( err );
		}

		size_t TcpConnection::Send( const ConstBufferType &data )
		{
			Error err;
			size_t res = Send( data, err );
			ThrowIfNeed( err );
			return res;
		}

		size_t TcpConnection::Recv( const BufferType &data )
		{
			Error err;
			size_t res = Recv( data, err );
			ThrowIfNeed( err );
			return res;
		}

		void TcpAcceptor::Listen( uint16_t backlog )
		{
			Error err;
			Listen( backlog, err );
			ThrowIfNeed( err );
		}

		void TcpAcceptor::Accept( TcpConnection &conn, Ip4Addr &addr )
		{
			Error err;
			Accept( conn, addr, err );
			ThrowIfNeed( err );
		}
	} // namespace CoroService
} // namespace Bicycle
