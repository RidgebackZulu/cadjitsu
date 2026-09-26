file(GLOB CADLY_APP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
qt_add_executable(cadly_app_tests ${CADLY_APP_TEST_SOURCES})
target_link_libraries(cadly_app_tests PRIVATE cadly_ui Qt6::Test)
set_target_properties(cadly_app_tests PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

# On Linux the app tests need an X server; run them through the Xvfb wrapper.
if(UNIX AND NOT APPLE)
    set(_cadly_app_test_cmd ${CMAKE_SOURCE_DIR}/scripts/run_xvfb.sh $<TARGET_FILE:cadly_app_tests>)
else()
    set(_cadly_app_test_cmd $<TARGET_FILE:cadly_app_tests>)
endif()
add_test(NAME cadly_app_tests COMMAND ${_cadly_app_test_cmd})
set_tests_properties(cadly_app_tests PROPERTIES LABELS app TIMEOUT 600)
