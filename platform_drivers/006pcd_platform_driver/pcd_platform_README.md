# Pseudo Character Platform Driver

A robust, multi-device Linux character driver utilizing the modern Platform Bus Architecture and Device-Managed Resource Framework (`devm_`). This project demonstrates how to decouple hardware-specific configuration (Platform Devices) from core execution logic (Platform Driver), providing strict permission enforcement and boundary safety checks across multiple concurrent virtual device instances.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Device Node Matrix](#device-node-matrix)
- [Compilation & Loading](#compilation--loading)
- [Functional Test Cases](#functional-test-cases)
- [Driver Removal & Cleanup](#driver-removal--cleanup)

---

## System Architecture

The project splits responsibility into two distinct kernel modules that communicate over the virtual platform bus:

| Module | Role |
|---|---|
| `pcd_device_setup.ko` | Allocates and registers 4 distinct pseudo-devices, each with unique serial numbers, buffer capacities, and access permissions |
| `pcd_platform_driver.ko` | Listens for matching device signatures, manages dynamic driver tracking arrays, exposes char interfaces under `/dev`, and uses `devm_` to automatically track and free all device memory |

---

## Device Node Matrix

| Device Node | Buffer Size | Permission | Major:Minor | Enforcement Behavior |
|---|---|---|---|---|
| `/dev/pcdev-0` | 512 bytes | `RDWR (0x11)` | `239:0` | Full read & write with strict overflow protection |
| `/dev/pcdev-1` | 1024 bytes | `RDWR (0x11)` | `239:1` | Expanded buffer, full read & write |
| `/dev/pcdev-2` | 1024 bytes | `RDONLY (0x01)` | `239:2` | Write calls return `-EPERM` |
| `/dev/pcdev-3` | 1024 bytes | `WRONLY (0x10)` | `239:3` | Read calls return `-EPERM` |

---

## Compilation & Loading

### 1. Cross-Compilation (Target: ARM / Raspberry Pi)

Ensure `KDIR_CROSS` in the `Makefile` points to your compiled kernel source, then run:

```bash
make clean
make cross
```

### 2. Module Insertion Sequence

The platform driver must be loaded **before** the device setup module, since it acts as the matching listener for device registration events:

```bash
# Step 1: Load the driver core
sudo insmod pcd_platform_driver.ko

# Step 2: Load the device configurations
sudo insmod pcd_device_setup.ko
```

### 3. Verify System Mapping

Check that the driver successfully probed all four platform devices:

```bash
dmesg | tail -n 15
```

Expected output:

```
pcd_platform_driver_init :pcd platform driver loaded
pcd_platform_driver_probe :Device serial number = PCDEVSHP1111
pcd_platform_driver_probe :Device size = 512
pcd_platform_driver_probe :Device permission = 17
pcd_platform_driver_probe :Platform device probed
...
pcd_platform_driver_probe :Device serial number = PCDEVSHP4444
pcd_platform_driver_probe :Device size = 1024
pcd_platform_driver_probe :Device permission = 16
pcd_platform_driver_probe :Platform device probed
pcdev_platform_init : Device setup module loaded
```

Verify the device nodes were created:

```bash
ls -l /dev/pcdev-*
```

Expected:

```
crw------- 1 root root 239, 0 Jun  3 01:35 /dev/pcdev-0
crw------- 1 root root 239, 1 Jun  3 01:35 /dev/pcdev-1
crw------- 1 root root 239, 2 Jun  3 01:35 /dev/pcdev-2
crw------- 1 root root 239, 3 Jun  3 01:35 /dev/pcdev-3
```

---

## Functional Test Cases

> **Note:** Device nodes are restricted to root (`crw-------`). Run all test commands from a privileged root shell (`sudo su`) before executing file operations.

---

### Test 1 — Read/Write Integrity on `pcdev-0`

**Objective:** Verify standard read/write capability with correct VFS file position tracking.

```bash
echo "Kernel_Testing_Data" > /dev/pcdev-0
cat /dev/pcdev-0
```

Expected output:

```
Kernel_Testing_Data
```

---

### Test 2 — Write Blocked on Read-Only Node (`pcdev-2`)

**Objective:** Confirm the driver intercepts and rejects write operations on a `RDONLY` device.

```bash
echo "Illegal_Write" > /dev/pcdev-2
```

Expected shell output:

```
bash: /dev/pcdev-2: Operation not permitted
```

Expected kernel log (`dmesg`):

```
pcd_open : minor access = 2
pcd_open : open was unsuccessful
```

---

### Test 3 — Read Blocked on Write-Only Node (`pcdev-3`)

**Objective:** Confirm the driver blocks read operations on a `WRONLY` device.

```bash
cat /dev/pcdev-3
```

Expected shell output:

```
cat: /dev/pcdev-3: Operation not permitted
```

Expected kernel log (`dmesg`):

```
pcd_open : minor access = 3
pcd_open : open was unsuccessful
```

---

### Test 4 — Buffer Overflow Safeguard on `pcdev-0`

**Objective:** Verify that the driver truncates writes at the 512-byte hard limit without overrunning kernel memory.

```bash
dd if=/dev/zero of=/dev/pcdev-0 bs=1 count=1000
```

Expected output:

```
dd: error writing '/dev/pcdev-0': Cannot allocate memory
513+0 records in
512+0 records out
512 bytes copied, 0.0260352 s, 19.7 kB/s
```

The driver cleanly cuts off incoming data at byte 512, returning `-ENOMEM` to the caller. No buffer overrun occurs.

---

## Driver Removal & Cleanup

Unload modules in **reverse** order — device setup first, then the driver core:

```bash
sudo rmmod pcd_device_setup
sudo rmmod pcd_platform_driver
```

Verify clean teardown:

```bash
dmesg | tail
```

Expected:

```
pcd_platform_driver_remove :platform device removed
pcdev_release : Device Release
...
pcdev_platform_exit : Device setup module unloaded
pcd_platform_driver_cleanup :module unloaded
```

Because all allocations use `devm_kzalloc`, the kernel automatically reclaims all buffer memory during unbind — no manual `kfree` required, no leaks.

---

## Author

**Sushant Patil**  
Linux Device Driver Learning Repository
