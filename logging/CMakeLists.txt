# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod Logging Module

zephyr_library_sources_ifdef(CONFIG_ZMOD_LOG_STORAGE src/log_storage.c)
zephyr_library_sources_ifdef(CONFIG_ZMOD_LOG_STORAGE src/flash_log_backend.c)

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
