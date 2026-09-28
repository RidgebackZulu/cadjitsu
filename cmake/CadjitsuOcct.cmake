# Locate OpenCASCADE and expose a single interface target `Cadjitsu::OCCT` with the
# toolkits Cadjitsu needs. Handles the 7.8 data-exchange toolkit rename
# (TKSTEP*/TKXDESTEP -> TKDESTEP) so the same tree builds against OCCT 7.6 - 8.x.

find_package(OpenCASCADE CONFIG REQUIRED)
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} at ${OpenCASCADE_INSTALL_PREFIX}")

set(_cadjitsu_occt_toolkits
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKBool TKShHealing TKFillet TKOffset TKFeat TKMesh
    TKCDF TKLCAF TKCAF TKXCAF TKXSBase)

if(TARGET TKDESTEP)
    list(APPEND _cadjitsu_occt_toolkits TKDESTEP)
else()
    list(APPEND _cadjitsu_occt_toolkits TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKXDESTEP)
endif()
if(TARGET TKDE)
    list(APPEND _cadjitsu_occt_toolkits TKDE)
endif()

set(_cadjitsu_occt_missing "")
foreach(tk IN LISTS _cadjitsu_occt_toolkits)
    if(NOT TARGET ${tk})
        list(APPEND _cadjitsu_occt_missing ${tk})
    endif()
endforeach()
if(_cadjitsu_occt_missing)
    message(FATAL_ERROR "OpenCASCADE is missing toolkits: ${_cadjitsu_occt_missing}")
endif()

add_library(cadjitsu_occt INTERFACE)
target_link_libraries(cadjitsu_occt INTERFACE ${_cadjitsu_occt_toolkits})
target_include_directories(cadjitsu_occt SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})
add_library(Cadjitsu::OCCT ALIAS cadjitsu_occt)
