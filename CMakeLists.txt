# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod Modules top-level CMakeLists.txt
zephyr_library()

# IWDOG module
if(CONFIG_ZMOD_IWDOG)
  add_subdirectory(iwdog)
endif()

# Config module
if(CONFIG_ZMOD_CONFIG)
  add_subdirectory(config)
endif()

# BT module
if(CONFIG_ZMOD_BT)
  add_subdirectory(bt)
endif()

# BT module
if(CONFIG_ZMOD_LOG_STORAGE)
  add_subdirectory(logging)
endif()