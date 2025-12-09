/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file log_storage.h
 * @brief Flash-backed log storage interface.
 */

#ifndef ZMOD_LOG_STORAGE_H
#define ZMOD_LOG_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <zephyr/fs/fcb.h>

/**
 * @brief Metadata persisted alongside the flash circular buffer.
 *
 * The structure is written to flash to track the head and tail positions
 * of the log storage, enabling recovery after resets.
 */
typedef struct zmod_log_storage_metadata_t {
    uint32_t magic;          /**< Magic word indicating a valid metadata block. */
    struct fcb_entry head;   /**< Cached head entry for the ring buffer. */
    struct fcb_entry tail;   /**< Cached tail entry for the ring buffer. */
} zmod_log_storage_metadata_t;

/**
 * @brief Initialize the flash-backed log storage subsystem.
 *
 * Opens the configured flash partition, sets up the underlying FCB instance,
 * and prepares the module mutex.
 *
 * @retval 0 Success.
 * @retval -E2BIG Reported sector count exceeds internal buffer.
 * @retval Negative errno value from underlying flash/FCB helpers.
 */
int zmod_log_storage_init(void);

/**
 * @brief Append raw log data to persistent storage.
 *
 * @param buf Pointer to the log record buffer.
 * @param buf_size Number of bytes to write; zero is treated as a no-op.
 *
 * @retval 0 Success.
 * @retval -EINVAL When @p buf is NULL.
 * @retval -EBUSY Unable to obtain mutex within timeout.
 * @retval Negative errno value from flash/FCB APIs.
 */
int zmod_log_storage_add_data(const void *buf, size_t buf_size);

/**
 * @brief Fetch the next chunk of stored log bytes.
 *
 * Callers should continue invoking this function until it returns -ENOENT.
 *
 * @param dst Destination buffer to populate.
 * @param dest_size Destination buffer size in bytes.
 * @param out_size Populated with the number of bytes written to @p dst.
 *
 * @retval 0 Success.
 * @retval -ENOENT No additional data is available.
 * @retval -EBUSY Unable to obtain mutex within timeout.
 * @retval -EINVAL Invalid arguments.
 * @retval -EIO Flash read failure.
 */
int zmod_log_storage_fetch_data(void *dst, size_t dest_size, size_t *out_size);

/**
 * @brief Reset the internal read cursor used during exports.
 */
void zmod_log_storage_reset_read(void);

/**
 * @brief Clear all stored log entries from flash.
 *
 * @retval 0 Success.
 * @retval Negative errno value from FCB operations.
 */
int zmod_log_storage_clear(void);

/**
 * @brief Mark whether a log export is currently in progress.
 *
 * When @p in_progress is true the module pauses automatic writes to avoid
 * race conditions with active exports.
 *
 * @param in_progress Flag indicating export state.
 */
void zmod_log_storage_set_export_in_progress(bool in_progress);

/**
 * @brief Initialize runtime log levels from persisted configuration.
 *
 * Reads log level from the Zmod Config module, applies minimum constraints,
 * and propagates the level to all registered modules.
 */
void zmod_log_storage_init_log_level(void);

/**
 * @brief Update the runtime log level for all modules and persist it.
 *
 * @param level Requested Zephyr log severity (1-4).
 *
 * @retval 0 Success.
 * @retval -EINVAL Requested level is invalid.
 * @retval -EIO Unable to persist the updated level.
 */
int zmod_log_storage_set_log_level(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif /* ZMOD_LOG_STORAGE_H */
