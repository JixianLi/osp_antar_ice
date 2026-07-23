cmake_minimum_required(VERSION 3.24)
project(osp_renderer VERSION 0.1.0 LANGUAGES C CXX)

set(OSPR_OSPRAY_VERSION 3.2.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(cmake/ospr_ext.cmake)

# OSPRay dlopens libospray_module_cpu at device-init time, so the loader must be
# able to find ext/ospray/lib from the executable itself.
set(CMAKE_BUILD_RPATH "${OSPR_OSPRAY_LIB_DIR}")

add_subdirectory(src/core)
add_subdirectory(src/render)
