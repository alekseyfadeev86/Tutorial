project(tmp)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
if( UNIX )
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -D_GLIBCXX_USE_NANOSLEEP -g -Wall -W  " )
endif()
add_executable(${PROJECT_NAME} ${SRC_LIST})

