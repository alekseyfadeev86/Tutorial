# Сборочный файл для программы my_program

# Минимальная версия CMake, младше нельзя.
cmake_minimum_required( VERSION 2.6 )

# Устанавливаем значение переменной BIN
set( BIN my_program )
# Имя проекта у нас соответствует имени программы.
project( ${BIN} )

# Устанавливаем корневой путь к папке с исходниками программы.
set( SOURCE_ROOT ../src )

# Очень полезная функция. По умолчанию имена файлов с исходниками
# нужно перечислять явно. Мне это кажется совершенно неудобным (особенно
# если файлов много). Поэтому я использую эту функцию.
# Она (рекурсивно) просматривает исходники (начиная от корневого пути,
# указанного выше) и собирает имена всех файлов, имеющих классическое
# разрешение, такие как .cpp, .cxx и т.п.
# В итоге формируется список всех найденных файлов и сохраняется в переменной
# SOURCES
aux_source_directory( ${SOURCE_ROOT} SOURCES )

# Указываем пути, где искать заголовочники, дополнительно к стандартным путям.
include_directories( "/my/addition/path/to/headers"
                     "${SOURCE_ROOT}" )

# Устанавливаем путь, куда будет скопирован исполняемый файл при выполнении
# команды 'make install'.
set( INSTALL_PATH /usr/local/bin/ )

# Указываем пути, где искать сторонние динамически библиотеки. Эти пути будут
# просмотрены в дополнение к умолчальными системным путям.
link_directories( "/my/addition/path/to/lib" )

# Перечисляем необходимые сторонние динамические библиотеки и сохраняем
# их список в переменной REQUIRED_LIBRARIES.
# Поиск этих библиотек будет произведён как по стандартным системным путям, 
# так и по указанным выше (с помощью функции link_directories).
set( REQUIRED_LIBRARIES
		my_addition_lib
		boost_thread 
		boost_system )

# Устанавливаем флаги компилятора (причём никаких скрытых флагов, только эти).
set( CMAKE_CXX_FLAGS "-O0 -Wall -pedantic -std=c++0x" ) 

# Собираем исполняемый файл с именем my_program.
add_executable( ${BIN} ${SOURCES} )
# Компонуем собранную программу с необходимыми динамическими библиотеками.
target_link_libraries( ${BIN} ${REQUIRED_LIBRARIES} )

# Установка. Это действие будет активировано при выполнении команды 'make install'.
install( TARGETS ${BIN} DESTINATION ${INSTALL_PATH} )

#Всё. На мой взгляд, чрезвычайно понятный синтаксис, и даже комментарии не особо нужны.
#Динамическая библиотека

# Сборочный файл для библиотеки my_lib

# См. выше.
cmake_minimum_required( VERSION 2.6 ) 

# Устанавливаем значение переменной LIB
set( LIB my_lib ) 
# Имя проекта у нас соответствует имени библиотеки.
project( ${LIB} ) 

# Устанавливаем версии библиотеки. В данном случае собранная библиотека будет иметь версию
# 1.0.0, а первая символическая ссылка на неё будет иметь версию 1.0.
set( MAJOR_VERSION 1.0.0 )
set( MINOR_VERSION 1.0 )

# Устанавливаем корневые пути к папками с исходниками библиотеки и 
# к заголовочникам для установки.
set( SOURCE_ROOT ../src )
set( HEADERS ../include )

# См. выше.
aux_source_directory( ${SOURCE_ROOT} SOURCES )
# См. выше.
include_directories( "${HEADERS}"
                     "${SOURCE_ROOT}"
                     "/my/addition/path/to/headers" )

# См. выше.
set( LIB_INSTALL_PATH /usr/local/lib )
# Устанавливаем путь установки заголовочных файлов библиотеки.
set( HEADERS_INSTALL_PATH /usr/local/include/my_lib )
# См. выше.
link_directories( "${LIB_INSTALL_PATH}" )

# См. выше.
set( REQUIRED_LIBRARIES 
		my_addition_lib
		boost_thread 
		boost_system )

# См. выше.
set( CMAKE_CXX_FLAGS "-O0 -Wall -pedantic -std=c++0x" )

# Собираем динамическую библиотеку.
add_library( ${LIB} SHARED ${SOURCES} )
# См. выше.
target_link_libraries( ${LIB} ${REQUIRED_LIBRARIES} )

# Создаём символические ссылки. Учитывая указанные выше версии, результат будет
# таким:
# lib_my_lib.so       -> lib_my_lib.so.1.0
# lib_my_lib.so.1.0   -> lib_my_lib.so.1.0.0
# lib_my_lib.so.1.0.0
# Это действие будет активировано при выполнении команды 'make install'.
set_target_properties( ${LIB} PROPERTIES 
                              VERSION ${MAJOR_VERSION} 
                              SOVERSION ${MINOR_VERSION} )

# Установка. Это действие будет активировано при выполнении команды 'make install'.
install( TARGETS ${LIB} DESTINATION ${LIB_INSTALL_PATH} )
# Берём из папки ${HEADERS} все файлы с расширением .hpp и копируем их в 
# ${HEADERS_INSTALL_PATH}.
install( DIRECTORY ${HEADERS}/
         DESTINATION ${HEADERS_INSTALL_PATH}
         FILES_MATCHING PATTERN "*.hpp" )
