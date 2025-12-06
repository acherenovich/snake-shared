set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

add_library(snake_shared_crypt STATIC
        src/crypt.cpp
        interface/crypt.hpp
)

target_compile_features(snake_shared_crypt PUBLIC cxx_std_23)

find_package(OpenSSL REQUIRED)

target_include_directories(snake_shared_crypt
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        PRIVATE
        /usr/local/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../logging/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/../coroutine/interface
)

target_link_libraries(snake_shared_crypt
        PUBLIC
        snake-shared::logging
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_library(snake-shared::crypt ALIAS snake_shared_crypt)
