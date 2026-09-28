file(GLOB CADJITSU_APP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
qt_add_executable(cadjitsu_app_tests ${CADJITSU_APP_TEST_SOURCES})
target_link_libraries(cadjitsu_app_tests PRIVATE cadjitsu_ui Qt6::Test)
target_compile_definitions(cadjitsu_app_tests PRIVATE CADJITSU_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(cadjitsu_app_tests PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

# On Linux the app tests need an X server; run them through the Xvfb wrapper.
if(UNIX AND NOT APPLE)
    set(_cadjitsu_app_test_cmd ${CMAKE_SOURCE_DIR}/scripts/run_xvfb.sh $<TARGET_FILE:cadjitsu_app_tests>)
else()
    set(_cadjitsu_app_test_cmd $<TARGET_FILE:cadjitsu_app_tests>)
endif()
add_test(NAME cadjitsu_app_tests COMMAND ${_cadjitsu_app_test_cmd})
set_tests_properties(cadjitsu_app_tests PROPERTIES LABELS app TIMEOUT 600)
