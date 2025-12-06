cmake_minimum_required(VERSION 3.20)

project(snake_shared_service_loader LANGUAGES CXX)

add_library(snake_shared_service_loader INTERFACE)

target_include_directories(snake_shared_service_loader
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/interface
)

add_library(snake-shared::service_loader ALIAS snake_shared_service_loader)