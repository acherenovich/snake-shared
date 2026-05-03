cmake_minimum_required(VERSION 3.27)
project(snake-shared LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SnakeSharedModule)

# ======================================================================
# Global compile defaults for ALL snake-shared modules (important!)
# ======================================================================
set(SNAKE_SHARED_COMMON_COMPILE_OPTIONS
        -Wall -Wextra
        -Wno-unused-variable
        -Wno-unused-parameter
        -Wno-sign-compare
        -Wno-deprecated-declarations
)

# No RTTI/EXCEPTIONS hacks here! leave defaults.
# If you disable them accidentally in some module -> type_info conflicts.

# ======================================================================
# Enabled modules list
# ======================================================================
set(SNAKE_SHARED_ENABLED_MODULE_DIRS "")

file(GLOB SNAKE_SHARED_SUBDIRS
        CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/*
)

foreach(mod_dir ${SNAKE_SHARED_SUBDIRS})
    if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${mod_dir}"
            AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${mod_dir}/CMakeLists.txt")

        string(TOUPPER "${mod_dir}" MOD_UPPER)
        set(OPT_NAME "VSHARED_ENABLE_${MOD_UPPER}")

        if (NOT DEFINED ${OPT_NAME})
            option(${OPT_NAME} "Enable snake-shared module ${mod_dir}" OFF)
        endif()

        if (${OPT_NAME})
            message(STATUS "snake-shared: enabling module '${mod_dir}'")
            list(APPEND SNAKE_SHARED_ENABLED_MODULE_DIRS "${mod_dir}")
            add_subdirectory(${mod_dir})
        else()
            message(STATUS "snake-shared: module '${mod_dir}' is disabled (${OPT_NAME}=OFF)")
        endif()
    endif()
endforeach()

# ======================================================================
# Aggregator target, links all enabled modules
# ======================================================================
add_library(snake_shared_all INTERFACE)
add_library(snake-shared::all ALIAS snake_shared_all)

foreach(mod_dir ${SNAKE_SHARED_ENABLED_MODULE_DIRS})
    target_link_libraries(snake_shared_all
            INTERFACE
            "snake-shared::${mod_dir}"
    )
endforeach()
