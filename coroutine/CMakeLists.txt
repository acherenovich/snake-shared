# snake-shared/coroutine/CMakeLists.txt

# coroutine зависит от логгера
set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

snake_shared_add_static_module(coroutine
        DEPS logging
)