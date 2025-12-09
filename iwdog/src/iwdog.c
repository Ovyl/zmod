/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file iwdog.c
 * @brief Zmod internal watchdog timer management implementation
 */

#include <zmod/iwdog.h>

#include <errno.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
#include <zephyr/zbus/zbus.h>
#endif

#include <zmod/iwdog_version.h>
/****************************************************************
 * Private Defines
 ****************************************************************/

LOG_MODULE_REGISTER(zmod_iwdog, CONFIG_ZMOD_IWDOG_LOG_LEVEL);

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
/* Define the Zbus channel for Zmod iwdog warnings */
ZBUS_CHAN_DEFINE(zmod_iwdog_warning_chan,
                 struct zmod_iwdog_warning_event,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));
#endif

#define ZMOD_IWDOG_THREAD_PRIORITY K_PRIO_PREEMPT(CONFIG_ZMOD_IWDOG_THREAD_PRIORITY)
#define ZMOD_IWDOG_THREAD_STACK_SIZE CONFIG_ZMOD_IWDOG_THREAD_STACK_SIZE

/* Ensure feed interval is less than timeout */
BUILD_ASSERT(CONFIG_ZMOD_WATCHDOG_FEED_INTERVAL_MS < CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS,
             "Watchdog feed interval must be less than watchdog timeout");

#if defined(CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING)
BUILD_ASSERT(CONFIG_ZMOD_IWDOG_LOG_PANIC_THRESHOLD_MS > 0 &&
                 CONFIG_ZMOD_IWDOG_LOG_PANIC_THRESHOLD_MS < CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS,
             "ZMOD_IWDOG_LOG_PANIC_THRESHOLD_MS must be in (0, TIMEOUT)");
#endif

/****************************************************************
 * Private Variables
 ****************************************************************/

static struct {
    const struct device *wdt_dev;
    int wdt_channel_id;
    struct k_thread thread_data;
    atomic_t feed_enabled;
    struct k_timer warning_timer;
    uint32_t last_feed_time32;
    bool is_initialized; /* Guard against multiple initialization */
    bool thread_started; /* Guard against duplicate thread creation */
#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
    struct k_work warning_work;       /* Work item for ISR-safe Zbus publishing */
    int32_t pending_time_until_reset; /* Time until reset for deferred publish */
#endif
#ifdef CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING
    struct k_timer panic_timer; /* Timer for LOG_PANIC before reset */
    atomic_t did_panic;         /* Ensure LOG_PANIC called only once */
#endif
} prv_inst;

static K_THREAD_STACK_DEFINE(prv_thread_stack, ZMOD_IWDOG_THREAD_STACK_SIZE);

/****************************************************************
 * Private Functions
 ****************************************************************/

/**
 * @brief Thread-safe getter for feed_enabled
 *
 * @return true if feeding is enabled, false otherwise
 */
static bool prv_get_feed_enabled(void) {
    return atomic_get(&prv_inst.feed_enabled) != 0;
}

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
/**
 * @brief Work handler for publishing iwdog warning to Zbus
 *
 * This runs in thread context, making it safe to publish to Zbus
 *
 * @param work Work item (unused)
 */
static void prv_warning_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    struct zmod_iwdog_warning_event warning_evt = {.time_until_reset_ms =
                                                       prv_inst.pending_time_until_reset};

    int ret = zbus_chan_pub(&zmod_iwdog_warning_chan, &warning_evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to publish Zmod iwdog warning event: %d", ret);
    }
}
#endif

#ifdef CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING
/**
 * @brief Panic timer callback for LOG_PANIC before reset
 *
 * This timer fires shortly before the actual watchdog reset to ensure
 * logs are flushed. LOG_PANIC() is called only once to avoid multiple
 * synchronous log switches.
 *
 * @param timer Timer instance (unused)
 */
static void prv_panic_timer_callback(struct k_timer *timer) {
    ARG_UNUSED(timer);

    if (atomic_cas(&prv_inst.did_panic, 0, 1)) {
        LOG_PANIC(); /* Switch to synchronous logging once */
        printk("Zmod IWDOG final flush: reset imminent\n");
    }
}
#endif

/**
 * @brief Internal watchdog warning timer callback
 *
 * This timer fires when CONFIG_ZMOD_IWDOG_WARNING_PCT of the iwdog timeout period
 * has elapsed without a feed, providing a warning before the actual reset.
 *
 * @param timer Timer instance (unused)
 */
static void prv_warning_timer_callback(struct k_timer *timer) {
    ARG_UNUSED(timer);

    uint32_t current_time = k_uptime_get_32();
    /* Compute elapsed time without mutating the reference */
    uint32_t time_since_feed = current_time - prv_inst.last_feed_time32;
    int32_t time_until_reset = (int32_t)CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS - (int32_t)time_since_feed;

    /* Clamp time_until_reset to non-negative for logging and publishing */
    if (time_until_reset < 0) {
        time_until_reset = 0;
    }

    LOG_ERR("Zmod IWDOG Warning: Timer will expire in approximately %d ms!", time_until_reset);
    LOG_ERR("Feed status: %s", prv_get_feed_enabled() ? "enabled" : "DISABLED");

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
    /* Store data and defer Zbus publish to work item (ISR-safe) */
    prv_inst.pending_time_until_reset = time_until_reset;
    k_work_submit(&prv_inst.warning_work);
#endif
}

/**
 * @brief Internal watchdog service thread entry point
 *
 * This thread periodically feeds the iwdog timer to prevent system reset.
 *
 * @param p1 Unused parameter
 * @param p2 Unused parameter
 * @param p3 Unused parameter
 */
static void prv_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Zmod Internal watchdog thread running, feeding every %d ms",
            CONFIG_ZMOD_WATCHDOG_FEED_INTERVAL_MS);

    while (1) {
        if (prv_get_feed_enabled()) {
            zmod_iwdog_feed();
        }
        k_sleep(K_MSEC(CONFIG_ZMOD_WATCHDOG_FEED_INTERVAL_MS));
    }
}

/****************************************************************
 * Public Functions
 ****************************************************************/

int zmod_iwdog_init(void) {
    int ret;
    struct wdt_timeout_cfg wdt_config;

    /* Check if already initialized */
    if (prv_inst.is_initialized) {
        LOG_WRN("Zmod Internal watchdog already initialized");
        return -EALREADY;
    }

    /* Initialize private instance */
    prv_inst.wdt_dev = NULL;
    prv_inst.wdt_channel_id = -1;
    atomic_set(&prv_inst.feed_enabled, 1); /* Initialize to enabled */
    prv_inst.last_feed_time32 = k_uptime_get_32();
    prv_inst.thread_started = false;

    /* Initialize warning timer */
    k_timer_init(&prv_inst.warning_timer, prv_warning_timer_callback, NULL);

#ifdef CONFIG_ZMOD_IWDOG_ZBUS_PUBLISH
    /* Initialize work item for ISR-safe Zbus publishing */
    k_work_init(&prv_inst.warning_work, prv_warning_work_handler);
#endif

#ifdef CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING
    /* Initialize panic timer and atomic flag */
    k_timer_init(&prv_inst.panic_timer, prv_panic_timer_callback, NULL);
    atomic_set(&prv_inst.did_panic, 0);
#endif

    /* Get watchdog device using standard alias */
    prv_inst.wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    if (!device_is_ready(prv_inst.wdt_dev)) {
        LOG_ERR("Zmod Internal watchdog device not ready");
        return -ENODEV;
    }

    /* Configure watchdog */
    wdt_config.flags = WDT_FLAG_RESET_SOC;
    wdt_config.window.min = 0;
    wdt_config.window.max = CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS;
    wdt_config.callback = NULL;

    /* Install timeout */
    prv_inst.wdt_channel_id = wdt_install_timeout(prv_inst.wdt_dev, &wdt_config);
    if (prv_inst.wdt_channel_id < 0) {
        LOG_ERR("Failed to install Zmod iwdog timeout: %d", prv_inst.wdt_channel_id);
        return prv_inst.wdt_channel_id;
    }

    /* Start watchdog */
    ret = wdt_setup(prv_inst.wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (ret < 0) {
        LOG_ERR("Failed to setup Zmod iwdog: %d", ret);
        return ret;
    }

    /* Start warning timer - fires at configured percentage of timeout period */
    uint32_t warning_timeout =
        (CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS * CONFIG_ZMOD_IWDOG_WARNING_PCT) / 100;
    k_timer_start(&prv_inst.warning_timer, K_MSEC(warning_timeout), K_NO_WAIT);

#ifdef CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING
    /* Start panic timer - fires shortly before reset */
    uint32_t panic_timeout =
        CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS - CONFIG_ZMOD_IWDOG_LOG_PANIC_THRESHOLD_MS;
    k_timer_start(&prv_inst.panic_timer, K_MSEC(panic_timeout), K_NO_WAIT);
#endif

    LOG_INF("Zmod Internal watchdog module v%s initialized with %d ms timeout "
            "(warning at %d ms).",
            ZMOD_IWDOG_VERSION_STRING,
            CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS,
            warning_timeout);

#ifdef CONFIG_ZMOD_IWDOG_AUTO_START_THREAD
    zmod_iwdog_start_service_thread();
#else
    LOG_INF("Zmod IWDOG thread auto-start disabled. Call zmod_iwdog_start_service_thread() "
            "to begin feeding.");
#endif

    /* Mark as successfully initialized */
    prv_inst.is_initialized = true;
    return 0;
}

void zmod_iwdog_feed(void) {
    int ret;

    if (prv_inst.wdt_dev == NULL || prv_inst.wdt_channel_id < 0) {
        LOG_ERR("Zmod Internal watchdog not initialized");
        return;
    }

    ret = wdt_feed(prv_inst.wdt_dev, prv_inst.wdt_channel_id);
    if (ret < 0) {
        LOG_ERR("Failed to feed Zmod iwdog: %d", ret);
    } else {
        /* Update last feed time and restart timers */
        prv_inst.last_feed_time32 = k_uptime_get_32();
        uint32_t warning_timeout =
            (CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS * CONFIG_ZMOD_IWDOG_WARNING_PCT) / 100;
        k_timer_start(&prv_inst.warning_timer, K_MSEC(warning_timeout), K_NO_WAIT);

#ifdef CONFIG_ZMOD_IWDOG_LOG_PANIC_ON_WARNING
        /* Reset panic timer and flag */
        atomic_set(&prv_inst.did_panic, 0);
        uint32_t panic_timeout =
            CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS - CONFIG_ZMOD_IWDOG_LOG_PANIC_THRESHOLD_MS;
        k_timer_start(&prv_inst.panic_timer, K_MSEC(panic_timeout), K_NO_WAIT);
#endif
    }
}

void zmod_iwdog_start_service_thread(void) {
    /* Check if thread already started */
    if (prv_inst.thread_started) {
        LOG_WRN("Zmod Internal watchdog service thread already started");
        return;
    }

    k_thread_create(&prv_inst.thread_data,
                    prv_thread_stack,
                    K_THREAD_STACK_SIZEOF(prv_thread_stack),
                    prv_thread,
                    NULL,
                    NULL,
                    NULL,
                    ZMOD_IWDOG_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&prv_inst.thread_data, "zmod_iwdog");
    prv_inst.thread_started = true;
    LOG_INF("Zmod Internal watchdog service thread started");
}

/****************************************************************
 * Shell Commands
 ****************************************************************/

#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

/**
 * @brief Thread-safe setter for feed_enabled
 *
 * @param enabled New feed enabled state
 */
static void prv_set_feed_enabled(bool enabled) {
    atomic_set(&prv_inst.feed_enabled, enabled ? 1 : 0);
}

/**
 * @brief Shell command to enable Zmod iwdog feeding
 *
 * @param sh Shell instance
 * @param argc Argument count
 * @param argv Argument values
 */
static void prv_shell_zmod_iwdog_enable(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    prv_set_feed_enabled(true);
    shell_print(sh, "Zmod Internal watchdog feeding enabled");
}

/**
 * @brief Shell command to disable Zmod iwdog feeding
 *
 * @param sh Shell instance
 * @param argc Argument count
 * @param argv Argument values
 */
static void prv_shell_zmod_iwdog_disable(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    prv_set_feed_enabled(false);
    shell_print(sh,
                "Zmod Internal watchdog feeding disabled - system will reset in %d ms",
                CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS);
}

/**
 * @brief Shell command to show Zmod iwdog status
 *
 * @param sh Shell instance
 * @param argc Argument count
 * @param argv Argument values
 */
static void prv_shell_zmod_iwdog_status(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Zmod Internal watchdog status:");
    shell_print(sh, "  Device: %s", prv_inst.wdt_dev ? "initialized" : "not initialized");
    shell_print(sh, "  Channel: %d", prv_inst.wdt_channel_id);
    shell_print(sh, "  Feeding: %s", prv_get_feed_enabled() ? "enabled" : "disabled");
    shell_print(sh, "  Timeout: %d ms", CONFIG_ZMOD_WATCHDOG_TIMEOUT_MS);
    shell_print(sh, "  Feed interval: %d ms", CONFIG_ZMOD_WATCHDOG_FEED_INTERVAL_MS);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    zmod_iwdog_cmds,
    SHELL_CMD_ARG(enable, NULL, "Enable Zmod iwdog feeding", prv_shell_zmod_iwdog_enable, 1, 0),
    SHELL_CMD_ARG(disable,
                  NULL,
                  "Disable Zmod iwdog feeding (for testing)",
                  prv_shell_zmod_iwdog_disable,
                  1,
                  0),
    SHELL_CMD_ARG(status, NULL, "Show Zmod iwdog status", prv_shell_zmod_iwdog_status, 1, 0),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(zmod_iwdog, &zmod_iwdog_cmds, "Zmod Internal watchdog commands", NULL);

#endif /* CONFIG_SHELL */