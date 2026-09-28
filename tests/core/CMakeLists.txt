file(GLOB CADCORE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(cadcore_tests ${CADCORE_TEST_SOURCES})
target_link_libraries(cadcore_tests PRIVATE cadcore slvs doctest Eigen3::Eigen)
target_compile_definitions(cadcore_tests PRIVATE
    CADJITSU_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
    CADJITSU_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/test-out")

add_test(NAME cadcore_tests COMMAND cadcore_tests)
set_tests_properties(cadcore_tests PROPERTIES LABELS core TIMEOUT 600)
