cmake_minimum_required(VERSION 3.24)

project(Cadjitsu
    VERSION 1.0.0
    DESCRIPTION "Parametric CAD for 3D-printable parts"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(CADJITSU_BUILD_APP   "Build the Qt application" ON)
option(CADJITSU_BUILD_TESTS "Build unit tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

find_package(Eigen3 REQUIRED NO_MODULE)
# The path tracer: Embree for rays, Open Image Denoise (optional) to denoise.
find_package(embree 4 REQUIRED CONFIG)
find_package(OpenImageDenoise 2 CONFIG QUIET)
include(CadjitsuOcct)

add_subdirectory(third_party/slvs)

add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE third_party/doctest)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE third_party/nlohmann)

add_subdirectory(src/core)

if(CADJITSU_BUILD_APP)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network Svg ShaderTools Test)
    # QRhi (<rhi/qrhi.h>) is semi-public API: its headers come with the GuiPrivate target.
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    qt_standard_project_setup()
    add_subdirectory(src/app)
endif()

if(CADJITSU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/core)
    if(CADJITSU_BUILD_APP)
        add_subdirectory(tests/app)
    endif()
endif()
