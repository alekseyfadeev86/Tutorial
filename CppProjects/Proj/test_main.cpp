#include "Tests.hpp"

int main( int argc, char *argv[] )
{
	printf( "%s\n", "Start..." );
	fflush( stdout );

	lock_free_tests();
	errors_test();
	utils_tests();
	coro_tests();
	coro_service_tests();

	printf( "%s\n", "Success" );

	return 0;
}
