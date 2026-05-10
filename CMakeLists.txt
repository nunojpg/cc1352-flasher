cmake_minimum_required(VERSION 3.20)
project(cc1352-flasher CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -g")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -s")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -fsanitize=undefined -fno-omit-frame-pointer")
else()
    message(FATAL_ERROR "Unknown build type")
endif()
add_executable(cc1352-flasher
    main.cpp
    intel_hex.cpp
    crc32.cpp
)
