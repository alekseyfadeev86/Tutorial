#include "Tests.hpp"

int main( int argc, char *argv[] )
{	
	printf( "%s\n", "Start..." );
	fflush( stdout );

	printf( "%s...", "Lockfree test" );
	fflush( stdout );
	lock_free_tests();

	printf( "Done\n%s...", "Errors functional test" );
	fflush( stdout );
	errors_test();

	printf( "Done\n%s...", "Spin lockers test" );
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
