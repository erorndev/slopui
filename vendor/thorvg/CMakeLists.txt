cmake_minimum_required(VERSION 3.23)
project(thorvg VERSION 1.1.4 LANGUAGES CXX)

option(THORVG_SIMD "Build the AVX2 raster path" ON)

set(TVG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(TVG_SOURCES
    ${TVG_SRC}/common/tvgArray.cpp
    ${TVG_SRC}/common/tvgCompressor.cpp
    ${TVG_SRC}/common/tvgMath.cpp
    ${TVG_SRC}/common/tvgStr.cpp
    ${TVG_SRC}/renderer/tvgAccessor.cpp
    ${TVG_SRC}/renderer/tvgAnimation.cpp
    ${TVG_SRC}/renderer/tvgCanvas.cpp
    ${TVG_SRC}/renderer/tvgFill.cpp
    ${TVG_SRC}/renderer/tvgInitializer.cpp
    ${TVG_SRC}/renderer/tvgLoaderMgr.cpp
    ${TVG_SRC}/renderer/tvgPaint.cpp
    ${TVG_SRC}/renderer/tvgPicture.cpp
    ${TVG_SRC}/renderer/tvgRender.cpp
    ${TVG_SRC}/renderer/tvgSaver.cpp
    ${TVG_SRC}/renderer/tvgScene.cpp
    ${TVG_SRC}/renderer/tvgShape.cpp
    ${TVG_SRC}/renderer/tvgTaskScheduler.cpp
    ${TVG_SRC}/renderer/tvgText.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwBlendOp.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwFill.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwImage.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwMemPool.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwPostEffect.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwRaster.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwRasterCore.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwRenderer.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwRle.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwShape.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwStroke.cpp
    ${TVG_SRC}/renderer/cpu_engine/tvgSwUtil.cpp
    ${TVG_SRC}/modules/raw/tvgRawLoader.cpp
    ${TVG_SRC}/modules/svg/tvgSvgBuilder.cpp
    ${TVG_SRC}/modules/svg/tvgSvgCssStyle.cpp
    ${TVG_SRC}/modules/svg/tvgSvgLoader.cpp
    ${TVG_SRC}/modules/svg/tvgSvgPath.cpp
    ${TVG_SRC}/modules/svg/tvgSvgUtil.cpp
    ${TVG_SRC}/modules/svg/tvgXmlParser.cpp
    ${TVG_SRC}/modules/sfnt/tvgSfntLoader.cpp
    ${TVG_SRC}/modules/sfnt/tvgSfntReader.cpp
    ${TVG_SRC}/modules/sfnt/tvgTtfReader.cpp
    ${TVG_SRC}/modules/sfnt/tvgOtfReader.cpp
)

if(THORVG_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(TVG_SIMD_SOURCE ${TVG_SRC}/renderer/cpu_engine/tvgSwRasterSimd.cpp)
    list(APPEND TVG_SOURCES ${TVG_SIMD_SOURCE})
    if(MSVC)
        set_source_files_properties(${TVG_SIMD_SOURCE} PROPERTIES COMPILE_OPTIONS /arch:AVX2)
    else()
        set_source_files_properties(${TVG_SIMD_SOURCE} PROPERTIES COMPILE_OPTIONS -mavx2)
    endif()
    set(THORVG_AVX_SUPPORT 1)
endif()

set(TVG_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/config)
set(TVG_CONFIG "#pragma once\n")
string(APPEND TVG_CONFIG "#define THORVG_VERSION_STRING \"${PROJECT_VERSION}\"\n")
foreach(flag
        THORVG_THREAD_SUPPORT
        THORVG_CPU_ENGINE_SUPPORT
        THORVG_PARTIAL_RENDER_SUPPORT
        THORVG_SVG_LOADER_SUPPORT
        THORVG_SFNT_LOADER_SUPPORT
        THORVG_TTF_LOADER_SUPPORT
        THORVG_OTF_LOADER_SUPPORT
        THORVG_FILE_IO_SUPPORT)
    string(APPEND TVG_CONFIG "#define ${flag} 1\n")
endforeach()
if(WIN32)
    string(APPEND TVG_CONFIG "#define WIN32_LEAN_AND_MEAN 1\n")
endif()
if(THORVG_AVX_SUPPORT)
    string(APPEND TVG_CONFIG "#define THORVG_AVX_SUPPORT 1\n")
endif()
file(CONFIGURE OUTPUT ${TVG_CONFIG_DIR}/config.h CONTENT "${TVG_CONFIG}")

add_library(thorvg STATIC ${TVG_SOURCES})
target_compile_features(thorvg PUBLIC cxx_std_14)
target_compile_definitions(thorvg PUBLIC TVG_STATIC)
if(WIN32)
    target_compile_definitions(thorvg PRIVATE NOMINMAX)
endif()
target_include_directories(thorvg
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE ${TVG_CONFIG_DIR}
            ${TVG_SRC}/common
            ${TVG_SRC}/renderer
            ${TVG_SRC}/renderer/cpu_engine
            ${TVG_SRC}/modules
            ${TVG_SRC}/modules/raw
            ${TVG_SRC}/modules/svg
            ${TVG_SRC}/modules/sfnt)
if(MSVC)
    target_compile_options(thorvg PRIVATE /W0)
endif()
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(thorvg PRIVATE Threads::Threads)
endif()
