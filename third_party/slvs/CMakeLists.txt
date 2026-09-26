# SolveSpace constraint solver (libslvs), vendored from solvespace 3.2.
# See PROVENANCE.md. Built as a static library; the original sources are unmodified,
# shim/mimalloc.h stands in for the mimalloc heap API used by the temporary arena.

add_library(slvs STATIC
    src/constrainteq.cpp
    src/entity.cpp
    src/expr.cpp
    src/system.cpp
    src/util.cpp
    src/platform/platformbase.cpp
    src/slvs/lib.cpp
)
target_compile_features(slvs PRIVATE cxx_std_17)
target_compile_definitions(slvs PRIVATE LIBRARY STATIC_LIB)
target_include_directories(slvs PRIVATE src shim)
target_include_directories(slvs SYSTEM PUBLIC include)
target_link_libraries(slvs PRIVATE Eigen3::Eigen)
set_target_properties(slvs PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(slvs PRIVATE /w)
else()
    target_compile_options(slvs PRIVATE -w)
endif()
