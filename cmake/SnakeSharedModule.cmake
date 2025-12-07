# snake-shared/cmake/SnakeSharedModule.cmake
include(CMakeParseArguments)

# Статический модуль с реализацией (src/*.cpp)
# Пример вызова:
# snake_shared_add_static_module(websocket
#     DEPS        logging coroutine
#     PUBLIC_LIBS Boost::system Boost::json OpenSSL::SSL OpenSSL::Crypto
#     PRIVATE_LIBS some_private_lib
# )
function(snake_shared_add_static_module NAME)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs DEPS PUBLIC_LIBS PRIVATE_LIBS)
    cmake_parse_arguments(SSM "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Автосбор исходников
    file(GLOB_RECURSE SNAKE_SHARED_${NAME}_SOURCES
            CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
    )

    add_library(snake_shared_${NAME} STATIC
            ${SNAKE_SHARED_${NAME}_SOURCES}
    )

    target_compile_features(snake_shared_${NAME} PUBLIC cxx_std_23)

    # Общие include-директории для всех модулей
    target_include_directories(snake_shared_${NAME}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # Зависимости от других модулей snake-shared
    foreach(dep IN LISTS SSM_DEPS)
        target_link_libraries(snake_shared_${NAME}
                PUBLIC
                snake-shared::${dep}
        )
    endforeach()

    if (SSM_PUBLIC_LIBS)
        target_link_libraries(snake_shared_${NAME}
                PUBLIC
                ${SSM_PUBLIC_LIBS}
        )
    endif()

    if (SSM_PRIVATE_LIBS)
        target_link_libraries(snake_shared_${NAME}
                PRIVATE
                ${SSM_PRIVATE_LIBS}
        )
    endif()

    add_library(snake-shared::${NAME} ALIAS snake_shared_${NAME})
endfunction()

# Header-only модуль (только interface/)
# Пример:
# snake_shared_add_header_only_module(utils)
function(snake_shared_add_header_only_module NAME)
    add_library(snake_shared_${NAME} INTERFACE)

    target_compile_features(snake_shared_${NAME}
            INTERFACE
            cxx_std_23
    )

    target_include_directories(snake_shared_${NAME}
            INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/interface
    )

    add_library(snake-shared::${NAME} ALIAS snake_shared_${NAME})
endfunction()