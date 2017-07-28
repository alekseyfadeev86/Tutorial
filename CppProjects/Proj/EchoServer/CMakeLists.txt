cmake_minimum_required( VERSION 2.8 )
project( EchoServer )

set( INCLUDE_DIR ../include )
set( SRC_DIR ../src )

include_directories( . ${INCLUDE_DIR} )

# Подцепляем вссе нужные файлы
set( SRC_LIST )
set( SRC_LIST ${SRC_LIST} ${INCLUDE_DIR}/LockFree.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/Errors.cpp ${INCLUDE_DIR}/Errors.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/Utils.cpp ${INCLUDE_DIR}/Utils.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/Coro.cpp ${INCLUDE_DIR}/Coro.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/Service.cpp ${INCLUDE_DIR}/CoroSrv/Service.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/Inet.cpp ${INCLUDE_DIR}/CoroSrv/Inet.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/Sync.cpp ${INCLUDE_DIR}/CoroSrv/Sync.hpp )
set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/Timer.cpp ${INCLUDE_DIR}/CoroSrv/Timer.hpp )

set( ADDITIONAL_FLAGS "-DBUILD_OUTPUT_BIN=./Output/${BuildType}")
set( ADDITIONAL_FLAGS_DEBUG "-D_DEBUG")
set( ADDITIONAL_FLAGS_RELEASE )

if( UNIX )
        set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -std=c++11 -pthread -D_GLIBCXX_USE_NANOSLEEP -D_GLIBCXX_USE_SCHED_YIELD" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -g3 -Wall -W -D_DEBUG " )
	set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/ServiceLinux.cpp )
	set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/InetLinux.cpp )
elseif( MSVC )
	set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -DMSVC -DWIN32 -D_WINDOWS -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -DNOMINMAX -EHsc -W3 -MP" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -Od -MTd -ZI" )
	set( ADDITIONAL_FLAGS_RELEASE "${ADDITIONAL_FLAGS_RELEASE} -O2 -MT" )
	set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/ServiceWindows.cpp )
	set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/CoroSrv/InetWindows.cpp )
else()
	#message( FATAL_ERROR "# Unsupported OS !" )
endif()

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ADDITIONAL_FLAGS}" )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${ADDITIONAL_FLAGS_DEBUG}" )
set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${ADDITIONAL_FLAGS_RELEASE}" )

set( SRC_LIST ${SRC_LIST} ${SRC_DIR}/Errors.cpp ${INCLUDE_DIR}/Errors.hpp )
set( SRC_LIST ${SRC_LIST} main.cpp )
add_executable( ${PROJECT_NAME} ${SRC_LIST} )

