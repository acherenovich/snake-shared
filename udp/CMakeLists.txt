# snake-shared/udp/CMakeLists.txt

set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

find_package(Boost REQUIRED COMPONENTS json)

set(UDP_SOCK_LIBS "")
if (WIN32)
    list(APPEND UDP_SOCK_LIBS ws2_32)
    list(APPEND UDP_SOCK_LIBS iphlpapi)
endif()

snake_shared_add_static_module(udp
        DEPS
        logging
        PUBLIC_LIBS
        Boost::json
        ${UDP_SOCK_LIBS}
)
