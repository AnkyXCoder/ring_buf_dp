# Copyright (c) 2026 Ankit Modi
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ring_buf_dp_unit)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_RING_BUF_DP_SHELL app PRIVATE src/shell_test.c)
