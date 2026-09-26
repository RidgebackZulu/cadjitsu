# Everything except main.cpp lives in a static library so app tests can link it.
file(GLOB_RECURSE CADLY_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
list(REMOVE_ITEM CADLY_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

qt_add_library(cadly_ui STATIC ${CADLY_UI_SOURCES})
target_include_directories(cadly_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadly_ui PUBLIC cadcore Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets)
if(NOT MSVC)
    target_compile_options(cadly_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

file(GLOB CADLY_SHADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
qt_add_shaders(cadly_ui "cadly_shaders"
    PREFIX "/shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    GLSL "330,300es"
    HLSL 50
    MSL 12
    FILES ${CADLY_SHADERS})

qt_add_executable(Cadly MACOSX_BUNDLE WIN32 main.cpp)
target_link_libraries(Cadly PRIVATE cadly_ui)
set_target_properties(Cadly PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.cadly.Cadly"
    MACOSX_BUNDLE_BUNDLE_NAME "Cadly"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
