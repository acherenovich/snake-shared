# snake-shared/websocket/CMakeLists.txt

# websocket зависит от логгера
set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

find_package(Boost REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

snake_shared_add_static_module(websocket
        DEPS
        logging
        PUBLIC_LIBS
        Boost::system
        Boost::json
        OpenSSL::SSL
        OpenSSL::Crypto
)