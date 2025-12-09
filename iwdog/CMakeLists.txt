# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod IWDOG Module

# Only compile if the feature is enabled
zephyr_library_sources_ifdef(CONFIG_ZMOD_IWDOG src/iwdog.c)

# Export headers to the whole app
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
