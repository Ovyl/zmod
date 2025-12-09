/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file config_version.h
 * @brief Zmod configuration module version definitions
 */

#ifndef ZMOD_CONFIG_VERSION_H
#define ZMOD_CONFIG_VERSION_H

#define ZMOD_CONFIG_VERSION_MAJOR 1
#define ZMOD_CONFIG_VERSION_MINOR 0
#define ZMOD_CONFIG_VERSION_PATCH 0

#define ZMOD_CONFIG_VERSION_STRING                                                                 \
    STRINGIFY(ZMOD_CONFIG_VERSION_MAJOR)                                                           \
    "." STRINGIFY(ZMOD_CONFIG_VERSION_MINOR) "." STRINGIFY(ZMOD_CONFIG_VERSION_PATCH)

#endif /* ZMOD_CONFIG_VERSION_H */
