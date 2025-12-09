/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file iwdog.h
 * @brief Zmod Internal watchdog timer management
 */

#ifndef ZMOD_IWDOG_H
#define ZMOD_IWDOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
#include <zephyr/zbus/zbus.h>
#endif

/****************************************************************
 * Public Defines
 ****************************************************************/

/****************************************************************
 * Public Types
 ****************************************************************/

/**
 * @brief Internal watchdog warning event structure
 *
 * Published when iwdog reset is imminent
 */
struct zmod_iwdog_warning_event {
    int32_t time_until_reset_ms; /* Time remaining until iwdog reset in milliseconds */
};

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
/* Zbus channel for Zmod iwdog reset imminent warnings */
ZBUS_CHAN_DECLARE(zmod_iwdog_warning_chan);
#endif

/****************************************************************
 * Public Functions
 ****************************************************************/

/**
 * @brief Initialize the internal watchdog timer
 *
 * Configures and starts the internal watchdog timer with the timeout specified
 * in Kconfig. The iwdog will be configured to reset the system
 * if not fed within the timeout period.
 *
 * @return 0 on success, negative errno on failure
 */
int zmod_iwdog_init(void);

/**
 * @brief Feed the internal watchdog timer
 *
 * This function must be called periodically to prevent the iwdog
 * from resetting the system. The feed period must be less than the
 * configured timeout.
 */
void zmod_iwdog_feed(void);

/**
 * @brief Start the internal watchdog service thread
 *
 * Creates and starts a dedicated thread that periodically feeds the
 * iwdog timer. The thread will run at the interval specified in
 * Kconfig.
 */
void zmod_iwdog_start_service_thread(void);

#ifdef __cplusplus
}
#endif
#endif /* ZMOD_IWDOG_H */