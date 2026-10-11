cmake_minimum_required(VERSION 3.21)
project(Neptune VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Le cœur (core/) ne dépend jamais de JUCE : il se compile et se teste seul.
option(NEPTUNE_BUILD_PLUGIN "Construire l'adaptateur JUCE / CLAP" ON)
option(NEPTUNE_BUILD_TESTS  "Construire les tests unitaires du cœur" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)

if(NEPTUNE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NEPTUNE_BUILD_PLUGIN)
  add_subdirectory(adapter-juce)
endif()
