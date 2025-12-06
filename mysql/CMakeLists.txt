cmake_minimum_required(VERSION 3.20)

project(snake_shared_mysql LANGUAGES CXX)

set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_COROUTINE ON CACHE BOOL "" FORCE)

add_library(snake_shared_mysql STATIC
        src/mysql_client.cpp
        src/mysql_client.hpp
        interface/mysql.hpp
)

target_compile_features(snake_shared_mysql PUBLIC cxx_std_23)

target_include_directories(snake_shared_mysql
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../logging/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/../coroutine/interface
)

find_package(Boost REQUIRED COMPONENTS json)

# Простейший поиск mysqlclient (для макос/линукса с libmysqlclient-dev)
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

target_include_directories(snake_shared_mysql
        PRIVATE
        ${MYSQLCLIENT_INCLUDE_DIR}
)

target_link_libraries(snake_shared_mysql
        PUBLIC
        snake-shared::logging
        Boost::json
        ${MYSQLCLIENT_LIBRARY}
)

add_library(snake-shared::mysql ALIAS snake_shared_mysql)