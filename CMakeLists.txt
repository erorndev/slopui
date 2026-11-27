cmake_minimum_required(VERSION 3.23)
project(Lale LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vendor/yoga/yoga EXCLUDE_FROM_ALL)
add_subdirectory(vendor/thorvg EXCLUDE_FROM_ALL)

find_package(llama CONFIG)
if(llama_FOUND)
    message(STATUS "Found system llama.cpp")
else()
    message(STATUS "System llama.cpp not found; continuing without it")
endif()

add_executable(lale WIN32
    src/Main.cc
    src/app/App.cc
    src/app/Theme.cc
    src/gfx/Presenter.cc
    src/platform/SystemAppearance.cc
    src/platform/Window.cc
    src/ui/Font.cc
    src/ui/Node.cc
    src/ui/Renderer.cc)
target_include_directories(lale PRIVATE src)
target_link_libraries(lale PRIVATE yogacore thorvg d3d11 dxgi dwmapi)
if(llama_FOUND)
    target_link_libraries(lale PRIVATE llama)
endif()
target_compile_definitions(lale PRIVATE UNICODE _UNICODE)

add_custom_command(TARGET lale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lale>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBold.ttf
        $<TARGET_FILE_DIR:lale>/fonts)
