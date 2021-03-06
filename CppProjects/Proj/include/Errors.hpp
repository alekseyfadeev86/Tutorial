#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <string>
#include <stdexcept>

#ifndef MY_ASERT
	#ifdef _DEBUG
	#include <assert.h>
	#define MY_ASSERT(E) assert( E )
	#else
	#define MY_ASSERT(E)
	#endif
#endif

namespace Bicycle
{
	using std::string;

#ifdef _WIN32
	typedef DWORD err_code_t;
#else
	typedef int err_code_t;
	static const err_code_t Success = 0;
#endif

	namespace ErrorCodes
	{
#ifdef _WIN32
		/// Успех
		const err_code_t Success = ERROR_SUCCESS;
#else
		/// Успех
		const err_code_t Success = 0;
#endif

		/// Неизвестная ошибка
		const err_code_t UnknownError = 0xFFFFFFEE;
	} // namespace ErrorCodes

	/// Структура ошибки
	struct Error
	{
		/// Числовой код ошибки
		err_code_t Code;

		/// Строковое описание ошибки
		string What;

		Error( const Error& ) = default;
		Error& operator=( const Error& ) = default;

		Error( Error &&err );
		Error& operator=( Error &&err );

		Error( err_code_t err_code = ErrorCodes::Success,
		       const string &what = string() );

		Error( err_code_t err_code, const char *what );
		Error( err_code_t err_code, string &&what );

		/**
		 * @brief operator bool возвращает true, если Code != Success
		 */
		operator bool() const;
	};

	/// Структура исключения
	struct Exception: public std::runtime_error
	{
		const err_code_t ErrorCode;
		Exception( err_code_t err_code,
		           const string &what = string() );
		Exception( err_code_t err_code,
		           const char *what );
	};

	Error GetSystemErrorByCode( err_code_t err_code );

	/**
	 * @brief GetLastSystemError возвращает последнюю системную ошибку
	 * @return структура с описанием ошибки
	 */
	Error GetLastSystemError();

	/**
	 * @brief ThrowIfNeed получает код последней системной ошибки
	 * и выбрасывает соответствующее исключение, если ошибка была
	 * @throw Exception
	 */
	void ThrowIfNeed();

	/**
	 * @brief ThrowIfNeed выбрасывает исключение при необходимости
	 * @param err обрабатываемая ошибка
	 * @throw Exception
	 */
	void ThrowIfNeed( const Error &err );
} // namespace Bicycle
