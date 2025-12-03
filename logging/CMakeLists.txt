add_library(snake_shared_logging STATIC
        src/logging.cpp
        interface/logging.hpp
)

target_compile_features(snake_shared_logging PUBLIC cxx_std_23)

target_include_directories(snake_shared_logging
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
)

add_library(snake-shared::logging ALIAS snake_shared_logging)

# Если нужны зависимости – тянем их тут
# find_package(spdlog CONFIG REQUIRED)
# target_link_libraries(snake_shared_logging PUBLIC spdlog::spdlog)