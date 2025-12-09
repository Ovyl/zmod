/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file config_keys.h
 * @brief Definition of configuration keys
 */

#ifndef ZMOD_CONFIG_KEYS_H
#define ZMOD_CONFIG_KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/

 #ifndef CONFIG_ZMOD_CONFIG_APP_DEF_PATH
#error "Set CONFIG_CONFIGS_APP_DEF_PATH in prj.conf to your app's .def file \
(e.g. \"configs.def\"). Ensure the file is on the compiler include path: \
- put it under app/include/, OR \
- add its directory via zephyr_include_directories(...), OR \
- use an absolute path in prj.conf."
#endif

/*****************************************************************************
 * Structs, Unions, Enums, & Typedefs
 *****************************************************************************/

#define CFG_DEFINE(key, type, default_val, rst) key,
/**
 * @brief Definition of configuration keys
 */
typedef enum {
#include CONFIG_ZMOD_CONFIG_APP_DEF_PATH
    CFG_NUM_KEYS
} config_key_t;
#undef CFG_DEFINE

/*****************************************************************************
 * Function Prototypes
 *****************************************************************************/

#ifdef __cplusplus
}
#endif
#endif /* ZMOD_CONFIG_KEYS_H */
