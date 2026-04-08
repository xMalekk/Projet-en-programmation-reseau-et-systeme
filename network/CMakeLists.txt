cmake_minimum_required(VERSION 3.21)
project(medievail_net LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MEDIEVAIL_ENABLE_WARNINGS "Enable compiler warnings" ON)

set(MEDIEVAIL_NET_CORE_SOURCES
    src/core/app.c
    src/core/log.c
    src/ipc/udp_local.c
)

add_library(medievail_net_core )
target_include_directories(medievail_net_core PUBLIC /include)

if(MEDIEVAIL_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(medievail_net_core PRIVATE /W4 /permissive-)
    else()
        target_compile_options(medievail_net_core PRIVATE -Wall -Wextra -pedantic)
    endif()
endif()

add_executable(medievail_net src/daemon_stub.c)
target_link_libraries(medievail_net PRIVATE medievail_net_core)

if(UNIX AND NOT APPLE)
    target_link_libraries(medievail_net_core PRIVATE rt)
endif()
