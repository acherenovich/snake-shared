# snake-shared/crypt/CMakeLists.txt

# crypt зависит от логгера, utils и coroutine
set(VSHARED_ENABLE_LOGGING   ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_UTILS     ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_COROUTINE ON CACHE BOOL "" FORCE)

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS json)

snake_shared_add_static_module(crypt
        DEPS
        logging
        utils
        coroutine
        PUBLIC_LIBS
        Boost::json
        OpenSSL::SSL
        OpenSSL::Crypto
)
