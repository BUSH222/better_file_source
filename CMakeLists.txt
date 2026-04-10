cmake_minimum_required(VERSION 3.13)
project(cs8_file_source)

set(SDRPP_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDRPlusPlus" CACHE PATH "path to sdrpp core")

file(GLOB SRC "src/*.cpp")

include("${SDRPP_CORE_ROOT}/sdrpp_module.cmake")

# Explicitly append the core and imgui paths here
target_include_directories(${PROJECT_NAME} PRIVATE 
    "src/"
    "${SDRPP_CORE_ROOT}/core/src/"
    "${SDRPP_CORE_ROOT}/core/src/imgui/"
)

target_link_libraries(${PROJECT_NAME} PRIVATE volk)