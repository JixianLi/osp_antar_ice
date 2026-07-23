# Every third-party dependency lives under ext/ and is populated by
# scripts/fetch_ext.sh. Nothing here may fall back to a system package.

set(OSPR_EXT "${CMAKE_SOURCE_DIR}/ext")

if(NOT EXISTS "${OSPR_EXT}/ospray/include/ospray/ospray.h")
    message(FATAL_ERROR
        "ext/ is not populated. Run ./scripts/fetch_ext.sh from the project root.")
endif()

find_package(ospray ${OSPR_OSPRAY_VERSION} REQUIRED
    PATHS "${OSPR_EXT}/ospray" NO_DEFAULT_PATH)

# The binary release ships rkcommon as a shared library but not its headers, so
# ospray_cpp is used standalone and ospr/math.h registers our own vector types.
set(OSPR_OSPRAY_LIB_DIR "${OSPR_EXT}/ospray/lib" CACHE INTERNAL "")

add_library(ospr_json INTERFACE)
target_include_directories(ospr_json SYSTEM INTERFACE "${OSPR_EXT}/json")

add_library(ospr_stb INTERFACE)
target_include_directories(ospr_stb SYSTEM INTERFACE "${OSPR_EXT}/stb")

add_library(ospr_pugixml STATIC "${OSPR_EXT}/pugixml/pugixml.cpp")
target_include_directories(ospr_pugixml SYSTEM PUBLIC "${OSPR_EXT}/pugixml")

add_library(ospr_miniz STATIC "${OSPR_EXT}/miniz/miniz.c")
target_include_directories(ospr_miniz SYSTEM PUBLIC "${OSPR_EXT}/miniz")
set_target_properties(ospr_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
