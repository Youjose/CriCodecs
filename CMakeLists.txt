cmake_minimum_required(VERSION 4.2)
project(CriCodecs LANGUAGES CXX)

option(CRICODECS_BUILD_PYTHON "Build the Python bindings" OFF)

set(CRICODECS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(${CMAKE_CURRENT_SOURCE_DIR}/cricodecs_library.cmake)

cricodecs_add_library(CriCodecs)

if(CRICODECS_BUILD_PYTHON)
    add_subdirectory(PyCriCodecs)
endif()
