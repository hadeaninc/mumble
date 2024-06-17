# Based on the template provided with the Mumble Plugin C++ Wrapper library:
# Copyright 2020 The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

cmake_minimum_required(VERSION 3.0)

cmake_policy(SET CMP0069 NEW)

set(version "1.0.0")

project(VoiceCapturePlugin
	VERSION "${version}"
	DESCRIPTION "Voice capture plugin for Mumble"
	LANGUAGES "CXX"
)

# C++17 or later is required in order to use the wrapper.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# In order to use additional parts of the plugin interface,
# you have to enable the respective option,
# e.g. set(server-event-callbacks ON CACHE BOOL "" FORCE)
set(all-features ON CACHE BOOL "" FORCE)

# We need the wrapper as a dependency.
add_subdirectory(../../mumble-plugin-cpp "${CMAKE_CURRENT_BINARY_DIR}/wrapper")

add_library(voiceCapture SHARED voiceCapture.cpp)

target_include_directories(voiceCapture PRIVATE ../../mumble-plugin-cpp/include)
target_link_libraries(voiceCapture PRIVATE mumble_plugin_cpp_wrapper)
