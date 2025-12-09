/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file config_mgr.h
 * @brief Configuration manager interface
 */

#ifndef ZMOD_CONFIG_MGR_H
#define ZMOD_CONFIG_MGR_H

#include <zmod/configs.h>

#include <stdbool.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/

/*****************************************************************************
 * Structs, Unions, Enums, & Typedefs
 *****************************************************************************/

/*****************************************************************************
 * Public Functions
 *****************************************************************************/

/**
 * @brief Initialize configuration manager
 */
void zmod_config_mgr_init(void);

/**
 * @brief Get value of configuration for key
 *
 * @param key Key
 * @param dst Buffer to write value to
 * @param size Size of buffer
 * @return Returns true on success
 */
bool zmod_config_mgr_get_value(config_key_t key, void *dst, size_t size);

/**
 * @brief Set value for given key
 *
 * @param key Key
 * @param src Source buffer
 * @param size Size of source
 * @return Returns true on success
 */
bool zmod_config_mgr_set_value(config_key_t key, const void *src, size_t size);

/**
 * @brief Reset all NVS entries to defaults
 *
 * This will delete ALL configuration values from NVS storage,
 * causing them to use default values on next read.
 */
void zmod_config_mgr_reset_nvs(void);

/**
 * @brief Reset resettable configuration entries to defaults
 *
 * This will only reset configuration entries that have the
 * resettable flag set to true in their definition.
 */
void zmod_config_mgr_reset_configs(void);

#ifdef __cplusplus
}
#endif
#endif /* ZMOD_CONFIG_MGR_H */
