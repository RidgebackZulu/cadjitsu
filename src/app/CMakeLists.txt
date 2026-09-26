# Everything except main.cpp lives in a static library so app tests can link it.
file(GLOB_RECURSE CADLY_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
list(REMOVE_ITEM CADLY_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

qt_add_library(cadly_ui STATIC ${CADLY_UI_SOURCES})
target_include_directories(cadly_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadly_ui PUBLIC cadcore Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Svg)
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

# The icon set (made by scripts/icons/gen_icons.py).
file(GLOB CADLY_ICON_SVGS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)
qt_add_resources(cadly_ui "cadly_icons"
    PREFIX "/icons"
    BASE ${PROJECT_SOURCE_DIR}/resources/icons
    FILES ${CADLY_ICON_SVGS})

qt_add_executable(Cadly MACOSX_BUNDLE WIN32 main.cpp)
target_link_libraries(Cadly PRIVATE cadly_ui)
set_target_properties(Cadly PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.cadly.Cadly"
    MACOSX_BUNDLE_BUNDLE_NAME "Cadly"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Cadly is free software under the GNU GPL v3"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)

# Draws the app icon to PNG files (QtGui only, no application object).
add_executable(cadly_icongen ${PROJECT_SOURCE_DIR}/src/tools/icongen.cpp ui/AppIcon.cpp ui/AppIcon.h)
target_include_directories(cadly_icongen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadly_icongen PRIVATE Qt6::Gui)

# macOS: the bundle's Cadly.icns, made from those PNGs with iconutil.
if(APPLE)
    set(CADLY_ICONSET ${CMAKE_CURRENT_BINARY_DIR}/Cadly.iconset)
    set(CADLY_ICNS ${CMAKE_CURRENT_BINARY_DIR}/Cadly.icns)
    add_custom_command(OUTPUT ${CADLY_ICNS}
        COMMAND cadly_icongen ${CADLY_ICONSET}
        COMMAND iconutil -c icns ${CADLY_ICONSET} -o ${CADLY_ICNS}
        DEPENDS cadly_icongen
        COMMENT "Drawing Cadly.icns"
        VERBATIM)
    target_sources(Cadly PRIVATE ${CADLY_ICNS})
    set_source_files_properties(${CADLY_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Cadly PROPERTIES MACOSX_BUNDLE_ICON_FILE Cadly.icns)
endif()
