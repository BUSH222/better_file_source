cmake_minimum_required(VERSION 3.13)
project(cs8_file_source)

set(SDRPP_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDRPlusPlus" CACHE PATH "path to sdrpp core")

file(GLOB SRC "src/*.cpp")

include("${SDRPP_CORE_ROOT}/sdrpp_module.cmake")

target_include_directories(${PROJECT_NAME} PRIVATE "src/")

target_link_libraries(${PROJECT_NAME} PRIVATE volk)