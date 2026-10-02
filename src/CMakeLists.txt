# Copyright (c) 2026 Ankit Modi
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(ring_buf_dp.c)
zephyr_library_sources_ifdef(CONFIG_RING_BUF_DP_SHELL shell_ring_buf_dp.c)
