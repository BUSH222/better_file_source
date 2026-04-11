cmake_minimum_required(VERSION 3.13)
project(better_file_source)

set(SDRPP_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDRPlusPlus" CACHE PATH "path to sdrpp core")
set(SDRPP_CORE_LIB_DIR "" CACHE PATH "path to the built sdrpp_core import library directory")
set(SDRPP_CORE_DLL_DIR "" CACHE PATH "path to the built sdrpp_core runtime library directory")

file(GLOB SRC "src/*.cpp")

# When building out-of-tree on Windows CI, bind the module link against the
# exact sdrpp_core import library produced by the core build.
if (MSVC AND SDRPP_CORE_LIB_DIR AND NOT TARGET sdrpp_core)
    set(_SDRPP_CORE_IMPLIB "${SDRPP_CORE_LIB_DIR}/sdrpp_core.lib")
    if (NOT EXISTS "${_SDRPP_CORE_IMPLIB}")
        message(FATAL_ERROR "sdrpp_core.lib not found at ${_SDRPP_CORE_IMPLIB}")
    endif ()

    add_library(sdrpp_core SHARED IMPORTED GLOBAL)
    set_target_properties(sdrpp_core PROPERTIES
        IMPORTED_IMPLIB "${_SDRPP_CORE_IMPLIB}"
        IMPORTED_IMPLIB_RELEASE "${_SDRPP_CORE_IMPLIB}"
    )

    if (SDRPP_CORE_DLL_DIR)
        set(_SDRPP_CORE_DLL "${SDRPP_CORE_DLL_DIR}/sdrpp_core.dll")
        if (EXISTS "${_SDRPP_CORE_DLL}")
            set_target_properties(sdrpp_core PROPERTIES
                IMPORTED_LOCATION "${_SDRPP_CORE_DLL}"
                IMPORTED_LOCATION_RELEASE "${_SDRPP_CORE_DLL}"
            )
        endif ()
    endif ()
endif ()

include("${SDRPP_CORE_ROOT}/sdrpp_module.cmake")

# Explicitly append the core and imgui paths here
target_include_directories(${PROJECT_NAME} PRIVATE 
    "src/"
    "${SDRPP_CORE_ROOT}/core/src/"
    "${SDRPP_CORE_ROOT}/core/src/imgui/"
)

# On Windows CI, SDR++ core headers pull GNU Radio VOLK headers from PothosSDR.
if (MSVC)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "C:/Program Files/PothosSDR/include"
        "C:/vcpkg/installed/x64-windows/include"
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        "C:/Program Files/PothosSDR/lib"
    )

    if (SDRPP_CORE_LIB_DIR)
        target_link_directories(${PROJECT_NAME} PRIVATE
            "${SDRPP_CORE_LIB_DIR}"
        )
    endif ()
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE volk)