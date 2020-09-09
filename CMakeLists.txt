# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.8)

project("fb303" LANGUAGES CXX C)

if (NOT DEFINED CPACK_GENERATOR)
  set(CPACK_GENERATOR "RPM")
endif()
include(CPack)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(INCLUDE_INSTALL_DIR include CACHE STRING
    "The subdirectory where header files should be installed")
set(LIB_INSTALL_DIR lib CACHE STRING
    "The subdirectory where libraries should be installed")
set(CMAKE_INSTALL_DIR lib/cmake/fb303 CACHE STRING
    "The subdirectory where CMake package config files should be installed")

# CMake include directories
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

find_package(Gflags REQUIRED)

find_package(Glog MODULE REQUIRED)

find_package(folly CONFIG REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(fmt CONFIG REQUIRED)
find_package(FBThrift CONFIG REQUIRED)
find_package(fizz CONFIG REQUIRED)
find_package(wangle CONFIG REQUIRED)

include(FBThriftLibrary)

add_subdirectory(fb303/thrift)

file(GLOB fb303_SOURCES fb303/*.cpp fb303/detail/*.cpp)
file(GLOB fb303_HEADERS fb303/*.h fb303/detail/*.h)

install(
  DIRECTORY fb303
  DESTINATION ${INCLUDE_INSTALL_DIR}
  FILES_MATCHING PATTERN *.h
)

add_library(fb303 ${fb303_SOURCES} ${fb303_HEADERS})

target_include_directories(fb303 PUBLIC
  ${GFLAGS_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIR}
  ${DOUBLE_CONVERSION_INCLUDE_DIR}
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

target_link_libraries(fb303
  fb303_thrift_cpp
  Folly::folly
  FBThrift::thrift
)

install(
  TARGETS fb303
  EXPORT fb303-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

# Install CMake package configuration files for fb303
include(CMakePackageConfigHelpers)
configure_package_config_file(
  CMake/fb303-config.cmake.in
  fb303-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
  PATH_VARS
    INCLUDE_INSTALL_DIR
    CMAKE_INSTALL_DIR
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/fb303-config.cmake
  DESTINATION ${CMAKE_INSTALL_DIR}
)
install(
  EXPORT fb303-exports
  FILE fb303-targets.cmake
  NAMESPACE fb303::
  DESTINATION ${CMAKE_INSTALL_DIR}
)
