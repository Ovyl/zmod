# Zmod - Zephyr Modules

This repository contains **out-of-tree Zephyr RTOS modules** developed and maintained by **[Ovyl](https://ovyl.io/)**.  
Each module provides reusable functionality (drivers, subsystems, utilities) that can be dropped into any Zephyr-based project.

---

## 📦 Modules

Each module has its own **Integration Guide** with detailed setup instructions.

- [BT (Bluetooth Peripheral)](bt/INTEGRATION.md)  
  Bluetooth LE peripheral stack with configurable advertising, optional Zbus events, and shell/NUS support.
- [Config Storage](config/INTEGRATION.md)  
  Persistent NVS-backed configuration manager with resettable/non-resettable entries and shell helpers.
- [IWDOG (Internal Watchdog)](iwdog/INTEGRATION.md)  
  High-level abstraction for hardware watchdog timer management in Zephyr applications.
- [Logging](logging/INTEGRATION.md)  
  Flash-backed logging backend using an FCB ring to capture and retrieve device logs.

> More modules will be added over time. See each module folder for details.

---

## 🚀 Getting Started

### 1. Add to `west.yml`

Add the repository to your manifest:

```yaml
manifest:
  remotes:
    - name: ovyl
      url-base: https://github.com/Ovyl

  projects:
    - name: ovyl-zephyr-modules
      remote: ovyl
      revision: main    # or a release tag/commit
      path: modules/ovyl
```

Update your workspace:

```bash
west update
```

### 2. Enable a module

Each module is integrated via Kconfig. For example, in `prj.conf`:

```conf
CONFIG_ZMOD_IWDOG=y
```

See the `Integration Guides` within each module for device tree and configuration details.

---
