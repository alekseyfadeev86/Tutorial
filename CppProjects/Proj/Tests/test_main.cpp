#include "Tests.hpp"

#ifdef _DEBUG
#ifdef _WIN32
#include <Ws2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include <thread>
#include <map>
#else
#error "убрать"
#endif

int main( int argc, char *argv[] )
{
#ifdef _DEBUG
	if( 0 )
	{
	std::multimap<int, char> mm;
	mm.insert( std::make_pair( 123, 'a' ) );
	mm.insert( std::make_pair( 125, 'b' ) );
	mm.insert( std::make_pair( 125, 'c' ) );
	mm.insert( std::make_pair( 126, 'd' ) );

	const int key = 125;
	for( auto iter = mm.begin(), end = mm.upper_bound( key ); iter != end; ++iter )
	{
		printf( "%i: %c\n", iter->first, iter->second );
	}

	return 0;

#ifdef _WIN32
	auto h_iocp = CreateIoCompletionPort( INVALID_HANDLE_VALUE, 0, 0, 0 );
	assert( h_iocp != NULL );

	WSADATA wsa_data;
	assert( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) == 0 );

	auto udp_sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	assert( udp_sock != INVALID_SOCKET );

	assert( CreateIoCompletionPort( ( HANDLE ) udp_sock, h_iocp, 123, 0 ) != 0 );

	struct sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	assert( inet_pton( AF_INET, "127.0.0.1", ( void* ) &addr.sin_addr ) == 1 );
	addr.sin_port = htons( 45123 );
	addr.sin_family = AF_INET;

	assert( bind( udp_sock, ( const sockaddr* ) &addr, sizeof( addr ) ) == 0 );

	char c_buf[ 11 ] = { 0 };
	WSABUF buf;
	buf.buf = c_buf;
	buf.len = 11;
	int sz = sizeof( addr );
	DWORD flags = 0;

	WSAOVERLAPPED ov;
	memset( ( void* ) &ov, 0, sizeof( ov ) );
	auto i_res = WSARecvFrom( udp_sock, &buf, 1, nullptr, &flags,
	                          ( sockaddr* ) &addr, &sz, &ov, nullptr );

	int err_code = 0;
	std::thread th( [ &err_code, &udp_sock, &ov ]()
	{
		std::this_thread::sleep_for( std::chrono::seconds( 60 ) );
		err_code = CancelIoEx( ( HANDLE ) udp_sock, &ov ) != 0 ? 0 : WSAGetLastError();
	});

	DWORD bytes_count = 0;
	ULONG_PTR comp_key = 0;
	LPOVERLAPPED pov = nullptr;
	BOOL res = GetQueuedCompletionStatus( h_iocp, &bytes_count, &comp_key, &pov, INFINITE );

	assert( ( pov == nullptr ) || ( pov == &ov ) );
	assert( ( comp_key == 0 ) || ( comp_key == 123 ) );

	DWORD err_code0 = 0;
	if( res == FALSE )
	{
		// err_code0 == ERROR_OPERATION_ABORTED, если операция была отменена
		err_code0 = GetLastError();
	}

	th.join();

	// ERROR_NOT_FOUND, если операция уже была выполнена
	assert( ( err_code == 0 ) || ( err_code == ERROR_NOT_FOUND ) );

	closesocket( udp_sock );
	WSACleanup();
	CloseHandle( h_iocp );
#else // #ifdef _WIN32
#endif
	} // if
//	return 1;
#else // #ifdef _DEBUG
#error "убрать"
#endif

	printf( "%s\n", "Start..." );
	fflush( stdout );

	printf( "%s...", "Lockfree test" );
	fflush( stdout );
	lock_free_tests();

	printf( "Done\n%s...", "Errors functional test" );
	fflush( stdout );
	errors_test();

	printf( "Done\n%s...", "Spin lockers and other utils test" );
	fflush( stdout );
	utils_tests();

	printf( "Done\n%s...", "Coroutines test" );
	fflush( stdout );
	coro_tests();

	printf( "Done\n%s...", "Coroutine service test" );
	fflush( stdout );
	coro_service_tests();

	printf( "Done\n%s\n", "Success" );

	return 0;
}
