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
  set(LALE_DEFAULT_PLATFORM win32)
else()
  set(LALE_DEFAULT_PLATFORM sdl)
endif()
set(LALE_PLATFORM ${LALE_DEFAULT_PLATFORM} CACHE STRING "Platform layer: win32 or sdl")
set_property(CACHE LALE_PLATFORM PROPERTY STRINGS win32 sdl)

if(WIN32)
  target_compile_definitions(lale PRIVATE UNICODE _UNICODE)
  if(MSVC)
    target_link_options(lale PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()

if(LALE_PLATFORM STREQUAL "sdl")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
  foreach(unused_subsystem AUDIO CAMERA JOYSTICK HAPTIC HIDAPI SENSOR POWER)
    set(SDL_${unused_subsystem} OFF CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(vendor/sdl EXCLUDE_FROM_ALL)
  target_sources(lale PRIVATE
      src/gfx/sdl/SdlPresenter.cc
      src/platform/sdl/SdlFileDialog.cc
      src/platform/sdl/SdlPlatform.cc
      src/platform/sdl/SdlSystemAppearance.cc
      src/platform/sdl/SdlWindow.cc)
  target_link_libraries(lale PRIVATE SDL3::SDL3-static)
  if(UNIX)
    target_link_libraries(lale PRIVATE ${CMAKE_DL_LIBS})
  endif()
elseif(LALE_PLATFORM STREQUAL "win32" AND WIN32)
  target_sources(lale PRIVATE
      src/gfx/d3d11/D3D11Presenter.cc
      src/platform/win32/Win32FileDialog.cc
      src/platform/win32/Win32Platform.cc
      src/platform/win32/Win32SystemAppearance.cc
      src/platform/win32/Win32Window.cc)
  target_link_libraries(lale PRIVATE d3d11 dxgi dwmapi ole32 uuid shell32)
else()
  message(FATAL_ERROR "Unknown LALE_PLATFORM: ${LALE_PLATFORM}")
endif()

add_custom_command(TARGET lale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lale>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-Italic.ttf
        ${CMAKE_SOURCE_DIR}/vendor/inter/Inter-SemiBoldItalic.ttf
        $<TARGET_FILE_DIR:lale>/fonts)

if(UNIX AND NOT APPLE)
  set_target_properties(lale PROPERTIES
      BUILD_RPATH_USE_ORIGIN ON
      INSTALL_RPATH_USE_LINK_PATH ON
      INSTALL_RPATH "$ORIGIN")
  include(GNUInstallDirs)
  install(TARGETS lale RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES
      vendor/inter/Inter-Regular.ttf
      vendor/inter/Inter-SemiBold.ttf
      vendor/inter/Inter-Italic.ttf
      vendor/inter/Inter-SemiBoldItalic.ttf
      DESTINATION ${CMAKE_INSTALL_BINDIR}/fonts)
  install(FILES assets/linux/lale.desktop
      DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES assets/linux/lale.svg
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()