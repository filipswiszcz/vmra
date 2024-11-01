cmake_minimum_required(VERSION 3.20)

project(vmra VERSION 0.0.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_FLAGS "-DLOG_USE_COLOR")

# collect all executables
set(VMRA_UTIL
    src/util/log.c
)
set(SOURCES
    ${VMRA_UTIL}
    src/main.c
)

add_executable(vmra ${SOURCES})

# collect all headers
target_include_directories(vmra PUBLIC include)