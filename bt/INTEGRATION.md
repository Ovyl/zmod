# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod BT Module Integration Guide

## Overview

The Zmod BT module provides a Bluetooth Low Energy (BLE) peripheral implementation with configurable advertising, connection management, and optional Zbus event publishing for connection state changes.

## Features

- Configurable BLE advertising parameters
- Connection state management
- Zbus integration for publishing connection events
- Shell commands for runtime control
- Automatic advertising restart on disconnect (configurable)
- Support for multiple Bluetooth identities

## Integration Steps

### 1. Add Module to West Manifest

Add the Zmod repository to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: ovyl
      url-base: https://github.com/Ovyl

  projects:
    - name: ovyl-zephyr-modules
      remote: ovyl
      repo-path: ovyl-zephyr-modules
      revision: main            # or a specific branch/tag/SHA
      path: modules/ovyl        # folder in your workspace
```

After updating `west.yml`, run:
```bash
west update ovyl-zephyr-modules
```

### 2. Kconfig Configuration

#### Bluetooth Configuration
Enable the module and configure options in your application's `prj.conf`:

```
# Enable Zmod BT module
CONFIG_ZMOD_BT=y

# Advertising configuration (optional)
CONFIG_ZMOD_BT_ADV_FLAGS=0x06                    # BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
CONFIG_ZMOD_BT_ADV_CONNECTABLE=y                 # Enable connectable advertising
CONFIG_ZMOD_BT_ADV_INTERVAL_MIN=1600             # 1 second (in 0.625ms units)
CONFIG_ZMOD_BT_ADV_INTERVAL_MAX=2400             # 1.5 seconds (in 0.625ms units)
CONFIG_ZMOD_BT_ADV_AUTO_START=y                  # Auto-start advertising on init
CONFIG_ZMOD_BT_ADV_RESTART_ON_DISCONNECT=y       # Auto-restart after disconnect
CONFIG_ZMOD_BT_ADV_ID=0                          # Bluetooth identity ID (0-15)
CONFIG_BT_DEVICE_NAME="Device Name"

# Enable Zbus event publishing (optional)
CONFIG_ZBUS=y
CONFIG_ZMOD_BT_ZBUS_PUBLISH=y

# Enable shell commands (optional)
CONFIG_SHELL=y
```

#### Bluetooth Shell and Logging over NUS

The BT module supports running the Zephyr shell and logging over Bluetooth using the Nordic UART Service (NUS). This enables remote shell access and log viewing without a physical UART connection.

### Enabling BT Shell and NUS

Add the following to your `prj.conf`:

```conf
# Enable Bluetooth shell over NUS
CONFIG_ZMOD_BT_SHELL=y

# Additional recommended buffer configurations for stable operation (not required)
CONFIG_BT_L2CAP_TX_MTU=247
CONFIG_BT_BUF_ACL_TX_SIZE=251
CONFIG_BT_BUF_ACL_RX_SIZE=251
CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE=512
CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE=64
CONFIG_BT_ATT_TX_MAX=2
```

**Note:** Depending on your application and the volume of data being transmitted over NUS, you may need to adjust buffer sizes, MTU values, or other Bluetooth stack parameters to prevent buffer overflow or dropped data.

### Using the Python BLE Shell Script

A Python script is provided to connect to the BT shell from your PC:

```bash
# Connect to a specific device by name
./bt/scripts/ble_shell.py "My Device"

# Or scan and select from available devices
./bt/scripts/ble_shell.py
```

The script requires Python 3 with the `bleak` package:
```bash
pip install bleak
```

Once connected, you have full shell access. By default logging is set to `inf`

This can be adjusted at runtime by sending `log enable <lvl>` where `lvl` is 
`err`, `wrn`, `inf`, `dbg`.

**Limitation:** The log level for BT transmission is controlled via shell command at runtime, not through Kconfig. This allows you to dynamically adjust the verbosity without reflashing firmware.

## Usage

### Initialization

Initialize the BT module during system startup. The advertising name can either be set with a Kconfig option, or it can be provided at runtime when calling `zmod_bt_core_init`. If the Kconfig default is used, pass `NULL` to the init function:

```c
#include <zmod/bt_core.h>

int main(void) {
    int err = zmod_bt_core_init(NULL); /* use CONFIG_BT_DEVICE_NAME */
    if (err) {
        LOG_ERR("Failed to initialize BT: %d", err);
        return err;
    }

    return 0;
}

/* Or provide a runtime name */
int main(void) {
    int err = zmod_bt_core_init("My Device");
    if (err) {
        LOG_ERR("Failed to initialize BT: %d", err);
        return err;
    }

    return 0;
}
```

### Custom Advertising Payloads

By default the module advertises the GAP flags and the configured device name. If your application needs to advertise additional data (e.g. custom 128-bit UUIDs) you can supply your own payloads before calling `zmod_bt_core_init()`:

```c
static const uint8_t custom_uuid[] = {
    0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x9A,
    0xA9, 0x87, 0x65, 0x43, 0x32, 0x1F, 0xED, 0xCB
};

static struct bt_data adv_payload[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, CONFIG_ZMOD_BT_ADV_FLAGS),
    BT_DATA(BT_DATA_UUID128_ALL, custom_uuid, sizeof(custom_uuid)),
};

static struct bt_data scan_rsp_payload[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, "MyDevice", 8),
};

void app_bt_init(void)
{
    /* Must be called before zmod_bt_core_init() */
    int err = zmod_bt_core_set_adv_payload(adv_payload,
                                           ARRAY_SIZE(adv_payload),
                                           scan_rsp_payload,
                                           ARRAY_SIZE(scan_rsp_payload));
    if (err) {
        LOG_ERR("Failed to set advertising payload: %d", err);
        return;
    }

    err = zmod_bt_core_init(NULL);
    if (err) {
        LOG_ERR("Failed to initialize BT core: %d", err);
    }
}
```

Passing `NULL` (or zero length) for either payload reverts to the module defaults. At runtime you can also call `zmod_bt_core_reset_adv_payload()` to restore the original data.

### Using Callbacks (Alternative to Zbus)

Register callbacks to handle connection events directly:

```c
#include <zmod/bt_core.h>

static void on_bt_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed: %u", err);
    } else {
        LOG_INF("Device connected!");
    }
}

static void on_bt_disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Device disconnected (reason: %u)", reason);
}

int main(void) {
    // Register callbacks before initialization
    struct ble_core_callbacks callbacks = {
        .on_connected = on_bt_connected,
        .on_disconnected = on_bt_disconnected
    };
    ble_core_set_callbacks(&callbacks);

    // Initialize the BT module
    int err = ble_core_init();
    if (err) {
        LOG_ERR("Failed to initialize BT: %d", err);
        return err;
    }

    // Your application code...
    return 0;
}
```

### Manual Advertising Control

If `CONFIG_ZMOD_BT_ADV_AUTO_START` is disabled, manually control advertising:

```c
#include <zmod/bt_core.h>

// Start advertising
ble_core_start_advertising();

// Check if currently advertising
if (ble_core_is_currently_advertising()) {
    LOG_INF("Device is advertising");
}
```

### Zbus Event Subscription

Subscribe to BT connection events when `CONFIG_ZMOD_BT_ZBUS_PUBLISH` is enabled:

```c
#include <zephyr/zbus/zbus.h>
#include <zmod/bt_core.h>

// Define listener callback
static void bt_conn_listener(const struct zbus_channel *chan) {
    const struct zmod_bt_conn_event *evt = zbus_chan_const_msg(chan);

    if (evt->state == ZMOD_BT_CONN_STATE_CONNECTED) {
        LOG_INF("Device connected (handle: 0x%04x)", evt->conn_handle);
    } else if (evt->state == ZMOD_BT_CONN_STATE_DISCONNECTED) {
        LOG_INF("Device disconnected (reason: %u)", evt->reason);
    }
}

// Define and register listener
ZBUS_LISTENER_DEFINE(bt_conn_listener_node, bt_conn_listener);
ZBUS_CHAN_ADD_OBS(zmod_bt_conn_chan, bt_conn_listener_node, 3);
```

### Shell Commands

When `CONFIG_SHELL` is enabled, the following commands are available:

- `zmod_bt status` - Show BT module status (advertising/connection state)
- `zmod_bt adv start` - Start BLE advertising
- `zmod_bt adv stop` - Stop BLE advertising
- `zmod_bt disconnect` - Disconnect active BLE connection

Example usage:
```bash
uart:~$ zmod_bt status
BT Module Status:
  Advertising: Yes
  Connected: No

uart:~$ zmod_bt adv stop
Advertising stopped

uart:~$ zmod_bt adv start
Advertising start requested
```

## Limitations

1. **Single Connection**: Currently supports only one active BLE connection at a time
2. **Peripheral Only**: Module is designed for BLE peripheral role only
3. **Fixed Advertising Data**: Advertising data structure is fixed (flags + optional name)
4. **No GATT Services**: Module provides connection management only; GATT services must be implemented separately
