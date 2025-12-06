cmake_minimum_required(VERSION 3.20)

project(snake_shared_service_utils LANGUAGES CXX)

add_library(snake_shared_service_utils INTERFACE)

target_include_directories(snake_shared_service_utils
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/interface
)

add_library(snake-shared::utils ALIAS snake_shared_service_utils)