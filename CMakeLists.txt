cmake_minimum_required(VERSION 3.25)

project(SDL3Metal
VERSION 1.0
LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# ============================================================
# Application Sources
# ============================================================

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
)

if(APPLE)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH
            "${CMAKE_CURRENT_SOURCE_DIR}/slang/lib"
    )

endif()

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
)

endif()
