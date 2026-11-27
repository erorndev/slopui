cmake_minimum_required(VERSION 3.23)
project(Lale LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vendor/yoga/yoga EXCLUDE_FROM_ALL)
add_subdirectory(vendor/thorvg EXCLUDE_FROM_ALL)

set(LALE_LLAMA_PREFIX "C:/llama.cpp" CACHE PATH "Install prefix of the system llama.cpp")
list(APPEND CMAKE_PREFIX_PATH ${LALE_LLAMA_PREFIX})
find_package(llama CONFIG REQUIRED)

add_executable(lale WIN32
    src/Main.cc
    src/app/App.cc
    src/app/Settings.cc
    src/app/Theme.cc
    src/gfx/Presenter.cc
    src/llm/InferenceEngine.cc
    src/platform/FileDialog.cc
    src/platform/SystemAppearance.cc
    src/platform/Window.cc
    src/ui/Font.cc
    src/ui/Node.cc
    src/ui/Renderer.cc)
target_include_directories(lale PRIVATE src)
target_link_libraries(lale PRIVATE yogacore thorvg d3d11 dxgi dwmapi ole32 uuid shell32 llama)
target_compile_definitions(lale PRIVATE UNICODE _UNICODE)

add_custom_command(TARGET lale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lale>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBold.ttf
        $<TARGET_FILE_DIR:lale>/fonts)

file(GLOB LALE_LLAMA_RUNTIME_LIBRARIES "${LALE_LLAMA_PREFIX}/bin/*.dll")
add_custom_command(TARGET lale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LALE_LLAMA_RUNTIME_LIBRARIES} $<TARGET_FILE_DIR:lale>)
