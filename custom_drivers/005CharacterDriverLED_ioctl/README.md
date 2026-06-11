# GPIO Character Driver with `ioctl` Support

GPIO (LED) control via both `write()` and `ioctl()` system calls.

---

## Table of Contents

1. [Objective](#objective)
2. [Learning Goals](#learning-goals)
3. [Project Files](#project-files)
4. [Driver Architecture](#driver-architecture)
5. [Features](#features)
6. [ioctl Commands](#ioctl-commands)
7. [Why ioctl()?](#why-ioctl)
8. [Device Registration Flow](#device-registration-flow)
9. [GPIO Initialization Flow](#gpio-initialization-flow)
10. [Build & Usage](#build--usage)
11. [Kernel Logs](#kernel-logs)
12. [Resource Cleanup Flow](#resource-cleanup-flow)
13. [Important Notes](#important-notes)
14. [References](#references)

---

## Objective

This project demonstrates how to:

- Develop a Linux Character Device Driver
- Implement custom control commands using `ioctl()`
- Control GPIOs from kernel space
- Use the modern GPIO Descriptor API (`gpiod_*`)
- Communicate between user space and kernel space
- Create device nodes automatically using `udev`

---

## Learning Goals

After completing this project, you should understand:

- Character device registration
  - `alloc_chrdev_region()`
  - `cdev_init()` and `cdev_add()`
  - `class_create()` and `device_create()`
- File operations: `open()`, `read()`, `write()`, `release()`, `unlocked_ioctl()`
- GPIO Descriptor Framework
- User space ↔ Kernel space communication
- Error handling and cleanup in kernel modules

---

## Project Files

| File | Description |
|------|-------------|
| `gpio_char_ioctl_driver.c` | Main kernel module |
| `ioctlapp.c` | User-space test application |
| `Makefile` | Build script |
| `README.md` | Project documentation |

---

## Driver Architecture

```
User Application
       │
       ▼
┌──────────────────┐
│   VFS Layer      │
└──────────────────┘
       │
       ▼
┌──────────────────┐
│ Character Driver │
└──────────────────┘
       │
       ▼
┌──────────────────┐
│  GPIO Framework  │
│   (gpiod API)    │
└──────────────────┘
       │
       ▼
┌──────────────────┐
│  GPIO Hardware   │
│   (LED GPIO)     │
└──────────────────┘
```

---

## Features

- Automatic `/dev/testchar` creation via `udev`
- GPIO control through `write()`
- GPIO control through `ioctl()`
- GPIO state reading via `read()`
- GPIO toggle support
- Proper resource cleanup on module removal
- Linux Kernel 6.x compatible
- Detailed kernel logging using `pr_info()`

---

## ioctl Commands

| Command | Value | Description |
|---------|-------|-------------|
| `LED_ON` | `1` | Turn LED ON |
| `LED_OFF` | `0` | Turn LED OFF |
| `LED_TOGGLE` | `2` | Toggle LED state |
| `LED_STATUS` | `3` | Get current LED status |

---

## Why ioctl()?

Standard file operations are designed for data transfer:

```c
read()
write()
```

But drivers often need custom control commands such as:

- Reset device
- Enable / disable a feature
- Get device status
- Change configuration

For these operations, Linux provides:

```c
ioctl()   // Input Output Control
```

`ioctl()` allows user space to send arbitrary commands to a driver without overloading the `read()`/`write()` interface.

---

## Device Registration Flow

Executed during `module_init`:

```
alloc_chrdev_region()
        │
        ▼
   cdev_init()
        │
        ▼
    cdev_add()
        │
        ▼
  class_create()
        │
        ▼
 device_create()
        │
        ▼
 /dev/testchar  ← created by udev
```

---

## GPIO Initialization Flow

```
  gpiod_get()
       │
       ▼
GPIO Descriptor
       │
       ├── gpiod_set_value()   ← write output
       └── gpiod_get_value()   ← read input
```

### Modern vs. Deprecated API

The driver uses the **modern GPIO Descriptor API**. The legacy integer-based API is deprecated in kernel 6.x:

| Deprecated (old) | Recommended (new) |
|------------------|-------------------|
| `gpio_request()` | `gpiod_get()` |
| `gpio_direction_output()` | *(handled by gpiod_get flags)* |
| `gpio_set_value()` | `gpiod_set_value()` |
| `gpio_free()` | `gpiod_put()` |

---

## Build & Usage

### 1. Build

```bash
make
```

Expected output: `gpio_char_ioctl_driver.ko`

### 2. Insert Module

```bash
sudo insmod gpio_char_ioctl_driver.ko
```

Verify it loaded:

```bash
lsmod | grep gpio_char
dmesg | tail -20
```

### 3. Verify Device Node

```bash
ls -l /dev/testchar
```

Expected:

```
crw------- 1 root root <major>,<minor> /dev/testchar
```

### 4. Test with `ioctlapp`

```bash
sudo ./ioctlapp 1    # LED ON
sudo ./ioctlapp 0    # LED OFF
sudo ./ioctlapp 2    # Toggle LED
sudo ./ioctlapp 3    # Read status
```

### 5. Test with `write()` (shell)

```bash
echo 1 | sudo tee /dev/testchar    # LED ON
echo 0 | sudo tee /dev/testchar    # LED OFF
```

### 6. Read GPIO Status

```bash
cat /dev/testchar
```

Internally calls `gpiod_get_value()` and returns the current GPIO state.

### 7. Remove Module

```bash
sudo rmmod gpio_char_ioctl_driver
dmesg | tail
```

Expected log:

```
Driver removed successfully
```

---

## Kernel Logs

```bash
dmesg | tail -30
```

Example output:

```
test_init: Driver loaded successfully
test_write: LED turned ON
test_write: LED turned OFF
test_read: GPIO value = 1
```

---

## Resource Cleanup Flow

Executed during `module_exit` — **reverse order of initialization**:

```
gpiod_unexport()
       │
       ▼
  gpiod_put()
       │
       ▼
device_destroy()
       │
       ▼
class_destroy()
       │
       ▼
   cdev_del()
       │
       ▼
unregister_chrdev_region()
```

> **Note:** Cleanup order must be the exact reverse of initialization to avoid kernel oops or resource leaks.

---

## Important Notes

- This driver uses GPIO label `"led"` — ensure the label exists in your Device Tree.
- All test commands require `sudo`.
- Tested with **Linux Kernel 6.x**.
- Uses the **modern GPIO Descriptor Framework** (`gpiod_*`).
- Intended for learning Linux Device Driver development.

---
