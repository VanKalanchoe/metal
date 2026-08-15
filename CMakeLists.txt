cmake_minimum_required(VERSION 3.25)

project(SDL3Metal
VERSION 1.0
LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    enable_language(OBJCXX)
endif()
# ============================================================

# SDL3

# ============================================================

include(FetchContent)

FetchContent_Declare(
SDL3
GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
GIT_TAG release-3.2.0
GIT_SHALLOW TRUE
)

set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3)

# ============================================================
# GLM
# ============================================================

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glm)

# ============================================================
# Slang
# ============================================================

set(SLANG_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/slang"
)

set(SLANG_INCLUDE_DIR
    "${SLANG_ROOT}/include"
)

if(WIN32)

    add_library(Slang::slang SHARED IMPORTED GLOBAL)

    set(SLANG_LIB
        "${SLANG_ROOT}/lib/slang.lib"
    )

    set_target_properties(Slang::slang PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${SLANG_INCLUDE_DIR}"
    )

elseif(APPLE)

    add_library(Slang::slang SHARED IMPORTED GLOBAL)

    set(SLANG_LIB
        "${SLANG_ROOT}/lib/libslang.dylib"
    )

    set_target_properties(Slang::slang PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${SLANG_INCLUDE_DIR}"
    )

endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    # Add the specific backends matching your engine tech!
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp

    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
if(APPLE)
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_metal4.mm
    )
endif()
# ============================================================
# Application Sources
# ============================================================

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
)

# Force mtl_renderer.cpp to compile as Objective-C++ on macOS without renaming on disk
if(APPLE)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mtl_renderer.cpp"
        PROPERTIES LANGUAGE OBJCXX
    )
endif()

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    ${IMGUI_SOURCES}
)

if(APPLE)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH
            "${CMAKE_CURRENT_SOURCE_DIR}/slang/lib"
    )

endif()


file(GLOB_RECURSE METAL_SHADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.metal"
)

set_source_files_properties(
    ${METAL_SHADERS}
    PROPERTIES
        HEADER_FILE_ONLY TRUE
)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${METAL_SHADERS}
)

# Keep the physical shader folder visible in Xcode.
source_group(
    "Shaders"
    FILES
        ${METAL_SHADERS}
)

# Make Xcode groups follow the physical directory structure
foreach(SOURCE_FILE IN LISTS PROJECT_SOURCES)

    get_filename_component(SOURCE_PATH "${SOURCE_FILE}" PATH)
    file(RELATIVE_PATH SOURCE_GROUP
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SOURCE_PATH}"
    )

    if(SOURCE_GROUP STREQUAL "")
        set(SOURCE_GROUP "src")
    else()
        set(SOURCE_GROUP "src/${SOURCE_GROUP}")
    endif()

    string(REPLACE "/" "\\" SOURCE_GROUP "${SOURCE_GROUP}")

    source_group("${SOURCE_GROUP}" FILES "${SOURCE_FILE}")

endforeach()

# ============================================================

# metal-cpp

# ============================================================

target_include_directories(${PROJECT_NAME}
PRIVATE
"${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/stb"
"${IMGUI_DIR}"
"${IMGUI_DIR}/backends"
)

# ============================================================

# Apple Frameworks

# ============================================================

if(APPLE)

find_library(METAL_FRAMEWORK
    Metal
    REQUIRED
)

find_library(QUARTZCORE_FRAMEWORK
    QuartzCore
    REQUIRED
)

find_library(FOUNDATION_FRAMEWORK
    Foundation
    REQUIRED
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        SDL3::SDL3
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        Slang::slang
        glm::glm
)

endif()
