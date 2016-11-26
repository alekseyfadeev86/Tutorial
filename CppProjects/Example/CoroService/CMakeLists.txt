project( CoroService )
cmake_minimum_required(VERSION 2.8)

set( ADDITIONAL_FLAGS "-DBUILD_OUTPUT_BIN=./Bin/${BuildType}")
set( ADDITIONAL_FLAGS_DEBUG "-D_DEBUG")
set( ADDITIONAL_FLAGS_RELEASE )

include_directories( . ../Coroutine ../SystemError ../Lockfree )

set( SRC_LIST ../SystemError/Errors.h ../SystemError/Errors.cpp )
set( SRC_LIST ${SRC_LIST} ../Coroutine/Coro.h ../Coroutine/Coro.cpp )
set( SRC_LIST ${SRC_LIST} Utils.h Utils.cpp )
set( SRC_LIST ${SRC_LIST} Service.h Service.cpp )
set( SRC_LIST ${SRC_LIST} ../Lockfree/LockFree.h )

if( UNIX )
	set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -std=c++11 -pthread -D_GLIBCXX_USE_NANOSLEEP -D_GLIBCXX_USE_SCHED_YIELD" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -g3 -Wall -W " )
	set( SRC_LIST ${SRC_LIST} ServiceLinux.cpp )
elseif( MSVC )
	set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -DMSVC -DWIN32 -D_WINDOWS -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -DNOMINMAX -EHsc -W3 -MP" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -Od -MTd -ZI" )
	set( ADDITIONAL_FLAGS_RELEASE "${ADDITIONAL_FLAGS_RELEASE} -O2 -MT" )
	set( SRC_LIST ${SRC_LIST} ServiceWindows.cpp )
else()
	message( FATAL_ERROR "# Unsupported OS !" )
endif()

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ADDITIONAL_FLAGS}" )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${ADDITIONAL_FLAGS_DEBUG}" )
set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${ADDITIONAL_FLAGS_RELEASE}" )

#set( LIB_NAME CoroService )
#add_library( ${LIB_NAME} ${SRC_LIST} ) - создание динамической библиотеки (.dll в винде, .so в линуксе)
#add_library( ${LIB_NAME} SHARED ${SRC_LIST} ) - создание статической библиотеки

add_executable( ${PROJECT_NAME} ${SRC_LIST} main.cpp )
#target_link_libraries( ${PROJECT_NAME} ${LIB_NAME} )

# Установка. Это действие будет активировано при выполнении команды 'make install'.
#install( TARGETS ${BIN} DESTINATION ${INSTALL_PATH} )

