set( LIB_NAME Bicycle )
project( ${LIB_NAME} )
cmake_minimum_required(VERSION 2.8)

# Устанавливаем версии библиотеки. В данном случае собранная библиотека будет иметь версию
# 1.0.0, а первая символическая ссылка на неё будет иметь версию 1.0.
#set( MAJOR_VERSION 1.0.0 )
#set( MINOR_VERSION 1.0 )

set( LIB_INSTALL_PATH ${ROOT}/${RES_DIR}/lib )
set( HEADERS_INSTALL_PATH ${ROOT}/${RES_DIR}/include )

#set( ADDITIONAL_FLAGS "-DBUILD_OUTPUT_BIN=./Bin/${BuildType}")
set( ADDITIONAL_FLAGS_DEBUG "-D_DEBUG")
set( ADDITIONAL_FLAGS_RELEASE )

include_directories( ./include )

# Устанавливаем корневой путь к папке с исходниками программы.
set( SOURCE_ROOT ${ROOT}/src )

# Очень полезная функция. По умолчанию имена файлов с исходниками
# нужно перечислять явно. Мне это кажется совершенно неудобным (особенно
# если файлов много). Поэтому я использую эту функцию.
# Она (рекурсивно) просматривает исходники (начиная от корневого пути,
# указанного выше) и собирает имена всех файлов, имеющих классическое
# разрешение, такие как .cpp, .cxx и т.п.
# В итоге формируется список всех найденных файлов и сохраняется в переменной SRC_LIST
#aux_source_directory( ${SOURCE_ROOT} SRC_LIST )

set( SRC_LIST ${SRC_LIST} ${SOURCE_ROOT}/Errors.h ${SOURCE_ROOT}/Errors.cpp )
#set( SRC_LIST ${SRC_LIST} ./src/Errors.cpp )

if( UNIX )
        set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -std=c++11 -pthread -D_GLIBCXX_USE_NANOSLEEP -D_GLIBCXX_USE_SCHED_YIELD" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -g3 -Wall -W -D_DEBUG " )
elseif( MSVC )
	set( ADDITIONAL_FLAGS "${ADDITIONAL_FLAGS} -DMSVC -DWIN32 -D_WINDOWS -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -DNOMINMAX -EHsc -W3 -MP" )
	set( ADDITIONAL_FLAGS_DEBUG "${ADDITIONAL_FLAGS_DEBUG} -Od -MTd -ZI" )
	set( ADDITIONAL_FLAGS_RELEASE "${ADDITIONAL_FLAGS_RELEASE} -O2 -MT" )
else()
	#message( FATAL_ERROR "# Unsupported OS !" )
endif()

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ADDITIONAL_FLAGS}" )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${ADDITIONAL_FLAGS_DEBUG}" )
set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${ADDITIONAL_FLAGS_RELEASE}" )

#set( SRC_LIST ${SRC_LIST} LockFree.h test.cpp )
#add_executable( ${PROJECT_NAME} ${SRC_LIST} )

if( SHARED_LIB )
	add_library( ${LIB_NAME} SHARED ${SRC_LIST} )
else()
	add_library( ${LIB_NAME} ${SRC_LIST} )
endif()

# Установка. Это действие будет активировано при выполнении команды 'make install'.
install( TARGETS ${LIB_NAME} DESTINATION ${LIB_INSTALL_PATH} )

# Берём из папки ${SOURCE_ROOT} все файлы с расширением .h и копируем их в ${HEADERS_INSTALL_PATH}.
#install( DIRECTORY ${SOURCE_ROOT} DESTINATION ${HEADERS_INSTALL_PATH} FILES_MATCHING PATTERN "*.h" )
install( DIRECTORY ${SOURCE_ROOT} DESTINATION ${HEADERS_INSTALL_PATH} FILES_MATCHING PATTERN "Errors.h" )

