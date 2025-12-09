/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bt_core.h
 * @brief Zmod BT module public API for BLE peripheral functionality
 */

#ifndef ZMOD_BT_CORE_H
#define ZMOD_BT_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
#include <zephyr/zbus/zbus.h>
#endif

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
 * @brief Bluetooth connection state
 */
enum zmod_bt_conn_state {
    ZMOD_BT_CONN_STATE_DISCONNECTED = 0,
    ZMOD_BT_CONN_STATE_CONNECTED = 1,
};

/**
 * @brief Bluetooth connection event structure
 *
 * Published when BT connection state changes
 */
struct zmod_bt_conn_event {
    enum zmod_bt_conn_state state; /* Current connection state */
    uint8_t reason;                /* Connect error or disconnect reason code */
    uint16_t conn_handle;          /* Connection handle (0 if disconnected) */
};

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
/* Zbus channel for BT connection events */
ZBUS_CHAN_DECLARE(zmod_bt_conn_chan);
#endif

/*****************************************************************************
 * Public Types
 *****************************************************************************/

/**
 * @brief BT connection callbacks structure
 */
typedef struct {
    void (*on_connected)(struct bt_conn *conn, uint8_t err);
    void (*on_disconnected)(struct bt_conn *conn, uint8_t reason);
} zmod_bt_core_callbacks_t;

/*****************************************************************************
 * Public Functions
 *****************************************************************************/
/**
 * @brief Initialize bluetooth core
 *
 * @param adv_name Optional advertising name (NULL to use Kconfig default)
 * @return 0 on success, negative errno on failure
 */
int zmod_bt_core_init(const char *adv_name);

/**
 * @brief Start advertising if not already
 *
 * @return none
 */
void zmod_bt_core_start_advertising(void);

/**
 * @brief Stop advertising if currently advertising
 *
 * @return none
 */
void zmod_bt_core_stop_advertising(void);

/**
 * @brief Return true if system is currently advertising, false otherwise
 *
 * @return true if advertising, false otherwise
 */
bool zmod_bt_core_is_currently_advertising(void);

/**
 * @brief Register callbacks for connection events
 *
 * Must be called before zmod_bt_core_init() to ensure callbacks are registered
 * before any connections can occur.
 *
 * @param callbacks Pointer to callbacks structure (can be NULL to clear)
 */
void zmod_bt_core_set_callbacks(const zmod_bt_core_callbacks_t *callbacks);

/**
 * @brief Override advertising and scan response payloads.
 *
 * Call before @ref zmod_bt_core_init to replace default data. Passing NULL
 * pointers leaves the corresponding payload unchanged.
 *
 * @param adv_data    Pointer to array of bt_data elements for advertising.
 * @param adv_len     Number of elements in @p adv_data.
 * @param scan_rsp    Pointer to array of bt_data elements for scan response.
 * @param scan_len    Number of elements in @p scan_rsp.
 *
 * @return 0 on success, -EINVAL if lengths exceed configured limits.
 */
int zmod_bt_core_set_adv_payload(const struct bt_data *adv_data,
                                 size_t adv_len,
                                 const struct bt_data *scan_rsp,
                                 size_t scan_len);

/**
 * @brief Reset advertising and scan response payloads to defaults.
 */
void zmod_bt_core_reset_adv_payload(void);

#ifdef __cplusplus
}
#endif
#endif /* ZMOD_BT_CORE_H */
