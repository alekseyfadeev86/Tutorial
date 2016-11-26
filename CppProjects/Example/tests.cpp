#include "Errors.h"
#include <stdio.h>

#ifdef NDEBUG
        #undef NDEBUG
        #include <assert.h>
        #define NDEBUG
#else
        #include <assert.h>
#endif

void test_error()
{
        using namespace Bicycle;

        Error example;
        assert( !example );
        assert( ( example.Code == ErrorCodes::Success ) && example.What.empty() );

        example.Code = 123;
        example.What = "qaz";
        assert( example );

        Error fake;
        Error test( example );
        assert( ( test.Code == example.Code ) && ( test.What == example.What ) );
        assert( ( bool ) test == ( bool ) example );

        Error test2;
        test2 = example;
        assert( ( test2.Code == example.Code ) && ( test2.What == example.What ) );
        assert( ( bool ) test2 == ( bool ) example );

        Error test3( std::move( test ) );
        assert( ( test3.Code == example.Code ) && ( test3.What == example.What ) );
        assert( ( bool ) test3 == ( bool ) example );
        assert( ( test.Code == fake.Code ) && ( test.What == fake.What ) );
        assert( ( bool ) test == ( bool ) fake );

        test = std::move( test2 );
        assert( ( test.Code == example.Code ) && ( test.What == example.What ) );
        assert( ( bool ) test == ( bool ) example );
        assert( ( test2.Code == fake.Code ) && ( test2.What == fake.What ) );
        assert( ( bool ) test2 == ( bool ) fake );
} // void test_coroutines()

int main( int argc, char *argv[] )
{
        test_error();

        printf( "%s\n", "Success" );
        return 0;
}
