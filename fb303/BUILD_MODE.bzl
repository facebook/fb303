""" build mode definitions for fb303 """

load("@fbcode//:BUILD_MODE.bzl", get_parent_modes = "get_empty_modes")
load("@fbcode_macros//build_defs:create_build_mode.bzl", "extend_build_modes")

_modes = extend_build_modes(
    get_parent_modes(),
    cxx_modular_headers = True,
)

def get_modes():
    """ Return modes for this file """
    return _modes
