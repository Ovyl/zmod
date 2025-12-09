/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file configs.c
 * @brief Configuration entries implementation
 */

#include <zmod/configs.h>

#include <stdbool.h>
#include <stddef.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

#ifdef CONFIG_ZMOD_CONFIG_USE_CUSTOM_TYPES
#include CONFIG_ZMOD_CONFIG_TYPES_DEF_PATH
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/

/*****************************************************************************
 * Variables
 *****************************************************************************/

// Define default values for each configuration key
#define CFG_DEFINE(key, type, default_val, rst) static type key##_def_val = default_val;
#include CONFIG_ZMOD_CONFIG_APP_DEF_PATH
#undef CFG_DEFINE

#define CFG_DEFINE(key, type, default_val, rst)                                                    \
    [key] = {.value_size_bytes = sizeof(type),                                                     \
             .default_value = &key##_def_val,                                                      \
             .human_readable_key = #key,                                                           \
             .resettable = (rst)},

static config_entry_t prv_config_entries[] = {
#include CONFIG_ZMOD_CONFIG_APP_DEF_PATH
#undef CFG_DEFINE
};

/*****************************************************************************
 * Prototypes
 *****************************************************************************/

/*****************************************************************************
 * Public Functions
 *****************************************************************************/

config_entry_t *zmod_configs_get_entry(config_key_t key) {
    if (key >= CFG_NUM_KEYS) {
        return NULL;
    }

    return &prv_config_entries[key];
}

const char *zmod_config_key_as_str(config_key_t key) {
    static const char *unknown_key = "Unknown key";

    config_entry_t *entry = zmod_configs_get_entry(key);

    if (entry == NULL) {
        return unknown_key;
    }

    return entry->human_readable_key;
}
