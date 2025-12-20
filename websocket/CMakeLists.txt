# snake-shared/websocket/CMakeLists.txt

# websocket зависит от логгера
set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

find_package(Boost REQUIRED COMPONENTS system json)
find_package(OpenSSL REQUIRED)

set(WSOCK_LIBS "")
if (WIN32)
    # WSAStartup / sockets
    list(APPEND WSOCK_LIBS ws2_32)
    # AcceptEx / GetAcceptExSockaddrs
    list(APPEND WSOCK_LIBS mswsock)
    # иногда нужно для asio/сетевых утилит
    list(APPEND WSOCK_LIBS iphlpapi)
endif()

snake_shared_add_static_module(websocket
        DEPS
        logging
        PUBLIC_LIBS
        Boost::system
        Boost::json
        OpenSSL::SSL
        OpenSSL::Crypto
        ${WSOCK_LIBS}
)