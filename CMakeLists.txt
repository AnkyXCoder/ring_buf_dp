# Copyright (c) 2026 Ankit Modi
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_RING_BUF_DP)
  zephyr_include_directories(include)
  add_subdirectory(src)
endif()
