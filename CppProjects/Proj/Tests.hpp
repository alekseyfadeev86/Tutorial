#pragma once
#include <stdio.h>

#ifdef NDEBUG
	#undef NDEBUG
	#include <assert.h>
	#define NDEBUG
#else
	#include <assert.h>
#endif

#ifdef _DEBUG
#define MY_ASSERT(E) assert( E )
#else
#define MY_ASSERT(E)
#endif

#define MY_CHECK_ASSERT(E) assert( E )

#define UNITTEST

void lock_free_tests();
void errors_test();
void utils_tests();
void coro_tests();
void coro_service_tests();
