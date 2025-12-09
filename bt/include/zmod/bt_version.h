/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bt_version.h
 * @brief Zmod BT module version definitions
 */

#ifndef ZMOD_BT_VERSION_H
#define ZMOD_BT_VERSION_H

#include <zephyr/sys/util_macro.h>

#define ZMOD_BT_VERSION_MAJOR 1
#define ZMOD_BT_VERSION_MINOR 0
#define ZMOD_BT_VERSION_PATCH 0

#define ZMOD_BT_VERSION_STRING                                                                     \
    STRINGIFY(ZMOD_BT_VERSION_MAJOR)                                                               \
    "." STRINGIFY(ZMOD_BT_VERSION_MINOR) "." STRINGIFY(ZMOD_BT_VERSION_PATCH)
#endif /* ZMOD_BT_VERSION_H */
