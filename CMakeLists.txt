cmake_minimum_required(VERSION 3.23)
project(Lale LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vendored third-party modules (see vendor/ and CLAUDE.md for versions).
add_subdirectory(vendor/yoga/yoga EXCLUDE_FROM_ALL)   # target: yogacore
add_subdirectory(vendor/thorvg EXCLUDE_FROM_ALL)      # target: thorvg

# llama.cpp is a system install; the app does not call it yet.
find_package(llama CONFIG)
if(llama_FOUND)
    message(STATUS "Found system llama.cpp")
else()
    message(STATUS "System llama.cpp not found; continuing without it")
endif()

add_executable(lale WIN32 src/main.cpp)
target_link_libraries(lale PRIVATE yogacore thorvg d3d11 dxgi)
if(llama_FOUND)
    target_link_libraries(lale PRIVATE llama)
endif()
target_compile_definitions(lale PRIVATE UNICODE _UNICODE)
