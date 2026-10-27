cmake_minimum_required(VERSION 3.24)
project(ptrack VERSION 0.1.0 LANGUAGES CXX)

# Working name "ptrack". Engine first: core (no I/O deps) -> io -> cli / plugin / app.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PTRACK_BUILD_TESTS "Build unit and accuracy tests" ON)
option(PTRACK_BUILD_CLI "Build the ptrack command-line tool" ON)
option(PTRACK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Any Eigen from 3.4 on (Homebrew and vcpkg ship 5.x, whose config rejects a 3.x request).
find_package(Eigen3 REQUIRED NO_MODULE)
if(Eigen3_VERSION VERSION_LESS 3.4)
  message(FATAL_ERROR "Eigen 3.4 or newer is required (found ${Eigen3_VERSION})")
endif()
find_package(OpenEXR 3 REQUIRED CONFIG)
find_package(nlohmann_json 3.10 REQUIRED CONFIG)

function(ptrack_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(PTRACK_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(PTRACK_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_subdirectory(core)
add_subdirectory(io)

if(PTRACK_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(PTRACK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
