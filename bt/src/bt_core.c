/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bt_core.c
 * @brief Zmod BT module core implementation for BLE peripheral functionality
 */

#include <zmod/bt_core.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zmod/bt_version.h>

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
#include <zephyr/zbus/zbus.h>
#endif

#ifdef CONFIG_ZMOD_BT_SHELL
#include <bluetooth/services/nus.h>
#include <shell/shell_bt_nus.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_backend.h>
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/

LOG_MODULE_REGISTER(zmod_bt_core, CONFIG_ZMOD_BT_LOG_LEVEL);

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
/* Define the Zbus channel for BT connection events */
ZBUS_CHAN_DEFINE(zmod_bt_conn_chan,
                 struct zmod_bt_conn_event,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));
#endif

/*****************************************************************************
 * Variables
 *****************************************************************************/

/**
 * @brief 
 * 
 */
static struct bt_data prv_default_adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, CONFIG_ZMOD_BT_ADV_FLAGS),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct bt_data prv_default_scan_rsp[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

#if defined(CONFIG_BT_DEVICE_NAME_MAX)
#define ZMOD_BT_NAME_BUF_SIZE CONFIG_BT_DEVICE_NAME_MAX
#else
#define ZMOD_BT_NAME_BUF_SIZE 31U
#endif

static char prv_adv_name_buf[ZMOD_BT_NAME_BUF_SIZE + 1];

static const struct bt_data *prv_adv_data = prv_default_adv_data;
static size_t prv_adv_data_len = ARRAY_SIZE(prv_default_adv_data);

static const struct bt_data *prv_scan_rsp = prv_default_scan_rsp;
static size_t prv_scan_rsp_len = ARRAY_SIZE(prv_default_scan_rsp);

#define ZMOD_BT_MAX_ADV_ITEMS 6

static struct bt_data prv_user_adv_data[ZMOD_BT_MAX_ADV_ITEMS];
static uint8_t prv_user_adv_storage[BT_GAP_ADV_MAX_ADV_DATA_LEN];

static struct bt_data prv_user_scan_data[ZMOD_BT_MAX_ADV_ITEMS];
static uint8_t prv_user_scan_storage[BT_GAP_ADV_MAX_ADV_DATA_LEN];

/**
 * @brief Advertising parameters
 *
 */
static const struct bt_le_adv_param adv_params = {
#ifdef CONFIG_ZMOD_BT_ADV_CONNECTABLE
    .options = BT_LE_ADV_OPT_CONN,
#else
    .options = 0,
#endif
    .interval_min = CONFIG_ZMOD_BT_ADV_INTERVAL_MIN,
    .interval_max = CONFIG_ZMOD_BT_ADV_INTERVAL_MAX,
    .id = CONFIG_ZMOD_BT_ADV_ID,
};

/**
 * @brief Private static instance
 */
static struct {
    struct k_work advertising_worker;
    zmod_bt_core_callbacks_t callbacks;
    struct bt_conn *conn;
    uint16_t conn_handle;
    volatile bool is_advertising;
} prv_inst;

/*****************************************************************************
 * Prototypes
 *****************************************************************************/
/**
 * @brief Start BLE advertising
 */
static void prv_advertising_start(void);
static void prv_advertising_stop(void);
static void prv_advertising_worker_task(struct k_work *work);

static int prv_copy_payload(const struct bt_data *src,
                            size_t len,
                            struct bt_data *dst,
                            size_t dst_cap,
                            uint8_t *storage,
                            size_t storage_cap)
{
    size_t offset = 0U;

    if (len > dst_cap) {
        return -EINVAL;
    }

    for (size_t i = 0U; i < len; i++) {
        size_t entry_len = src[i].data_len;

        if ((entry_len > 0U) && (src[i].data == NULL)) {
            return -EINVAL;
        }

        if ((offset + entry_len) > storage_cap) {
            return -EINVAL;
        }

        dst[i].type = src[i].type;
        dst[i].data_len = src[i].data_len;

        if ((entry_len > 0U) && (src[i].data != NULL)) {
            memcpy(storage + offset, src[i].data, entry_len);
            dst[i].data = storage + offset;
        } else {
            dst[i].data = NULL;
        }

        offset += entry_len;
    }

    /* Clear any unused slots */
    if (len < dst_cap) {
        memset(&dst[len], 0, (dst_cap - len) * sizeof(*dst));
    }

    return 0;
}

/*****************************************************************************
 * Public Functions
 *****************************************************************************/
int zmod_bt_core_init(const char *adv_name) {
    int err;

    prv_inst.conn = NULL;
    prv_inst.conn_handle = 0;

    bool using_default_scan = (prv_scan_rsp == prv_default_scan_rsp);

    /* Update scan response data with custom name if provided */
    if ((adv_name != NULL) && using_default_scan) {
        size_t name_len = strlen(adv_name);
        name_len = MIN(name_len, sizeof(prv_adv_name_buf) - 1U);
        memcpy(prv_adv_name_buf, adv_name, name_len);
        prv_adv_name_buf[name_len] = '\0';

        if ((prv_scan_rsp_len > 0U) && (prv_scan_rsp[0].type == BT_DATA_NAME_COMPLETE)) {
            struct bt_data *mutable = (struct bt_data *)prv_scan_rsp;
            mutable[0].data_len = name_len;
            mutable[0].data = (const uint8_t *)prv_adv_name_buf;
        }
    }

    k_work_init(&prv_inst.advertising_worker, prv_advertising_worker_task);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth core initialization failed: %d", err);
        return err;
    }

#ifdef CONFIG_ZMOD_BT_ADV_AUTO_START
    prv_advertising_start();
#endif

#ifdef CONFIG_ZMOD_BT_SHELL
    err = shell_bt_nus_init();
    if (err) {
        LOG_ERR("Failed to initialize BT NUS shell (err: %d)", err);
        return err;
    }
#endif

    const char *active_name = adv_name;
    if (active_name == NULL) {
        if ((prv_scan_rsp_len > 0U) && (prv_scan_rsp[0].type == BT_DATA_NAME_COMPLETE)) {
            active_name = (const char *)prv_scan_rsp[0].data;
        } else {
            active_name = CONFIG_BT_DEVICE_NAME;
        }
    }
    LOG_INF("Zmod BT module v%s initialized, advertising name: %s",
            ZMOD_BT_VERSION_STRING,
            active_name);
    return 0;
}

void zmod_bt_core_start_advertising(void) {
    prv_advertising_start();
}

void zmod_bt_core_stop_advertising(void) {
    prv_advertising_stop();
}

bool zmod_bt_core_is_currently_advertising(void) {
    return prv_inst.is_advertising;
}

void zmod_bt_core_set_callbacks(const zmod_bt_core_callbacks_t *callbacks) {
    if (callbacks) {
        prv_inst.callbacks = *callbacks;
    } else {
        memset(&prv_inst.callbacks, 0, sizeof(prv_inst.callbacks));
    }
}

int zmod_bt_core_set_adv_payload(const struct bt_data *adv_data,
                                 size_t adv_len,
                                 const struct bt_data *scan_rsp,
                                 size_t scan_len)
{
    if ((adv_len > 0U) && (adv_data == NULL)) {
        return -EINVAL;
    }
    if ((scan_len > 0U) && (scan_rsp == NULL)) {
        return -EINVAL;
    }

    if (prv_inst.is_advertising) {
        int err = bt_le_adv_stop();
        if (err) {
            return err;
        }
        prv_inst.is_advertising = false;
    }

    if (adv_len > 0U) {
        int err = prv_copy_payload(adv_data,
                                   adv_len,
                                   prv_user_adv_data,
                                   ARRAY_SIZE(prv_user_adv_data),
                                   prv_user_adv_storage,
                                   sizeof(prv_user_adv_storage));
        if (err) {
            return err;
        }
        prv_adv_data = prv_user_adv_data;
        prv_adv_data_len = adv_len;
    } else {
        prv_adv_data = prv_default_adv_data;
        prv_adv_data_len = ARRAY_SIZE(prv_default_adv_data);
    }

    if (scan_len > 0U) {
        int err = prv_copy_payload(scan_rsp,
                                   scan_len,
                                   prv_user_scan_data,
                                   ARRAY_SIZE(prv_user_scan_data),
                                   prv_user_scan_storage,
                                   sizeof(prv_user_scan_storage));
        if (err) {
            return err;
        }
        prv_scan_rsp = prv_user_scan_data;
        prv_scan_rsp_len = scan_len;
    } else {
        prv_scan_rsp = prv_default_scan_rsp;
        prv_scan_rsp_len = ARRAY_SIZE(prv_default_scan_rsp);
    }

    return 0;
}

void zmod_bt_core_reset_adv_payload(void)
{
    if (prv_inst.is_advertising) {
        (void)bt_le_adv_stop();
        prv_inst.is_advertising = false;
    }

    prv_adv_data = prv_default_adv_data;
    prv_adv_data_len = ARRAY_SIZE(prv_default_adv_data);
    prv_scan_rsp = prv_default_scan_rsp;
    prv_scan_rsp_len = ARRAY_SIZE(prv_default_scan_rsp);

    memset(prv_user_adv_data, 0, sizeof(prv_user_adv_data));
    memset(prv_user_adv_storage, 0, sizeof(prv_user_adv_storage));
    memset(prv_user_scan_data, 0, sizeof(prv_user_scan_data));
    memset(prv_user_scan_storage, 0, sizeof(prv_user_scan_storage));
}

/*****************************************************************************
 * Private Functions
 *****************************************************************************/

/**
 * @brief Callback called when device connected
 *
 * @param conn Pointer to connection
 * @param err Error connecting to device
 */
static void prv_device_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Failed to connect to BLE device: %u", err);
        return;
    } else {
        LOG_INF("Connected to BLE device.");

        prv_inst.conn = bt_conn_ref(conn);

#ifdef CONFIG_ZMOD_BT_SHELL
        shell_bt_nus_enable(conn);
#endif

        int ret = bt_hci_get_conn_handle(prv_inst.conn, &prv_inst.conn_handle);
        if (ret) {
            LOG_ERR("Failed to get connection handle: %d", ret);
            return;
        }
    }

    if (prv_inst.callbacks.on_connected) {
        prv_inst.callbacks.on_connected(conn, err);
    }

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
    /* Publish connection event via Zbus */
    struct zmod_bt_conn_event evt = {
        .state = ZMOD_BT_CONN_STATE_CONNECTED, .reason = err, .conn_handle = prv_inst.conn_handle};
    int ret_zbus = zbus_chan_pub(&zmod_bt_conn_chan, &evt, K_NO_WAIT);
    if (ret_zbus != 0) {
        LOG_WRN("Failed to publish BT connection event: %d", ret_zbus);
    }
#endif

    prv_inst.is_advertising = false;
}

/**
 * @brief Callback called when device disconnected
 *
 * @param conn Pointer to connection
 * @param reason Reason for disconnection
 */
static void prv_device_disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Disconnected from device: %u", reason);

#ifdef CONFIG_ZMOD_BT_SHELL
    shell_bt_nus_disable();
#endif

    // Explicitly unreference the connection only if we have a reference
    if (prv_inst.conn) {
        bt_conn_unref(prv_inst.conn);
        prv_inst.conn = NULL;
    }

    prv_inst.conn_handle = 0;

    if (prv_inst.callbacks.on_disconnected) {
        prv_inst.callbacks.on_disconnected(conn, reason);
    }

#ifdef CONFIG_ZMOD_BT_ZBUS_PUBLISH
    /* Publish disconnection event via Zbus */
    struct zmod_bt_conn_event evt = {
        .state = ZMOD_BT_CONN_STATE_DISCONNECTED, .reason = reason, .conn_handle = 0};
    int ret = zbus_chan_pub(&zmod_bt_conn_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to publish BT disconnection event: %d", ret);
    }
#endif

#ifdef CONFIG_ZMOD_BT_ADV_RESTART_ON_DISCONNECT
    prv_advertising_start();
#endif
}

/**
 * @brief Worker task handling advertising
 *
 * @param work Pointer to worker instance
 */
static void prv_advertising_worker_task(struct k_work *work) {
    int err = bt_le_adv_start(&adv_params,
                              prv_adv_data,
                              prv_adv_data_len,
                              prv_scan_rsp,
                              prv_scan_rsp_len);

    if (err) {
        LOG_ERR("Failed to start BLE advertising: %d", err);
        return;
    }

    LOG_INF("BLE Advertising begun...");
    prv_inst.is_advertising = true;
}

/**
 * @brief Stop BLE advertising
 */
static void prv_advertising_stop(void) {
    int err = bt_le_adv_stop();
    if (err) {
        LOG_ERR("Failed to stop BLE advertising: %d", err);
    } else {
        LOG_INF("BLE Advertising stopped");
        prv_inst.is_advertising = false;
    }
}

/**
 * @brief Start advertising
 */
static void prv_advertising_start(void) {
    k_work_submit(&prv_inst.advertising_worker);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = prv_device_connected,
    .disconnected = prv_device_disconnected,
};

/*****************************************************************************
 * Shell Commands
 *****************************************************************************/
#ifdef CONFIG_ZMOD_BT_SHELL_CMDS

#include <zephyr/shell/shell.h>
#include <stdlib.h>

/**
 * @brief Shell command to start advertising
 */
static int cmd_adv_start(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (prv_inst.is_advertising) {
        shell_print(shell, "Advertising already active");
        return 0;
    }

    prv_advertising_start();
    shell_print(shell, "Advertising start requested");
    return 0;
}

/**
 * @brief Shell command to stop advertising
 */
static int cmd_adv_stop(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!prv_inst.is_advertising) {
        shell_print(shell, "Advertising not active");
        return 0;
    }

    prv_advertising_stop();
    shell_print(shell, "Advertising stopped");
    return 0;
}

/**
 * @brief Shell command to disconnect active connection
 */
static int cmd_disconnect(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!prv_inst.conn) {
        shell_print(shell, "No active connection");
        return -ENOTCONN;
    }

    int err = bt_conn_disconnect(prv_inst.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        shell_print(shell, "Failed to disconnect: %d", err);
        return err;
    }

    shell_print(shell, "Disconnection initiated");
    return 0;
}

/**
 * @brief Shell command to show BT status
 */
static int cmd_status(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(shell, "BT Module Status:");
    shell_print(shell, "  Advertising: %s", prv_inst.is_advertising ? "Yes" : "No");
    shell_print(shell, "  Connected: %s", prv_inst.conn ? "Yes" : "No");
    if (prv_inst.conn) {
        shell_print(shell, "  Connection handle: 0x%04x", prv_inst.conn_handle);
    }
    return 0;
}

/* Advertising subcommands */
SHELL_STATIC_SUBCMD_SET_CREATE(zmod_bt_adv_cmds,
                               SHELL_CMD(start, NULL, "Start BLE advertising", cmd_adv_start),
                               SHELL_CMD(stop, NULL, "Stop BLE advertising", cmd_adv_stop),
                               SHELL_SUBCMD_SET_END);

/* Main BT subcommands */
SHELL_STATIC_SUBCMD_SET_CREATE(
    zmod_bt_cmds,
    SHELL_CMD(adv, &zmod_bt_adv_cmds, "Advertising commands", NULL),
    SHELL_CMD(disconnect, NULL, "Disconnect active BLE connection", cmd_disconnect),
    SHELL_CMD(status, NULL, "Show BT module status", cmd_status),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(zmod_bt, &zmod_bt_cmds, "Zmod Bluetooth module commands", NULL);

#endif /* CONFIG_ZMOD_BT_SHELL_CMDS */
