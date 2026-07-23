add_library(ospr_core STATIC
    image.cpp
    keyframe.cpp
    render.cpp
    scene.cpp
    script.cpp
)

target_include_directories(ospr_core PUBLIC "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(ospr_core
    PUBLIC  ospray::ospray
    PRIVATE ospr_json ospr_stb
)
