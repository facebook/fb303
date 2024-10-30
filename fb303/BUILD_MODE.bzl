""" build mode definitions for fb303 """

# Copyright (c) Meta Platforms, Inc. and affiliates.
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

load("@fbcode//:BUILD_MODE.bzl", get_parent_modes = "get_empty_modes")
load("@fbcode_macros//build_defs:create_build_mode.bzl", "extend_build_modes")

_modes = extend_build_modes(
    get_parent_modes(),
    cxx_modular_headers = True,
)

def get_modes():
    """ Return modes for this file """
    return _modes
