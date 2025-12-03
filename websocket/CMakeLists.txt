# Вебсокеты зависят от логгера и тредера
set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_THREADER ON CACHE BOOL "" FORCE)

add_library(snake_shared_websocket STATIC
        src/websocket_server.cpp
        src/websocket_server.hpp
        src/websocket_session.cpp
        src/websocket_session.hpp
        src/websocket_client.cpp
        src/websocket_client.hpp
        interface/websocket.hpp
)

target_compile_features(snake_shared_websocket PUBLIC cxx_std_23)

target_include_directories(snake_shared_websocket
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../logging/interface
)

find_package(Boost REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

target_link_libraries(snake_shared_websocket
        PUBLIC
        snake-shared::logging
        snake-shared::threader
        Boost::system
        Boost::json
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_library(snake-shared::websocket ALIAS snake_shared_websocket)