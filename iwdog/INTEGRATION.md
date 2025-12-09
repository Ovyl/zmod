# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod IWDOG Module Integration Guide

## Overview

The Zmod Internal Watchdog (IWDOG) module provides a high-level abstraction for hardware watchdog timer management in Zephyr RTOS applications. This module includes configurable automatic feeding, warning notifications, and integration with Zephyr's logging and Zbus subsystems.

## Integration Steps

### 1. Add Module to West Manifest

Add the Zmod modules repository the `west.yml`:

```yaml
manifest:
  remotes:
    - name: ovyl
      url-base: https://github.com/Ovyl

  projects:
    - name: ovyl-zephyr-modules
      remote: ovyl
      repo-path: ovyl-zephyr-modules
      revision: main            # or a branch/tag/SHA
      path: modules/ovyl        # folder in your workspace

```

After updating `west.yml`, run:
```bash
west update ovyl-zephyr-modules
```

### 2. Device Tree Configuration

The IWDOG module requires a hardware watchdog device. Ensure the device tree has a watchdog node with the `watchdog0` alias:

#### Example Device Tree Overlay

Create or update the board overlay file (e.g., `boards/your_board.overlay`):

```dts
/ {
    aliases {
        watchdog0 = &wdt0;
    };
};

&wdt0 {
    status = "okay";
};
```

### 3. Kconfig Configuration

Enable the module in your application's `prj.conf` and change any default kconfig values if desired:

```conf
# Enable Zmod IWDOG module
CONFIG_ZMOD_IWDOG=y

```

## Usage

### Basic Initialization

Initialize the watchdog during system startup:

```c
#include <zmod/iwdog.h>

void main(void) {
    int ret = zmod_iwdog_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize iwdog: %d", ret);
        return;
    }

    // If using manual feeding
    zmod_iwdog_start_service_thread();

    // Your application code...
}
```

### Manual Feeding

If you need manual control over watchdog feeding:

```c
#include <zmod/iwdog.h>

void main(void) {
    // Initialize without auto-feed thread
    int ret = zmod_iwdog_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize iwdog: %d", ret);
        return;
    }

    while (1) {
        // Your application logic here...

        // Feed the watchdog manually
        zmod_iwdog_feed();

        // Sleep or continue with other tasks
        k_sleep(K_MSEC(5000));  // Must be less than timeout
    }
}
```

### Handling Warning Events

If you enabled Zbus publishing, you can subscribe to warning events:

```c
#include <zmod/iwdog.h>
#include <zephyr/zbus/zbus.h>

// Subscriber callback for iwdog warnings
static void iwdog_warning_handler(const struct zbus_channel *chan) {
    const struct zmod_iwdog_warning_event *event;

    if (zbus_chan_read(chan, &event, K_NO_WAIT) == 0) {
        LOG_WRN("Watchdog reset imminent! Time remaining: %d ms",
                event->time_until_reset_ms);

        // Take emergency actions here (save data, etc.)
    }
}

// Subscribe to watchdog warning channel
ZBUS_LISTENER_DEFINE(iwdog_warning_listener, iwdog_warning_handler);
ZBUS_CHAN_ADD_OBS(zmod_iwdog_warning_chan, iwdog_warning_listener, 0);
```
