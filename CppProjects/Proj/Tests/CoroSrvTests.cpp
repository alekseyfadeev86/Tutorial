#include "Tests.hpp"
#include "CoroService.hpp"
#include "CoroSrv/Inet.hpp"

#include <stdio.h>
#include <string>
#include <set>

#ifdef NDEBUG
	#undef NDEBUG
	#include <assert.h>
	#define NDEBUG
#else
	#include <assert.h>
#endif

#if !(defined( _WIN32) || defined(_WIN64))
#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
inline pid_t GetCurrentThreadId()
{
	return syscall( SYS_gettid );
}
#endif

using namespace Bicycle;
using namespace CoroService;

void check_coros()
{
	const uint8_t Count = 10;
	int64_t tids[ 2*Count ] = { 0 };

	std::atomic<int64_t> StartedCoros( 0 );
	std::atomic<int64_t> FinishedCoros( 0 );
	std::atomic<uint8_t> N( 0 );

	auto coro_task = [ & ]()
	{
		++StartedCoros;
		const uint8_t n = N++;
		tids[ n ] = GetCurrentThreadId();
		MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );
		
		if( ( n % 2 ) == 0 )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}

		auto coro_task2 = [ & ]()
		{
			++StartedCoros;
			const uint8_t n = N++;
			tids[ n ] = GetCurrentThreadId();
			MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );
			++FinishedCoros;
		};
		auto err = Go( coro_task2 );
		MY_CHECK_ASSERT( !err );
		++FinishedCoros;
	}; // auto coro_task = [ & ]()

	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	MY_CHECK_ASSERT( !srv.Restart() );

	for( uint8_t t = 0; t < Count; ++t )
	{
		auto err = srv.AddCoro( coro_task );
		MY_CHECK_ASSERT( !err );
	}

	const uint8_t ThreadsNum = 4;
	std::thread threads[ ThreadsNum ];
	auto h = [ &srv ]
	{
		try
		{ srv.Run(); }
		catch( ... )
		{ MY_CHECK_ASSERT( false ); }
	};
	for( auto &th : threads ) { th = std::thread( h ); }
	for( auto &th : threads ) { th.join(); }

	MY_CHECK_ASSERT( srv.Stop() );
	MY_CHECK_ASSERT( !srv.Stop() );

	MY_CHECK_ASSERT( StartedCoros.load() == ( int64_t ) 2*Count );
	MY_CHECK_ASSERT( FinishedCoros.load() == ( int64_t ) 2*Count );

	std::set<int64_t> th_ids;
	for( int64_t id : tids ) { th_ids.insert( id ); }

	MY_CHECK_ASSERT( th_ids.size() > 1 );
	MY_CHECK_ASSERT( th_ids.size() <= ThreadsNum );
} // void check_coros()

void check_cancel()
{
	const uint8_t Count = 5;

	std::atomic<int64_t> StartedCoros( 0 );
	std::atomic<int64_t> FinishedCoros( 0 );

	auto coro_task = [ & ]()
	{
		++StartedCoros;
		MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );
		std::shared_ptr<UdpSocket> sock_ptr( new UdpSocket );

		MY_CHECK_ASSERT( sock_ptr );
		MY_CHECK_ASSERT( !sock_ptr->IsOpen() );

		Error err;
		sock_ptr->Open( err );
		MY_CHECK_ASSERT( !err );
		MY_CHECK_ASSERT( sock_ptr->IsOpen() );

		Ip4Addr addr;
		addr.SetIp( "127.0.0.1", err );
		MY_CHECK_ASSERT( !err );
		MY_CHECK_ASSERT( addr.GetIp( err ) == "127.0.0.1" );
		MY_CHECK_ASSERT( !err );

		addr.SetPortNum( 45123 );
		MY_CHECK_ASSERT( addr.GetPortNum() == 45123 );

		sock_ptr->Bind( addr, err );
		MY_CHECK_ASSERT( !err );

		auto coro_task2 = [ &StartedCoros, &FinishedCoros, sock_ptr ]()
		{
			++StartedCoros;
			MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );

			Ip4Addr sender;
			uint8_t arr[ 10 ] = {};
			BufferType buf( arr, 10 );
			Error err;

			sock_ptr->RecvFrom( buf, sender, err );
			MY_CHECK_ASSERT( err.Code == ErrorCodes::OperationAborted );
			MY_CHECK_ASSERT( sock_ptr->IsOpen() );

			++FinishedCoros;
		};

		for( uint8_t t = 0; t < Count; ++t )
		{
			err = Go( coro_task2 );
			MY_CHECK_ASSERT( !err );
		}

		while( StartedCoros.load() < ( Count + 1 ) )
		{
			std::this_thread::yield();
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		sock_ptr->Cancel( err );
		MY_CHECK_ASSERT( !err );
		MY_CHECK_ASSERT( sock_ptr->IsOpen() );

		++FinishedCoros;
	};

	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	MY_CHECK_ASSERT( !srv.Restart() );

	auto err = srv.AddCoro( coro_task );
	MY_CHECK_ASSERT( !err );

	const uint8_t ThreadsNum = 4;
	std::thread threads[ ThreadsNum ];
	auto h = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};
	for( auto &th : threads )
	{
		th = std::thread( h );
	}

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( srv.Stop() );
	MY_CHECK_ASSERT( !srv.Stop() );

	MY_CHECK_ASSERT( StartedCoros.load() == ( 1 + Count ) );
	MY_CHECK_ASSERT( FinishedCoros.load() == ( 1 + Count ) );
} // void check_cancel()

void check_stop()
{
	std::atomic<int64_t> StartedCoros( 0 );
	std::atomic<int64_t> FinishedCoros( 0 );

	auto coro_task = [ & ]()
	{
		++StartedCoros;
		MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );
		std::shared_ptr<UdpSocket> sock_ptr( new UdpSocket );

		MY_CHECK_ASSERT( sock_ptr );
		MY_CHECK_ASSERT( !sock_ptr->IsOpen() );

		Error err;
		sock_ptr->Open( err );
		MY_CHECK_ASSERT( !err );
		MY_CHECK_ASSERT( sock_ptr->IsOpen() );

		Ip4Addr addr;
		addr.SetIp( "127.0.0.1", err );
		MY_CHECK_ASSERT( !err );
		MY_CHECK_ASSERT( addr.GetIp( err ) == "127.0.0.1" );
		MY_CHECK_ASSERT( !err );

		addr.SetPortNum( 45123 );
		MY_CHECK_ASSERT( addr.GetPortNum() == 45123 );

		sock_ptr->Bind( addr, err );
		MY_CHECK_ASSERT( !err );

		auto coro_task2 = [ &StartedCoros, &FinishedCoros, sock_ptr ]()
		{
			++StartedCoros;
			MY_CHECK_ASSERT( GetCurrentCoro() != nullptr );

			Ip4Addr sender;
			uint8_t arr[ 10 ] = {};
			BufferType buf( arr, 10 );
			Error err;
			sock_ptr->RecvFrom( buf, sender, err );
			MY_CHECK_ASSERT( err.Code == ErrorCodes::OperationAborted );
			MY_CHECK_ASSERT( !sock_ptr->IsOpen() );

			++FinishedCoros;
		};

		err = Go( coro_task2 );
		MY_CHECK_ASSERT( !err );

		Ip4Addr sender;
		uint8_t arr[ 10 ] = {};
		BufferType buf( arr, 10 );
		sock_ptr->RecvFrom( buf, sender, err );
		MY_CHECK_ASSERT( err.Code == ErrorCodes::OperationAborted );
		MY_CHECK_ASSERT( !sock_ptr->IsOpen() );

		++FinishedCoros;
	};

	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	MY_CHECK_ASSERT( !srv.Restart() );

	auto err = srv.AddCoro( coro_task );
	MY_CHECK_ASSERT( !err );

	const uint8_t ThreadsNum = 4;
	std::thread threads[ ThreadsNum ];
	auto h = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};
	for( auto &th : threads )
	{
		th = std::thread( h );
	}

	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	MY_CHECK_ASSERT( StartedCoros.load() == 2 );
	MY_CHECK_ASSERT( FinishedCoros.load() == 0 );

	MY_CHECK_ASSERT( srv.Stop() );
	MY_CHECK_ASSERT( !srv.Stop() );

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( StartedCoros.load() == 2 );
	MY_CHECK_ASSERT( FinishedCoros.load() == 2 );
} // void check_stop()

void check_udp_sock( bool single_thread )
{
	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );

	const uint8_t senders_count = 10;
	std::atomic<uint8_t> senders_finished( 0 );

	auto err = srv.AddCoro( [ senders_count, &senders_finished ]()
	{
		Ip4Addr reader_addr;
		Error err;
		reader_addr.SetIp( "127.0.0.1", err );
		MY_CHECK_ASSERT( !err );
		reader_addr.SetPortNum( 45123 );

		std::shared_ptr<UdpSocket> reader( new UdpSocket );
		MY_CHECK_ASSERT( reader );
		reader->Open( err );
		MY_CHECK_ASSERT( !err );
		reader->Bind( reader_addr, err );
		MY_CHECK_ASSERT( !err );

		std::function<void()> read_task = [ reader ]()
		{
			using namespace ErrorCodes;

			MY_CHECK_ASSERT( reader );
			Ip4Addr sender_addr;
			uint8_t arr[ 100 ] = { 0 };
			BufferType buf( arr, sizeof( arr ) );
			ConstBufferType cbuf( arr, sizeof( arr ) );
			Error err;

			while( 1 )
			{
				size_t res = reader->RecvFrom( buf, sender_addr, err );
				MY_CHECK_ASSERT( ( res == 0 ) == ( bool ) err );
				MY_CHECK_ASSERT( res <= 100 );

				if( err )
				{
					MY_CHECK_ASSERT( ( err.Code == SrvStop ) || ( err.Code == NotOpen ) || ( err.Code == OperationAborted ) );
					break;
				}

				cbuf.second = res;
				res = reader->SendTo( cbuf, sender_addr, err );
				MY_CHECK_ASSERT( ( res == 0 ) == ( bool ) err );
				MY_CHECK_ASSERT( res == cbuf.second );

				if( err )
				{
					MY_CHECK_ASSERT( ( err.Code == SrvStop ) || ( err.Code == NotOpen )|| ( err.Code == OperationAborted ) );
					break;
				}
			}

			MY_CHECK_ASSERT( !reader->IsOpen() );
		}; // std::function<void()> read_task = [ reader ]()

		for( uint8_t t = 0; t < 10; ++t )
		{
			err = Go( read_task );
			MY_CHECK_ASSERT( !err );
		}

		std::function<void( uint16_t )> send_task = [ reader_addr,
		                                              reader,
		                                              senders_count,
		                                              &senders_finished ]( uint16_t port_num )
		{
			MY_ASSERT( reader );
			Ip4Addr addr;
			Error err;
			addr.SetIp( "127.0.0.1", err );
			MY_CHECK_ASSERT( !err );
			addr.SetPortNum( port_num );

			UdpSocket writer;
			writer.Open( err );
			MY_CHECK_ASSERT( !err );
			writer.Bind( addr, err );
			MY_CHECK_ASSERT( !err );

			uint8_t arr1[ 5 ] = { 0 };
			uint8_t arr2[ 100 ] = { 0 };

			ConstBufferType snd_buf( arr1, sizeof( arr1 ) );
			BufferType rcv_buf( arr2, sizeof( arr2 ) );
			Ip4Addr sender_addr;

			for( uint8_t t = 0; t < 10; ++t )
			{
				for( size_t j = 0; j < snd_buf.second; ++j )
				{
					arr1[ j ] = 10*t + j;
				}

				size_t res = writer.SendTo( snd_buf, reader_addr, err );
				MY_CHECK_ASSERT( !err );
				MY_CHECK_ASSERT( res == snd_buf.second );

				res = writer.RecvFrom( rcv_buf, sender_addr, err );
				MY_CHECK_ASSERT( !err );
				MY_CHECK_ASSERT( res == snd_buf.second );

				for( size_t t = 0; t < res; ++t )
				{
					MY_CHECK_ASSERT( arr1[ t ] == arr2[ t ] );
				}

				string ip_str = sender_addr.GetIp( err );
				MY_CHECK_ASSERT( !err );
				MY_CHECK_ASSERT( ip_str == "127.0.0.1" );
				MY_CHECK_ASSERT( sender_addr.GetPortNum() == reader_addr.GetPortNum() );
			}

			if( ++senders_finished == senders_count )
			{
				// Последний отправитель закончил работу
				reader->Close( err );
				MY_CHECK_ASSERT( !err );
			}
		}; // std::function<void( uint16_t )> send_task
		reader.reset();

		const uint16_t reader_port = reader_addr.GetPortNum();
		for( uint8_t t = 1; t <= senders_count; ++t )
		{
			uint16_t port_number = reader_port + t;
			err = Go( [ send_task, port_number ]() { send_task( port_number ); } );
			MY_CHECK_ASSERT( !err );
		}
	}); // auto err = srv.AddCoro( [ senders_count, &senders_finished ]()
	MY_CHECK_ASSERT( !err );

	const uint8_t threads_num = single_thread ? 1 : 4;
	std::vector<std::thread> threads( threads_num );
	MY_CHECK_ASSERT( threads.size() == threads_num );
	std::function<void()> thread_task = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( thread_task );
	}

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( srv.Stop() );
	MY_CHECK_ASSERT( senders_finished.load() == senders_count );
} // void check_udp_sock( bool single_thread )

void check_tcp( bool single_thread )
{
	using namespace ErrorCodes;

	const uint8_t ConnectionsNum = 10;
	static std::atomic<uint16_t> PortNumber( 30000 );
	std::atomic<int64_t> FinishedConns( 0 );
	const uint16_t srv_port_num = ( PortNumber += ConnectionsNum );

	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	Error err = srv.AddCoro( [ srv_port_num, ConnectionsNum, &FinishedConns ]()
	{
		Error err;
		std::shared_ptr<TcpAcceptor> acceptor_ptr( new TcpAcceptor );
		MY_CHECK_ASSERT( acceptor_ptr );

		TcpAcceptor &acceptor = *acceptor_ptr;
		acceptor.Open( err );
		MY_CHECK_ASSERT( !err );

		Ip4Addr srv_addr;
		srv_addr.SetIp( "127.0.0.1", err );
		MY_CHECK_ASSERT( !err );
		srv_addr.SetPortNum( srv_port_num );

		acceptor.Bind( srv_addr, err );
		MY_CHECK_ASSERT( !err );

		acceptor.Listen( 2* ConnectionsNum, err );
		MY_CHECK_ASSERT( !err );

		auto task = [ srv_port_num, srv_addr ]( uint8_t n )
		{
			Error err;
			Ip4Addr addr;
			addr.SetIp( "127.0.0.1", err );
			MY_CHECK_ASSERT( !err );
			addr.SetPortNum( srv_port_num + n );
			TcpConnection conn;
			conn.Open( err );
			MY_CHECK_ASSERT( !err );

			conn.Bind( addr, err );
			MY_CHECK_ASSERT( !err );

			conn.Connect( srv_addr, err );
			MY_CHECK_ASSERT( !err );

			uint8_t arr1[ 10 ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
			uint8_t arr2[ 101 ] = { 0 };

			size_t res = conn.Send( ConstBufferType( arr1, sizeof( arr1 ) ), err );
			MY_CHECK_ASSERT( !err );
			MY_CHECK_ASSERT( res == sizeof( arr1 ) );

			size_t total_res = 0;
			for( ; total_res < res; ++total_res )
			{
				size_t rs = conn.Recv( BufferType( arr2 + total_res, 1 ), err );
				MY_CHECK_ASSERT( !err );
				MY_CHECK_ASSERT( rs == 1 );
				MY_CHECK_ASSERT( arr1[ total_res ] == arr2[ total_res ] );
			}

			conn.Close( err );
			MY_CHECK_ASSERT( !conn.IsOpen() );
			MY_CHECK_ASSERT( !err );
		};

		for( uint8_t t = 1; t <= ConnectionsNum; ++t )
		{
			err = Go( [ task, t ]() { task( t ); } );
			MY_CHECK_ASSERT( !err );
		}

		do
		{
			std::shared_ptr<TcpConnection> conn( new TcpConnection );
			MY_CHECK_ASSERT( conn );

			Ip4Addr addr;
			acceptor.Accept( *conn, addr, err );

			if( !err )
			{
				MY_CHECK_ASSERT( conn->IsOpen() );
				err = Go( [ conn, acceptor_ptr, &FinishedConns, ConnectionsNum ]()
				{
					MY_CHECK_ASSERT( conn );
					MY_CHECK_ASSERT( conn->IsOpen() );
					MY_CHECK_ASSERT( acceptor_ptr );
					Error err;

					uint8_t arr[ 101 ] = { 0 };
					BufferType buf( arr, sizeof( arr ) );

					bool run = true;
					while( run )
					{
						size_t res = conn->Recv( buf, err );
						MY_CHECK_ASSERT( !err );
						MY_CHECK_ASSERT( res > 0 );

						for( size_t t = 0; t < res; ++t )
						{
							if( arr[ t ] == 0 )
							{
								run = false;
								break;
							}
						}

						ConstBufferType cbuf( arr, res );
						res = conn->Send( cbuf, err );
						MY_CHECK_ASSERT( !err );
						MY_CHECK_ASSERT( res == cbuf.second );
					}

					conn->Close( err );
					MY_CHECK_ASSERT( !conn->IsOpen() );
					MY_CHECK_ASSERT( !err );

					if( ++FinishedConns == ConnectionsNum )
					{
						acceptor_ptr->Close( err );
						MY_CHECK_ASSERT( !err );
					}
				} );
				MY_CHECK_ASSERT( !err );
			}
		}
		while( !err );
		MY_CHECK_ASSERT( ( err.Code == OperationAborted ) || ( err.Code == NotOpen ) );
		MY_CHECK_ASSERT( !acceptor.IsOpen() );
	} ); // Error err = srv.AddCoro( [ ConnectionsNum, &FinishedConns ]()
	MY_CHECK_ASSERT( !err );

	const uint8_t threads_num = single_thread ? 1 : 4;
	std::vector<std::thread> threads( threads_num );
	MY_CHECK_ASSERT( threads.size() == threads_num );
	std::function<void()> thread_task = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( thread_task );
	}

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( srv.Stop() );
} // void check_tcp( bool single_thread )

void check_sync( bool single_thread )
{
	std::atomic<int64_t> total_unique_locks( 0 );
	std::atomic<int64_t> unique_locks_counter( 0 );

	std::atomic<int64_t> total_sh_unique_locks( 0 );
	std::atomic<int64_t> total_sh_shared_locks( 0 );
	std::atomic<int64_t> sh_unique_locks_counter( 0 );
	std::atomic<int64_t> sh_shared_locks_counter( 0 );

	std::atomic<int64_t> semaphore_counter( 0 );

	std::atomic<int64_t> ev_waiters_num( 0 );

	auto task = [ & ]()
	{
		std::shared_ptr<Mutex> mut_ptr( new Mutex );
		MY_CHECK_ASSERT( mut_ptr );

		for( uint8_t t = 0; t < 10; ++t )
		{
			auto err = Go( [ &, mut_ptr ]()
			{
				for( uint8_t i = 0; i < 10; ++i )
				{
					MY_CHECK_ASSERT( unique_locks_counter.load() <= 1 );
					mut_ptr->Lock();

					++unique_locks_counter;
					MY_CHECK_ASSERT( unique_locks_counter.load() == 1 );
					++total_unique_locks;

					std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

					--unique_locks_counter;
					MY_CHECK_ASSERT( unique_locks_counter.load() == 0 );

					mut_ptr->Unlock();
					MY_CHECK_ASSERT( unique_locks_counter.load() <= 1 );
				}
			});
			MY_CHECK_ASSERT( !err );
		}
		mut_ptr.reset();

		std::shared_ptr<SharedMutex> sh_mut_ptr( new SharedMutex );
		MY_CHECK_ASSERT( sh_mut_ptr );
		for( uint8_t t = 0; t < 10; ++t )
		{
			auto err = Go( [ &, sh_mut_ptr ]()
			{
				for( uint8_t i = 0; i < 10; ++i )
				{
					if( ( i % 2 ) == 0 )
					{
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() <= 1 );
						sh_mut_ptr->Lock();

						++sh_unique_locks_counter;
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() == 1 );
						MY_CHECK_ASSERT( sh_shared_locks_counter.load() == 0 );
						++total_sh_unique_locks;

						std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

						--sh_unique_locks_counter;
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() == 0 );
						MY_CHECK_ASSERT( sh_shared_locks_counter.load() == 0 );

						sh_mut_ptr->Unlock();
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() <= 1 );
					}
					else
					{
						sh_mut_ptr->SharedLock();
						++sh_shared_locks_counter;

						++total_sh_shared_locks;
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() == 0 );
						MY_CHECK_ASSERT( sh_shared_locks_counter.load() > 0 );

						std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

						--sh_shared_locks_counter;
						MY_CHECK_ASSERT( sh_unique_locks_counter.load() == 0 );
						MY_CHECK_ASSERT( sh_shared_locks_counter.load() >= 0 );
						sh_mut_ptr->Unlock();
					}
				}
			});
			MY_CHECK_ASSERT( !err );
		}
		sh_mut_ptr.reset();

		std::shared_ptr<Semaphore> sem_ptr( new Semaphore );
		MY_CHECK_ASSERT( sem_ptr );
		for( uint8_t t = 0; t < 10; ++t )
		{
			auto err = Go( [ &, sem_ptr ]()
			{
				MY_CHECK_ASSERT( sem_ptr );
				for( uint8_t i = 0; i < 10; ++i )
				{
					if( ( i % 2 ) == 0 )
					{
						MY_CHECK_ASSERT( semaphore_counter.load() >= 0 );
						std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
						sem_ptr->Push();
						++semaphore_counter;
						MY_CHECK_ASSERT( semaphore_counter.load() > 0 );
					}
					else
					{
						MY_CHECK_ASSERT( semaphore_counter.load() > 0 );
						sem_ptr->Pop();
						--semaphore_counter;
						MY_CHECK_ASSERT( semaphore_counter.load() >= 0 );
					}
				}
			});
			MY_CHECK_ASSERT( !err );
		}
		sem_ptr.reset();

		std::shared_ptr<Event> ev_ptr( new Event );
		MY_CHECK_ASSERT( ev_ptr );

		for( uint8_t step = 0; step < 2; ++step )
		{
			for( uint8_t t = 0; t < 10; ++t )
			{
				auto err = Go( [ &, ev_ptr ]()
				{
					MY_CHECK_ASSERT( ev_ptr );
					++ev_waiters_num;
					MY_CHECK_ASSERT( ev_waiters_num.load() > 0 );
					ev_ptr->Wait();
					--ev_waiters_num;
					MY_CHECK_ASSERT( ev_waiters_num.load() >= 0 );
				});
				MY_CHECK_ASSERT( !err );
			}

			while( ev_waiters_num.load() < 10 )
			{
				YieldCoro();
			}

			ev_ptr->Set();

			while( ev_waiters_num.load() > 0 )
			{
				YieldCoro();
			}
			MY_CHECK_ASSERT( ev_waiters_num.load() == 0 );

			ev_ptr->Reset();
		} // for( uint8_t step = 0; step < 2; ++step )

	}; //auto task

	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	Error err = srv.AddCoro( task );
	MY_CHECK_ASSERT( !err );

	const uint8_t threads_num = single_thread ? 1 : 4;
	std::vector<std::thread> threads( threads_num );
	MY_CHECK_ASSERT( threads.size() == threads_num );
	std::function<void()> thread_task = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( thread_task );
	}

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( srv.Stop() );

	MY_CHECK_ASSERT( unique_locks_counter.load() == 0 );
	MY_CHECK_ASSERT( total_unique_locks.load() > 10 );
	
	MY_CHECK_ASSERT( total_sh_unique_locks.load() > 10 );
	MY_CHECK_ASSERT( total_sh_shared_locks.load() > 10 );
	MY_CHECK_ASSERT( sh_unique_locks_counter.load() == 0 );
	MY_CHECK_ASSERT( sh_shared_locks_counter.load() == 0 );

	MY_CHECK_ASSERT( semaphore_counter.load() == 0 );
} // void check_sync()

void check_timer( bool single_thread )
{
	Service srv;
	MY_CHECK_ASSERT( srv.Restart() );
	Error err = srv.AddCoro( []()
	{
		std::shared_ptr<Timer> timer_ptr( new Timer );
		MY_CHECK_ASSERT( timer_ptr );
		Timer &timer = *timer_ptr;
		Error err;

		timer.Wait( err );
		MY_CHECK_ASSERT( err );
		MY_CHECK_ASSERT( err.Code == ErrorCodes::TimerExpired );

		timer.ExpiresAfter( 100*1000, err );
		MY_CHECK_ASSERT( !err );

		timer.ExpiresAfter( 1000*1000, err );
		MY_CHECK_ASSERT( err );
		MY_CHECK_ASSERT( err.Code == ErrorCodes::TimerNotExpired );

		timer.Wait( err );
		MY_CHECK_ASSERT( !err );

		timer.Wait( err );
		MY_CHECK_ASSERT( err );
		MY_CHECK_ASSERT( err.Code == ErrorCodes::TimerExpired );

		timer.ExpiresAfter( 1000*1000, err );
		MY_CHECK_ASSERT( !err );

		for( uint8_t t = 0; t < 10; ++t )
		{
			err = Go( [ timer_ptr ]()
			{
				MY_ASSERT( timer_ptr );
				Error err;
				timer_ptr->Wait( err );
				MY_CHECK_ASSERT( !err );
			});
		}
		timer.Wait( err );
		MY_CHECK_ASSERT( !err );

		timer.ExpiresAfter( 1000*1000*1000, err );
		MY_CHECK_ASSERT( !err );
		std::shared_ptr<std::atomic<uint8_t>> count_ptr( new std::atomic<uint8_t>( 0 ) );
		const uint8_t number = 10;
		for( uint8_t t = 0; t < number; ++t )
		{
			err = Go( [ timer_ptr, count_ptr, number ]()
			{
				MY_ASSERT( timer_ptr );
				MY_ASSERT( count_ptr );

				Error err;
				uint8_t val = ++( *count_ptr );
				if( val == number )
				{
					std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
					timer_ptr->Cancel( err );
					MY_CHECK_ASSERT( !err );
				}
				else
				{
					timer_ptr->Wait( err );
					MY_CHECK_ASSERT( err );
					MY_CHECK_ASSERT( err.Code == ErrorCodes::OperationAborted );
				}
			});
		}
	} ); // Error err = srv.AddCoro
	MY_CHECK_ASSERT( !err );

	const uint8_t threads_num = single_thread ? 1 : 4;
	std::vector<std::thread> threads( threads_num );
	MY_CHECK_ASSERT( threads.size() == threads_num );
	std::function<void()> thread_task = [ &srv ]
	{
		try
		{
			srv.Run();
		}
		catch( ... )
		{
			MY_CHECK_ASSERT( false );
		}
	};

	for( auto &th : threads )
	{
		th = std::thread( thread_task );
	}

	for( auto &th : threads )
	{
		th.join();
	}

	MY_CHECK_ASSERT( srv.Stop() );
} // void check_timer( bool single_thread )

void coro_service_tests()
{
	const uint16_t steps_num = 100;
	for( uint16_t t = 1; t <= steps_num; ++t )
	{
#ifdef _DEBUG
		printf( "Step %i of %i\n", t, steps_num );
		fflush( stdout );
#endif

		check_coros();
		check_cancel();
		check_stop();
		check_udp_sock( false );
		check_udp_sock( true );
		check_tcp( false );
		check_tcp( true );
		check_sync( true );
		check_sync( false );
		check_timer( true );
		check_timer( false );
	}
}
