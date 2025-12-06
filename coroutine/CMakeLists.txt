set(VSHARED_ENABLE_LOGGING ON CACHE BOOL "" FORCE)

add_library(snake_shared_coroutine STATIC
        src/coroutine.cpp
        interface/coroutine.hpp
)

target_compile_features(snake_shared_coroutine PUBLIC cxx_std_23)

target_include_directories(snake_shared_coroutine
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../logging/interface
)

target_link_libraries(snake_shared_coroutine
        PUBLIC
        snake-shared::logging
)

add_library(snake-shared::coroutine ALIAS snake_shared_coroutine)
