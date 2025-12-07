# snake-shared/mysql/CMakeLists.txt

# mysql зависит от логгера и coroutine
set(VSHARED_ENABLE_LOGGING   ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_COROUTINE ON CACHE BOOL "" FORCE)

find_package(Boost REQUIRED COMPONENTS json)

# Ищем libmysqlclient
find_path(MYSQLCLIENT_INCLUDE_DIR
        NAMES mysql/mysql.h
        PATH_SUFFIXES mysql
)

find_library(MYSQLCLIENT_LIBRARY
        NAMES mysqlclient
)

if (NOT MYSQLCLIENT_INCLUDE_DIR OR NOT MYSQLCLIENT_LIBRARY)
    message(FATAL_ERROR "mysqlclient not found (need libmysqlclient-dev)")
endif()

snake_shared_add_static_module(mysql
        DEPS
        logging
        coroutine
        PUBLIC_LIBS
        Boost::json
        ${MYSQLCLIENT_LIBRARY}
)

# доп. инклюды для внешней библиотеки mysqlclient
target_include_directories(snake_shared_mysql
        PRIVATE
        ${MYSQLCLIENT_INCLUDE_DIR}
)