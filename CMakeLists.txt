cmake_minimum_required(VERSION 3.23)
project(Lale LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vendor/yoga/yoga EXCLUDE_FROM_ALL)
add_subdirectory(vendor/thorvg EXCLUDE_FROM_ALL)

find_package(llama CONFIG REQUIRED)

add_executable(lale WIN32
    src/Main.cc
    src/app/App.cc
    src/app/Conversation.cc
    src/app/DataFile.cc
    src/app/Settings.cc
    src/app/SplashScreen.cc
    src/app/Theme.cc
    src/llm/InferenceEngine.cc
    src/ui/Font.cc
    src/ui/Markdown.cc
    src/ui/Node.cc
    src/ui/Renderer.cc
    src/ui/RichText.cc
    src/ui/TextEditing.cc)
target_include_directories(lale PRIVATE src)
target_link_libraries(lale PRIVATE yogacore thorvg llama)

if(WIN32)
  target_sources(lale PRIVATE
      src/gfx/d3d11/D3D11Presenter.cc
      src/platform/win32/Win32FileDialog.cc
      src/platform/win32/Win32Platform.cc
      src/platform/win32/Win32SystemAppearance.cc
      src/platform/win32/Win32Window.cc)
  target_link_libraries(lale PRIVATE d3d11 dxgi dwmapi ole32 uuid shell32)
  target_compile_definitions(lale PRIVATE UNICODE _UNICODE)
  if(MSVC)
    target_link_options(lale PRIVATE /ENTRY:mainCRTStartup)
  endif()
else()
  message(FATAL_ERROR "Lale has no platform layer for this system yet.")
endif()

add_custom_command(TARGET lale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lale>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Italic.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBoldItalic.ttf
        $<TARGET_FILE_DIR:lale>/fonts)