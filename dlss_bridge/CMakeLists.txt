# SPDX-FileCopyrightText: Copyright 2026 IFreemz
# SPDX-License-Identifier: MIT
#
# shadps4_dlss.dll links the NVIDIA DLSS SDK. It is built separately from shadPS4:
#   cmake -S dlss_bridge -B build-dlss -DDLSS_SDK_ROOT=<path to DLSS SDK> -DVULKAN_HEADERS=<path>
#   cmake --build build-dlss --config Release

cmake_minimum_required(VERSION 3.20)
project(shadps4_dlss LANGUAGES CXX)

set(DLSS_SDK_ROOT "" CACHE PATH "NVIDIA DLSS SDK root (contains include/ and lib/)")
set(VULKAN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/../externals/vulkan-headers/include"
    CACHE PATH "Vulkan-Headers include directory")

if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The DLSS bridge is Windows x64 only")
endif()
if (NOT EXISTS "${DLSS_SDK_ROOT}/include/nvsdk_ngx_vk.h")
    message(FATAL_ERROR "Set DLSS_SDK_ROOT to the NVIDIA DLSS SDK (github.com/NVIDIA/DLSS)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# nvsdk_ngx_s.lib is the static-CRT (/MT) variant: no Visual C++ runtime install needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(shadps4_dlss SHARED shadps4_dlss_bridge.cpp shadps4_dlss_bridge.h)
target_include_directories(shadps4_dlss PRIVATE "${DLSS_SDK_ROOT}/include" "${VULKAN_HEADERS}")
target_link_libraries(shadps4_dlss PRIVATE
                      "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
target_compile_definitions(shadps4_dlss PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
