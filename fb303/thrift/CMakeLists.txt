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

add_fbthrift_library(
  fb303_thrift
  fb303_core.thrift
  LANGUAGES cpp py
  THRIFT_INCLUDE_DIR "${INCLUDE_INSTALL_DIR}/thrift-files"
  SERVICES
    BaseService
)

install(
  TARGETS fb303_thrift_cpp
  EXPORT fb303-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
  PUBLIC_HEADER DESTINATION
    "$<TARGET_PROPERTY:fb303_thrift_cpp,HEADER_INSTALL_DIR>"
)
install(
  TARGETS fb303_thrift_cpp.thrift_includes
  EXPORT fb303-exports
)
install(
  FILES fb303_core.thrift
  DESTINATION "$<TARGET_PROPERTY:fb303_thrift_cpp,THRIFT_INSTALL_DIR>"
)

install_fb_python_library(fb303_thrift_py EXPORT fb303-exports)
install(
  TARGETS fb303_thrift_py.thrift_includes
  EXPORT fb303-exports
)
