/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file iwdog_version.h
 * @brief Zmod Internal watchdog version definitions
 */

#ifndef ZMOD_IWDOG_VERSION_H
#define ZMOD_IWDOG_VERSION_H

#define ZMOD_IWDOG_VERSION_MAJOR 1
#define ZMOD_IWDOG_VERSION_MINOR 0
#define ZMOD_IWDOG_VERSION_PATCH 0

#define ZMOD_IWDOG_VERSION_STRING                                                                  \
    STRINGIFY(ZMOD_IWDOG_VERSION_MAJOR)                                                            \
    "." STRINGIFY(ZMOD_IWDOG_VERSION_MINOR) "." STRINGIFY(ZMOD_IWDOG_VERSION_PATCH)

#endif /* ZMOD_IWDOG_VERSION_H */
