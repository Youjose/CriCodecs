cmake_minimum_required(VERSION 4.2)
project(CriCodecs LANGUAGES CXX)

option(CRICODECS_BUILD_PYTHON "Build the Python bindings" OFF)
option(CRICODECS_BUILD_CLI "Build the shared CriCodecs CLI executable" ON)

set(CRICODECS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(${CMAKE_CURRENT_SOURCE_DIR}/cricodecs_library.cmake)

cricodecs_add_library(CriCodecs)

if(CRICODECS_BUILD_CLI)
    add_executable(cricodecs_cli
        CriCodecs/src/cli/cricodecs_cli_main.cpp
    )
    target_link_libraries(cricodecs_cli PRIVATE CriCodecs)
    target_compile_features(cricodecs_cli PRIVATE cxx_std_23)
    set_target_properties(cricodecs_cli PROPERTIES
        OUTPUT_NAME cricodecs
    )
    if(NOT CRICODECS_BUILD_PYTHON)
        install(TARGETS cricodecs_cli RUNTIME DESTINATION .)
        if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
            install(
                FILES $<TARGET_RUNTIME_DLLS:cricodecs_cli>
                DESTINATION .
                OPTIONAL
            )
        endif()
    endif()
endif()

if(CRICODECS_BUILD_PYTHON)
    add_subdirectory(PyCriCodecs)
endif()
