add_executable(ospr_preview main.cpp)
target_link_libraries(ospr_preview PRIVATE ospr_core ospr_imgui)

find_package(OpenGL REQUIRED)
target_link_libraries(ospr_preview PRIVATE OpenGL::GL)
