/*
 * Copyright (c) 2025 Ovyl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file log_storage.c
 * @brief Flash-backed log storage implementation.
 */

#include <zmod/log_storage.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_internal.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/fs/fcb.h>


#include <zmod/config_mgr.h>
#include <zmod/configs.h>

LOG_MODULE_REGISTER(zmod_log_storage, CONFIG_ZMOD_LOG_STORAGE_LOG_LEVEL);

#define LOG_STORAGE_FLASH_LABEL logging_storage
#define LOG_STORAGE_FLASH_AREA_ID FLASH_AREA_ID(LOG_STORAGE_FLASH_LABEL)
#define LOG_STORAGE_FCB_MAGIC (0x1EE71065U)
#define LOG_STORAGE_SECTOR_SIZE_BYTES (4096U)
#define LOG_STORAGE_NUM_SECTORS (FIXED_PARTITION_SIZE(LOG_STORAGE_FLASH_LABEL) / LOG_STORAGE_SECTOR_SIZE_BYTES)
#define LOG_STORAGE_MUTEX_TIMEOUT_MS (200U)

#define LOG_RUNTIME_MIN_LEVEL CONFIG_ZMOD_LOG_STORAGE_MIN_RUNTIME_LEVEL

/** @brief Read cursor state for exported log data. */
typedef struct {
    struct fcb_entry head;
    size_t read_bytes;
} zmod_log_storage_read_ctx_t;

/** @brief Internal module state. */
typedef struct {
    const struct flash_area *fa;
    struct fcb fcb_inst;
    struct flash_sector sectors[LOG_STORAGE_NUM_SECTORS];
    zmod_log_storage_metadata_t metadata;
    struct k_mutex mutex;
    zmod_log_storage_read_ctx_t read_head;
    volatile bool export_in_progress;
} prv_log_storage_state_t;

static prv_log_storage_state_t prv_inst;

/** @brief Convert a Zephyr log severity level to a printable name. */
static const char *prv_get_log_level_name(uint8_t level)
{
    switch (level) {
        case LOG_LEVEL_NONE:
            return "OFF";
        case LOG_LEVEL_ERR:
            return "ERR";
        case LOG_LEVEL_WRN:
            return "WRN";
        case LOG_LEVEL_INF:
            return "INF";
        case LOG_LEVEL_DBG:
            return "DBG";
        default:
            return "UNK";
    }
}

typedef struct {
    const char *name;
    uint8_t level;
} prv_log_level_entry_t;

static const prv_log_level_entry_t prv_log_levels[] = {
    {"off", LOG_LEVEL_NONE},
    {"err", LOG_LEVEL_ERR},
    {"wrn", LOG_LEVEL_WRN},
    {"inf", LOG_LEVEL_INF},
    {"dbg", LOG_LEVEL_DBG},
};

/** @brief Resolve a textual log level into the matching table entry. */
static const prv_log_level_entry_t *prv_find_log_level(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    size_t input_len = strlen(name);

    for (size_t i = 0; i < ARRAY_SIZE(prv_log_levels); i++) {
        const char *candidate = prv_log_levels[i].name;
        size_t cand_len = strlen(candidate);

        if (input_len != cand_len) {
            continue;
        }

        bool match = true;
        for (size_t j = 0; j < cand_len; j++) {
            if ((char)tolower((unsigned char)name[j]) != candidate[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            return &prv_log_levels[i];
        }
    }

    return NULL;
}

int zmod_log_storage_init(void)
{
    if (prv_inst.fa != NULL) {
        return 0;
    }

    int ret = flash_area_open(LOG_STORAGE_FLASH_AREA_ID, &prv_inst.fa);

    if (ret < 0) {
        LOG_ERR("Failed to open flash area (ID %d): %d", LOG_STORAGE_FLASH_AREA_ID, ret);
        return ret;
    }

    uint32_t sector_count = LOG_STORAGE_NUM_SECTORS;
    ret = flash_area_sectors(prv_inst.fa, &sector_count, prv_inst.sectors);
    if (ret < 0) {
        LOG_ERR("Failed to read flash sector info: %d", ret);
        flash_area_close(prv_inst.fa);
        prv_inst.fa = NULL;
        return ret;
    }

    if (sector_count > ARRAY_SIZE(prv_inst.sectors)) {
        LOG_ERR("Partition reported %u sectors, expected <= %zu",
                sector_count,
                ARRAY_SIZE(prv_inst.sectors));
        flash_area_close(prv_inst.fa);
        prv_inst.fa = NULL;
        return -E2BIG;
    }

    memset(&prv_inst.fcb_inst, 0, sizeof(prv_inst.fcb_inst));
    prv_inst.fcb_inst.f_magic = LOG_STORAGE_FCB_MAGIC;
    prv_inst.fcb_inst.f_sectors = prv_inst.sectors;
    prv_inst.fcb_inst.f_sector_cnt = (uint8_t)sector_count;
    prv_inst.fcb_inst.f_scratch_cnt = 1U;

    ret = fcb_init(LOG_STORAGE_FLASH_AREA_ID, &prv_inst.fcb_inst);
    if (ret < 0) {
        LOG_ERR("Failed to initialize FCB: %d", ret);
        flash_area_close(prv_inst.fa);
        prv_inst.fa = NULL;
        return ret;
    }

    k_mutex_init(&prv_inst.mutex);
    zmod_log_storage_reset_read();
    prv_inst.export_in_progress = false;

    return 0;
}

int zmod_log_storage_add_data(const void *buf, size_t buf_size)
{
    if (buf == NULL) {
        return -EINVAL;
    }

    if (buf_size == 0U) {
        return 0;
    }

    if (prv_inst.export_in_progress) {
        return 0;
    }

    int ret = k_mutex_lock(&prv_inst.mutex, K_MSEC(LOG_STORAGE_MUTEX_TIMEOUT_MS));

    if (ret < 0) {
        if (!prv_inst.export_in_progress) {
            LOG_WRN("Failed to lock mutex.");
        }
        return -EBUSY;
    }

    struct fcb_entry loc = {0};

    ret = fcb_append(&prv_inst.fcb_inst, buf_size, &loc);

    if (ret == -ENOSPC) {
        ret = fcb_rotate(&prv_inst.fcb_inst);

        if (ret < 0) {
            if (!prv_inst.export_in_progress) {
                LOG_ERR("Failed to rotate sectors: %d", ret);
            }
            k_mutex_unlock(&prv_inst.mutex);

            return ret;
        }

        ret = fcb_append(&prv_inst.fcb_inst, buf_size, &loc);
    }

    if (ret < 0) {
        if (!prv_inst.export_in_progress) {
            LOG_ERR("Failed to get location to write to: %d", ret);
        }
        (void)fcb_clear(&prv_inst.fcb_inst);
        memset(&prv_inst.read_head, 0, sizeof(prv_inst.read_head));
        k_mutex_unlock(&prv_inst.mutex);
        return ret;
    }

    ret = flash_area_write(prv_inst.fa, FCB_ENTRY_FA_DATA_OFF(loc), buf, buf_size);

    if (ret < 0) {
        if (!prv_inst.export_in_progress) {
            LOG_ERR("Failed to write to flash: %d", ret);
        }
        k_mutex_unlock(&prv_inst.mutex);
        return ret;
    }

    ret = fcb_append_finish(&prv_inst.fcb_inst, &loc);
    if (ret < 0) {
        if (!prv_inst.export_in_progress) {
            LOG_ERR("Failed to finalize write: %d", ret);
        }
        k_mutex_unlock(&prv_inst.mutex);
        return ret;
    }

    k_mutex_unlock(&prv_inst.mutex);
    return 0;
}

int zmod_log_storage_fetch_data(void *dst, size_t dest_size, size_t *out_size)
{
    if (dst == NULL || out_size == NULL) {
        return -EINVAL;
    }

    int ret = k_mutex_lock(&prv_inst.mutex, K_MSEC(LOG_STORAGE_MUTEX_TIMEOUT_MS));
    if (ret < 0) {
        LOG_WRN("Failed to lock mutex.");
        return -EBUSY;
    }

    zmod_log_storage_read_ctx_t *ctx = &prv_inst.read_head;
    struct fcb_entry *loc = &prv_inst.read_head.head;

    if (loc->fe_sector == NULL || (loc->fe_sector && ctx->read_bytes == loc->fe_data_len)) {
        ctx->read_bytes = 0;
        ret = fcb_getnext(&prv_inst.fcb_inst, loc);

        if (ret < 0) {
            k_mutex_unlock(&prv_inst.mutex);
            return ret;
        }
    }

    uint16_t len = ctx->head.fe_data_len - ctx->read_bytes;
    if (len > dest_size) {
        len = dest_size;
    }

    ret = flash_area_read(prv_inst.fa, FCB_ENTRY_FA_DATA_OFF((*loc)) + ctx->read_bytes, dst, len);

    if (ret < 0) {
        LOG_ERR("Failed to read from flash %d", ret);
        k_mutex_unlock(&prv_inst.mutex);
        return -EIO;
    }

    ctx->read_bytes += len;
    *out_size = len;

    k_mutex_unlock(&prv_inst.mutex);

    return ret;
}

void zmod_log_storage_reset_read(void)
{
    memset(&prv_inst.read_head, 0, sizeof(prv_inst.read_head));
}

int zmod_log_storage_clear(void)
{
    k_mutex_lock(&prv_inst.mutex, K_MSEC(LOG_STORAGE_MUTEX_TIMEOUT_MS));

    int ret = fcb_clear(&prv_inst.fcb_inst);
    if (ret < 0) {
        LOG_ERR("Failed to clear FCB: %d", ret);
        k_mutex_unlock(&prv_inst.mutex);
        return ret;
    }

    memset(&prv_inst.read_head, 0, sizeof(prv_inst.read_head));

    k_mutex_unlock(&prv_inst.mutex);
    return 0;
}

void zmod_log_storage_set_export_in_progress(bool in_progress)
{
    prv_inst.export_in_progress = in_progress;
}

void zmod_log_storage_init_log_level(void)
{
    uint8_t log_level;
    bool use_default = false;

    if (zmod_config_mgr_get_value(CFG_LOG_LEVEL, &log_level, sizeof(log_level))) {
        if (log_level > LOG_LEVEL_DBG) {
            use_default = true;
        }
    } else {
        use_default = true;
    }

    if (use_default) {
        log_level = CONFIG_LOG_DEFAULT_LEVEL;
        zmod_config_mgr_set_value(CFG_LOG_LEVEL, &log_level, sizeof(log_level));
    }

    if (log_level < LOG_RUNTIME_MIN_LEVEL) {
        log_level = LOG_RUNTIME_MIN_LEVEL;
        LOG_WRN("Persisted log level is below minimum; clamping to %u", log_level);
        zmod_config_mgr_set_value(CFG_LOG_LEVEL, &log_level, sizeof(log_level));
    }

    uint32_t source_count = log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID);
    uint32_t set_count = 0;

    for (uint32_t source_id = 0; source_id < source_count; source_id++) {
        uint32_t result_level = log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, log_level);
        if (result_level == log_level) {
            set_count++;
        }
    }

    LOG_INF("Log level initialized: %u (applied to %u/%u modules)", log_level, set_count, source_count);
}

int zmod_log_storage_set_log_level(uint8_t level)
{
    if (level > LOG_LEVEL_DBG) {
        LOG_ERR("Invalid log level: %u. Valid levels: %u=ERR, %u=WRN, %u=INF, %u=DBG",
                level,
                LOG_LEVEL_ERR,
                LOG_LEVEL_WRN,
                LOG_LEVEL_INF,
                LOG_LEVEL_DBG);
        return -EINVAL;
    }

    uint8_t clamped_level = level;
    if (clamped_level < LOG_RUNTIME_MIN_LEVEL) {
        clamped_level = LOG_RUNTIME_MIN_LEVEL;
    }

    uint32_t source_count = log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID);

    for (uint32_t source_id = 0; source_id < source_count; source_id++) {
        log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, clamped_level);
    }

    if (!zmod_config_mgr_set_value(CFG_LOG_LEVEL, &clamped_level, sizeof(clamped_level))) {
        LOG_ERR("Failed to save log level to config");
        return -EIO;
    }

    if (clamped_level != level) {
        LOG_WRN("Requested level %u clamped to minimum runtime level %u", level, clamped_level);
    }

    const char *level_name = prv_get_log_level_name(clamped_level);
    LOG_INF("Log level set to: %s (%u)", level_name, clamped_level);

    return 0;
}

#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

/** @brief Shell command handler that reports export-in-progress state. */
static int prv_shell_print_export_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Log export in progress: %s", prv_inst.export_in_progress ? "true" : "false");
    return 0;
}

/** @brief Shell command handler to erase all stored log entries. */
static int prv_shell_log_storage_clear(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Clearing stored logs...");

    int ret = zmod_log_storage_clear();

    if (ret < 0) {
        shell_error(sh, "Failed to clear logs: %d", ret);
        return ret;
    }

    shell_print(sh, "Stored logs cleared.");
    return 0;
}

/** @brief Shell command handler that streams stored logs to the shell. */
static int prv_shell_log_storage_export(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint8_t buffer[64];
    struct fcb_entry entry = {0};
    bool previous_export_state = prv_inst.export_in_progress;

    int ret = k_mutex_lock(&prv_inst.mutex, K_MSEC(LOG_STORAGE_MUTEX_TIMEOUT_MS));
    if (ret < 0) {
        shell_error(sh, "Unable to lock log storage: %d", ret);
        return ret;
    }

    prv_inst.export_in_progress = true;

    ret = fcb_getnext(&prv_inst.fcb_inst, &entry);
    if (ret == -ENOENT) {
        shell_print(sh, "No stored log entries.");
        ret = 0;
        goto out;
    }

    while (ret >= 0) {
        uint32_t offset = FCB_ENTRY_FA_DATA_OFF(entry);
        uint16_t remaining = entry.fe_data_len;
        uint32_t pos = 0U;

        while (remaining > 0U) {
            uint16_t chunk = MIN((uint16_t)sizeof(buffer), remaining);
            int read_rc = flash_area_read(prv_inst.fa, offset + pos, buffer, chunk);
            if (read_rc < 0) {
                shell_error(sh, "Failed to read log entry: %d", read_rc);
                ret = read_rc;
                goto out;
            }

            char out_chunk[sizeof(buffer) + 1];
            memcpy(out_chunk, buffer, chunk);
            out_chunk[chunk] = '\0';
            shell_fprintf(sh, SHELL_VT100_COLOR_DEFAULT, "%s", out_chunk);
            remaining -= chunk;
            pos += chunk;
        }

        ret = fcb_getnext(&prv_inst.fcb_inst, &entry);
    }

    if (ret == -ENOENT) {
        ret = 0;
    }

out:
    prv_inst.export_in_progress = previous_export_state;
    k_mutex_unlock(&prv_inst.mutex);
    return ret;
}

/** @brief Print a table of compiled and runtime log levels for each module. */
static int prv_shell_list_module_log_levels(const struct shell *sh)
{
    uint32_t source_count = log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID);

    shell_print(sh, "Module Log Levels (%u modules):", source_count);
    shell_print(sh, "%-24s %-8s %-8s", "Module", "Runtime", "Compiled");
    shell_print(sh, "%-24s %-8s %-8s", "------", "-------", "--------");

    for (uint32_t source_id = 0; source_id < source_count; source_id++) {
        const char *source_name = log_source_name_get(Z_LOG_LOCAL_DOMAIN_ID, source_id);
        uint32_t runtime_level = log_filter_get(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, true);
        uint32_t compiled_level = log_filter_get(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, false);

        const char *runtime_name = prv_get_log_level_name(runtime_level);
        const char *compiled_name = prv_get_log_level_name(compiled_level);

        shell_print(sh,
                    "%-24s %-8s %-8s",
                    source_name ? source_name : "unknown",
                    runtime_name,
                    compiled_name);
    }

    shell_print(sh, "\nUse 'log_storage set_log_level <level>' to change runtime levels for all modules.");

    return 0;
}

/** @brief Shell helper that lists available severity names and current levels. */
static int prv_shell_log_storage_list_levels(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Available severity levels:");
    for (size_t i = 0; i < ARRAY_SIZE(prv_log_levels); i++) {
        shell_print(sh, "  %s", prv_log_levels[i].name);
    }

    shell_print(sh, "\nModule log level summary:");
    return prv_shell_list_module_log_levels(sh);
}

/** @brief Shell command handler for updating the runtime log level. */
static int prv_shell_log_storage_set_level_cmd(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Missing level argument. Usage: log_storage set_log_level <err|wrn|inf|dbg|1-4>");
        return -EINVAL;
    }

    uint8_t level;
    const prv_log_level_entry_t *entry = prv_find_log_level(argv[1]);

    if (entry != NULL) {
        level = entry->level;
    } else {
        char *endptr = NULL;
        long numeric = strtol(argv[1], &endptr, 10);
        if ((endptr == NULL) || (*endptr != '\0') || numeric < LOG_RUNTIME_MIN_LEVEL || numeric > LOG_LEVEL_DBG) {
            shell_error(sh, "Invalid level '%s'. Use one of: err, wrn, inf, dbg, or 1-4.", argv[1]);
            return -EINVAL;
        }
        level = (uint8_t)numeric;
    }

    int ret = zmod_log_storage_set_log_level(level);
    if (ret < 0) {
        shell_error(sh, "Failed to set log level: %d", ret);
        return ret;
    }

    uint8_t clamped = level < LOG_RUNTIME_MIN_LEVEL ? LOG_RUNTIME_MIN_LEVEL : level;
    const char *name = prv_get_log_level_name(clamped);
    shell_print(sh, "Log level set to %s (%u).", name, (unsigned int)clamped);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(log_storage_cmds,
                               SHELL_CMD_ARG(export_status,
                                             NULL,
                                             "Print log export status.\n"
                                             "usage:\n"
                                             "$ log_storage export_status\n",
                                             prv_shell_print_export_status,
                                             1,
                                             0),
                               SHELL_CMD_ARG(clear,
                                             NULL,
                                             "Erase all stored log entries.\n"
                                             "usage:\n"
                                             "$ log_storage clear\n",
                                             prv_shell_log_storage_clear,
                                             1,
                                             0),
                               SHELL_CMD_ARG(export,
                                             NULL,
                                             "Stream stored log entries as plain text.\n"
                                             "usage:\n"
                                             "$ log_storage export\n",
                                             prv_shell_log_storage_export,
                                             1,
                                             0),
                               SHELL_CMD_ARG(list_log_levels,
                                             NULL,
                                             "List current module log levels and available severities.\n"
                                             "usage:\n"
                                             "$ log_storage list_log_levels\n",
                                             prv_shell_log_storage_list_levels,
                                             1,
                                             0),
                               SHELL_CMD_ARG(set_log_level,
                                             NULL,
                                             "Set runtime log level for all modules (minimum 'err').\n"
                                             "usage:\n"
                                             "$ log_storage set_log_level <err|wrn|inf|dbg|1-4>\n",
                                             prv_shell_log_storage_set_level_cmd,
                                             2,
                                             0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(log_storage, &log_storage_cmds, "Log storage commands", NULL);

#endif /* CONFIG_SHELL */
