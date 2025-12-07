# snake-shared/crypt/CMakeLists.txt

# crypt зависит от логгера, utils и coroutine
set(VSHARED_ENABLE_LOGGING   ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_UTILS     ON CACHE BOOL "" FORCE)
set(VSHARED_ENABLE_COROUTINE ON CACHE BOOL "" FORCE)

find_package(OpenSSL REQUIRED)

snake_shared_add_static_module(crypt
        DEPS
        logging
        utils
        coroutine
        PUBLIC_LIBS
        OpenSSL::SSL
        OpenSSL::Crypto
)