# Everything except main.cpp lives in a static library so app tests can link it.
file(GLOB_RECURSE CADJITSU_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
list(REMOVE_ITEM CADJITSU_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

qt_add_library(cadjitsu_ui STATIC ${CADJITSU_UI_SOURCES})
target_include_directories(cadjitsu_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadjitsu_ui PUBLIC cadcore Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Svg)
if(NOT MSVC)
    target_compile_options(cadjitsu_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

file(GLOB CADJITSU_SHADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
qt_add_shaders(cadjitsu_ui "cadjitsu_shaders"
    PREFIX "/shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    GLSL "330,300es"
    HLSL 50
    MSL 12
    FILES ${CADJITSU_SHADERS})

# The icon set (made by scripts/icons/gen_icons.py).
file(GLOB CADJITSU_ICON_SVGS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)
qt_add_resources(cadjitsu_ui "cadjitsu_icons"
    PREFIX "/icons"
    BASE ${PROJECT_SOURCE_DIR}/resources/icons
    FILES ${CADJITSU_ICON_SVGS})

qt_add_executable(Cadjitsu MACOSX_BUNDLE WIN32 main.cpp)
target_link_libraries(Cadjitsu PRIVATE cadjitsu_ui)
set_target_properties(Cadjitsu PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.cadjitsu.Cadjitsu"
    MACOSX_BUNDLE_BUNDLE_NAME "Cadjitsu"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Cadjitsu is free software under the GNU GPL v3"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)

# Draws the app icon to PNG files (QtGui only, no application object).
add_executable(cadjitsu_icongen ${PROJECT_SOURCE_DIR}/src/tools/icongen.cpp ui/AppIcon.cpp ui/AppIcon.h)
target_include_directories(cadjitsu_icongen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cadjitsu_icongen PRIVATE Qt6::Gui)

# macOS: the bundle's Cadjitsu.icns, made from those PNGs with iconutil.
if(APPLE)
    set(CADJITSU_ICONSET ${CMAKE_CURRENT_BINARY_DIR}/Cadjitsu.iconset)
    set(CADJITSU_ICNS ${CMAKE_CURRENT_BINARY_DIR}/Cadjitsu.icns)
    add_custom_command(OUTPUT ${CADJITSU_ICNS}
        COMMAND cadjitsu_icongen ${CADJITSU_ICONSET}
        COMMAND iconutil -c icns ${CADJITSU_ICONSET} -o ${CADJITSU_ICNS}
        DEPENDS cadjitsu_icongen
        COMMENT "Drawing Cadjitsu.icns"
        VERBATIM)
    target_sources(Cadjitsu PRIVATE ${CADJITSU_ICNS})
    set_source_files_properties(${CADJITSU_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Cadjitsu PROPERTIES MACOSX_BUNDLE_ICON_FILE Cadjitsu.icns)
endif()
