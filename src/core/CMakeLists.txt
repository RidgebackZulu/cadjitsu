# cadcore: the UI-free CAD kernel (document, features, sketch, geometry, export).
# Must never depend on Qt so it can be tested headless.

file(GLOB_RECURSE CADCORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(cadcore STATIC ${CADCORE_SOURCES})
target_include_directories(cadcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadcore
    PUBLIC  Cadjitsu::OCCT nlohmann_json
    PRIVATE slvs Eigen3::Eigen)
# The version the app reports: the project version, or a CI build's label
# ("1.0.1-dev.52"; see scripts/version_label.sh).
set(CADJITSU_BUILD_LABEL "${PROJECT_VERSION}" CACHE STRING "Version label the app reports")
target_compile_definitions(cadcore PUBLIC
    CADJITSU_VERSION="${CADJITSU_BUILD_LABEL}")
set_target_properties(cadcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT MSVC)
    target_compile_options(cadcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
