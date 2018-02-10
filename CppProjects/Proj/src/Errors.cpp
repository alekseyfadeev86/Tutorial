#include "Errors.hpp"
#include <string.h>

#if !(defined( _WIN32) || defined(_WIN64))
#include <errno.h>
#endif

namespace Bicycle
{
	inline string StrErrorByCode( err_code_t err_code )
	{
#if defined( _WIN32) || defined(_WIN64)
		char res[ 501 ] = { 0 };
		if( FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM,
		    0, err_code, MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US ),
		    ( LPSTR ) &res, sizeof( res ), 0 ) != 0 )
		{
			return res;
		}

		return "Unknown error";
#else
		return ( string ) strerror( err_code );
#endif
	}

	Error::Error( Error &&err ): Code( err.Code ),
	                             What( std::move(  err.What ) )
	{
		err.Code = ErrorCodes::Success;
	}

	Error& Error::operator=( Error &&err )
	{
		if( &err != this )
		{
			Code = err.Code;
			err.Code = ErrorCodes::Success;
			What = std::move( err.What );
		}

		return *this;
	}

	Error::Error( err_code_t err_code,
	              const string &what ): Code( err_code ),
	                                    What( what )
	{}

	Error::Error( err_code_t err_code,
	              const char *what ): Code( err_code ),
	                                  What( what )
	{}

	Error::Error( err_code_t err_code,
	              string &&what ): Code( err_code ),
	                               What( std::move( what ) )
	{}

	Error::operator bool() const
	{
		return Code != ErrorCodes::Success;
	}
	
	void Error::Reset()
	{
		Code = ErrorCodes::Success;
		What.clear();
	}

	//----------------------------------------------------------------------

	Exception::Exception( err_code_t err_code,
	                      const string &what ): std::runtime_error( what ),
	                                            ErrorCode( err_code )
	{}

	Exception::Exception( err_code_t err_code,
	                      const char *what ): std::runtime_error( what ),
	                                          ErrorCode( err_code )
	{}

	Error GetSystemErrorByCode( err_code_t err_code )
	{
		return Error( err_code, StrErrorByCode( err_code ) );
	}

	Error GetLastSystemError()
	{
		err_code_t err = 0;

#if defined( _WIN32) || defined(_WIN64)
		err = GetLastError();
		SetLastError( ERROR_SUCCESS );
#else
		err = errno;
		errno = 0;
#endif

		return Error( err, StrErrorByCode( err ) );
	}

	void ThrowIfNeed()
	{
		err_code_t err = 0;

#if defined( _WIN32) || defined(_WIN64)
		err = GetLastError();
		SetLastError( ERROR_SUCCESS );
#else
		err = errno;
		errno = 0;
#endif

		if( err != ErrorCodes::Success )
		{
			throw Exception( err, StrErrorByCode( err ) );
		}
	}

	void ThrowIfNeed( const Error &err )
	{
		if( err )
		{
			throw Exception( err.Code, err.What.c_str() );
		}
	}
} // namespace Bicycle
