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

# Preview-only dependencies. ospr_core must never link these: ospr_render has to
# build and run on a headless compute node with no display and no GL.
#
# The render container has no GL or X11 development packages and no reason to
# carry any, so probe for them and drop the preview rather than failing the
# whole configure. A plain `cmake` then works on both a workstation and a
# compute node without anyone having to remember a flag.
if(OSPR_BUILD_PREVIEW)
    find_package(OpenGL QUIET)
    set(OSPR_PREVIEW_SUPPORTED ${OPENGL_FOUND})
    if(UNIX AND NOT APPLE)
        find_package(X11 QUIET)
        if(NOT X11_FOUND)
            set(OSPR_PREVIEW_SUPPORTED FALSE)
        endif()
    endif()
    if(NOT OSPR_PREVIEW_SUPPORTED)
        message(STATUS
            "OpenGL/X11 not available -- skipping ospr_preview. ospr_render is unaffected.")
        set(OSPR_BUILD_PREVIEW OFF CACHE BOOL "" FORCE)
    endif()
endif()

if(OSPR_BUILD_PREVIEW)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OSPR_EXT}/glfw" "${CMAKE_BINARY_DIR}/ext/glfw" EXCLUDE_FROM_ALL)

    add_library(ospr_imgui STATIC
        "${OSPR_EXT}/imgui/imgui.cpp"
        "${OSPR_EXT}/imgui/imgui_draw.cpp"
        "${OSPR_EXT}/imgui/imgui_tables.cpp"
        "${OSPR_EXT}/imgui/imgui_widgets.cpp"
        "${OSPR_EXT}/imgui/backends/imgui_impl_glfw.cpp"
        "${OSPR_EXT}/imgui/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(ospr_imgui SYSTEM PUBLIC
        "${OSPR_EXT}/imgui" "${OSPR_EXT}/imgui/backends")
    target_link_libraries(ospr_imgui PUBLIC glfw)
endif()
