# Copyright (c) 2025 Ovyl
# SPDX-License-Identifier: Apache-2.0

# Zmod Logging Module Integration Guide

## Overview

The Zmod Logging module provides a flash-backed Zephyr logging backend for
capturing runtime logs to an FCB ring buffer and exporting them later. This module requires data to be stored in NVS for tracking the FCB, so ensure `CONFIG_ZMOD_CONFIG`
is enabled and follow `config/INTEGRATION.md` before integrating logging.

## Features

- Flash Circular Buffer (FCB) storage for persistent logs
- Shell commands for exporting or clearing stored entries
- Programmable API for manual exports
- Persistent runtime log level management via Zmod Config

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

### 2. Enable the module

Add the following to your application Kconfig:

```conf
CONFIG_ZMOD_LOG_STORAGE=y
```

Optional Kconfig settings:

```conf
CONFIG_ZMOD_LOG_STORAGE_MIN_RUNTIME_LEVEL=1    # 1=ERR, 2=WRN, 3=INF, 4=DBG
CONFIG_ZMOD_LOG_STORAGE_BUFFER_SIZE=1024       # Shell export scratch buffer (bytes)
```

### 2. Reserve flash partitions

Partition Manager needs static partitions for NVS configuration data and the
logging FCB. Add the following to your `pm_static.yml`:

```yaml
nvs_storage:
  address: 0x15d000      # example address
  size: 0x4000           # 16 KB (aligned to erase block)
  region: flash_primary

logging_storage:
  address: 0x161000      # example address
  size: 0x4000           # 16 KB
  region: flash_primary
```

Adjust addresses/sizes to suit your layout; keep them aligned to erase blocks.

### 3. Initialize logging

Call the init helpers during application startup:

```c
#include <zmod/log_storage.h>

zmod_log_storage_init();
zmod_log_storage_init_log_level();
```

### 4. Optional shell support

If `CONFIG_SHELL` is enabled the module registers commands under `log_storage`
(`export`, `export_status`, `clear`, `list_log_levels`, `set_log_level`).

### 5. Export logs programmatically

When a shell isn't available you can pull logs manually:

```c
#include <zmod/log_storage.h>

uint8_t buffer[64];
size_t out = 0;
zmod_log_storage_set_export_in_progress(true);
zmod_log_storage_reset_read();

while (true) {
    int rc = zmod_log_storage_fetch_data(buffer, sizeof(buffer), &out);
    if (rc == -ENOENT) {
        break;  // no more data
    }
    if (rc < 0) {
        // handle error
        break;
    }

    // Process `buffer[0..out-1]`
}

zmod_log_storage_clear();
zmod_log_storage_set_export_in_progress(false);
```

### 6. Adjust log levels at runtime

Call `zmod_log_storage_set_log_level()` to change the runtime filter. The
module clamps requests below `CONFIG_ZMOD_LOG_STORAGE_MIN_RUNTIME_LEVEL`
(default `ERR`). Updated levels are persisted via the Zmod Config module.

## Configuration Options

| Option                                      | Description                                            | Default |
| ------------------------------------------- | ------------------------------------------------------ | ------- |
| `CONFIG_ZMOD_LOG_STORAGE`                   | Enables the logging module.                            | `n`     |
| `CONFIG_ZMOD_LOG_STORAGE_MIN_RUNTIME_LEVEL` | Lowest severity selectable at runtime (1=ERR … 4=DBG). | `1`     |
| `CONFIG_ZMOD_LOG_STORAGE_BUFFER_SIZE`       | Shell export scratch buffer size in bytes.             | `1024`  |
