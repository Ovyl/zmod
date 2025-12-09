/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file flash_log_backend.c
 * @brief Flash-based logging backend for persistent log storage
 */

#include <zmod/flash_log_backend.h>
#include <zmod/log_storage.h>

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>

#define FLASH_LOG_BUFFER_SIZE CONFIG_ZMOD_LOG_STORAGE_BUFFER_SIZE

static uint8_t flash_log_buf[FLASH_LOG_BUFFER_SIZE];

BUILD_ASSERT(FLASH_LOG_BUFFER_SIZE > 0, "Flash log buffer must be positive");

/** @brief Zephyr log_output callback that persists formatted logs. */
static int prv_flash_log_output_func(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);

    int ret = zmod_log_storage_add_data(data, length);

    if (ret < 0) {
        return ret;
    }

    return (int)length;
}

LOG_OUTPUT_DEFINE(flash_log_output, prv_flash_log_output_func, flash_log_buf, sizeof(flash_log_buf));

/** @brief Process a log message and route it into flash storage. */
static void prv_flash_log_backend_process(const struct log_backend *const backend, union log_msg_generic *msg)
{
    ARG_UNUSED(backend);

    uint32_t flags = LOG_OUTPUT_FLAG_LEVEL |
                     LOG_OUTPUT_FLAG_TIMESTAMP |
                     LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP |
                     LOG_OUTPUT_FLAG_CRLF_LFONLY;

    log_output_msg_process(&flash_log_output, &msg->log, flags);
}

/** @brief Initialize backend by priming log storage. */
static void prv_flash_log_backend_init(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);

    zmod_log_storage_init();
}

/** @brief Flush buffered output during a LOG_PANIC event. */
static void prv_flash_log_backend_panic(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);

    log_output_flush(&flash_log_output);
}

/** @brief Report dropped messages to the shared log output handler. */
static void prv_flash_log_backend_dropped(const struct log_backend *const backend, uint32_t cnt)
{
    ARG_UNUSED(backend);

    log_output_dropped_process(&flash_log_output, cnt);
}

static const struct log_backend_api flash_log_backend_api = {
    .process = prv_flash_log_backend_process,
    .dropped = prv_flash_log_backend_dropped,
    .panic = prv_flash_log_backend_panic,
    .init = prv_flash_log_backend_init,
};

LOG_BACKEND_DEFINE(flash_log_backend, flash_log_backend_api, true);
