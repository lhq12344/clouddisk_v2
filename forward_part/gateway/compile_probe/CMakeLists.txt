cmake_minimum_required(VERSION 3.10)
project(clouddisk_gateway_core_logic_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(core_logic_compile_probe
    core_logic_compile_probe.cc
    ../application/common/FeatureFlags.cc
)
target_include_directories(core_logic_compile_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
