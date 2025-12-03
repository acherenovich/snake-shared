add_library(snake_shared_threader STATIC
    src/threader.cpp
    src/threader.hpp
    interface/threader.hpp
)

target_compile_features(snake_shared_threader PUBLIC cxx_std_23)

target_include_directories(snake_shared_threader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(snake-shared::threader ALIAS snake_shared_threader)