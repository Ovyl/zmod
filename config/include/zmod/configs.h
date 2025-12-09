/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file configs.h
 * @brief Configuration entries interface
 */

#ifndef ZMOD_CONFIGS_H
#define ZMOD_CONFIGS_H

#include <stddef.h>
#include <stdbool.h>

#include <zmod/config_keys.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/

/*****************************************************************************
 * Structs, Unions, Enums, & Typedefs
 *****************************************************************************/

/**
 * @typedef config_entry_t
 * @brief Definition of configuration entry
 *
 */
typedef struct config_entry_t {
    const char *human_readable_key;
    size_t value_size_bytes;
    void *default_value;
    bool resettable;
} config_entry_t;

/*****************************************************************************
 * Public Functions
 *****************************************************************************/

/**
 * @brief Get configuration entry for given key
 *
 * @param key Key for entry
 * @return Returns pointer to entry or NULL if key is not valid
 */
config_entry_t *zmod_configs_get_entry(config_key_t key);

/**
 * @brief Get human readable version of key
 *
 * @param key Key
 * @return Will return human readable key if key exists or "Unknown Key" if not
 * valid.
 */
const char *zmod_config_key_as_str(config_key_t key);

#ifdef __cplusplus
}
#endif
#endif /* ZMOD_CONFIGS_H */
