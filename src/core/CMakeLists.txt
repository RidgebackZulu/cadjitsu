# cadcore: the UI-free CAD kernel (document, features, sketch, geometry, export).
# Must never depend on Qt so it can be tested headless.

file(GLOB_RECURSE CADCORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(cadcore STATIC ${CADCORE_SOURCES})
target_include_directories(cadcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadcore
    PUBLIC  Cadly::OCCT nlohmann_json
    PRIVATE slvs Eigen3::Eigen)
target_compile_definitions(cadcore PUBLIC
    CADLY_VERSION="${PROJECT_VERSION}")
set_target_properties(cadcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT MSVC)
    target_compile_options(cadcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
