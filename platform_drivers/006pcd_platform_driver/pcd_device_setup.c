/* ============================================================
 * FILE: pcd_device_setup.c
 *
 * PURPOSE:
 *   This module ONLY defines and registers platform devices.
 *   It represents the "board file" — the hardware description
 *   side. It does NOT contain any driver logic.
 *
 *   Think of it as: "Here is the hardware that exists."
 *   The platform driver (pcd_platform_driver.c) says: "I know
 *   how to drive that hardware."
 *
 * ANALOGY:
 *   platform_device  = a job posting (what hardware exists)
 *   platform_driver  = a candidate (who can handle it)
 *   platform bus     = HR that matches them together
 * ============================================================ */

#include<linux/module.h>
#include<linux/platform_device.h>

#include "platform.h"

/* ---- Redefine pr_fmt so every pr_info/pr_err automatically
 *      prints the function name as a prefix.
 *      e.g.  pr_info("hello\n")  prints:  "pcdev_platform_init : hello"
 * ---- */
#undef pr_fmt
#define pr_fmt(fmt) "%s : " fmt,__func__

/* ============================================================
 * Release Callback
 *
 * Every platform_device MUST have a .release() callback.
 * The kernel calls it when the last reference to the device
 * is dropped (i.e., after platform_device_unregister() and
 * all file handles are closed).
 *
 * Without this, the kernel prints a warning:
 *   "Device does not have a release() function"
 * and may oops on removal.
 * ============================================================ */
static void pcdev_release(struct device *dev)
{
    pr_info("Device Release\n");
}

/* ============================================================
 * Platform Data Array
 *
 * struct pcdev_platform_data is defined in platform.h:
 *   int   size;           -- how many bytes the device buffer holds
 *   int   perm;           -- RDWR / RDONLY / WRONLY
 *   const char *serial_number;  -- unique ID string
 *
 * We create one entry per device instance.
 * This data travels with the device and is read by the driver
 * inside probe() via dev_get_platdata(&pdev->dev).
 * ============================================================ */

/* create 2 platform data */
struct pcdev_platform_data pcdev_pdata[] = {
    /* Index 0 → /dev/pcdev-0 */
    [0] = {.size = 512, .perm = RDWR, .serial_number = "PCDEVSHP1111"},

    /* Index 0 → /dev/pcdev-1 */
    [1] = {.size = 1024,. perm = RDWR, .serial_number = "PCDEVSHP2222"},

    /* Index 0 → /dev/pcdev-2 */
    [2] = {.size = 1024,. perm = RDONLY, .serial_number = "PCDEVSHP3333"},

    /* Index 0 → /dev/pcdev-3 */
    [3] = {.size = 1024,. perm = WRONLY, .serial_number = "PCDEVSHP4444"}
};


/* ============================================================
 * Platform Device Definitions
 *
 * struct platform_device fields used here:
 *
 *   .name   — string the platform bus uses for driver matching.
 *             The platform_driver whose .driver.name equals this
 *             string will have its probe() called.
 *
 *   .id     — instance number. With 4 devices sharing the same
 *             .name, the id (0..3) makes each unique.
 *             The kernel creates sysfs entries like:
 *               /sys/bus/platform/devices/pseudo-char-device.0
 *               /sys/bus/platform/devices/pseudo-char-device.1  ...
 *             Use id = -1 only when there is exactly ONE instance.
 *
 *   .dev.platform_data — pointer to our pcdev_platform_data.
 *             Retrieved in probe() with dev_get_platdata().
 *
 *   .dev.release — mandatory cleanup callback (see above).
 * ============================================================ */

/* Device 0 — 512 bytes, RDWR */
struct platform_device platform_pcdev_1 = {
    .name  = "pseudo-char-device",     /* MUST match platform_driver.driver.name */
    .id    = 0,                        /* instance 0 → pcdev-0 */
    .dev = {
        .platform_data = &pcdev_pdata[0],  /* attach hardware config */
        .release = pcdev_release           /* mandatory release hook */
    }
};

/* Device 1 — 1024 bytes, RDWR */
struct platform_device platform_pcdev_2 = {
    .name  = "pseudo-char-device",
    .id    = 1,                       /* instance 1 → pcdev-1 */
    .dev = {
        .platform_data = &pcdev_pdata[1],
        .release = pcdev_release
    }
};

/* Device 2 — 1024 bytes, RDONLY */
struct platform_device platform_pcdev_3 = {
    .name  = "pseudo-char-device",
    .id    = 2,                       /* instance 2 → pcdev-2 */
    .dev = {
        .platform_data = &pcdev_pdata[2],
        .release = pcdev_release
    }
};

/* Device 3 — 1024 bytes, WRONLY */
struct platform_device platform_pcdev_4 = {
    .name  = "pseudo-char-device",
    .id    = 3,                       /* instance 3 → pcdev-3 */
    .dev = {
        .platform_data = &pcdev_pdata[3],
        .release = pcdev_release
    }
};

/* ============================================================
 * Pointer Array for Bulk Registration
 *
 * platform_add_devices() takes an array of pointers and
 * registers all of them in one call — cleaner than calling
 * platform_device_register() four times individually.
 * ============================================================ */
struct platform_device *platform_pcdevs[] = {
    &platform_pcdev_1,
    &platform_pcdev_2,
    &platform_pcdev_3,
    &platform_pcdev_4
};

/* ============================================================
 * Module Init — Register All Devices
 * ============================================================ */
static int __init pcdev_platform_init(void)
{
    int ret;

    /* platform_add_devices(array, count):
     *   Loops through the pointer array and calls
     *   platform_device_register() for each entry.
     *
     *   After this returns, the platform bus scans all
     *   registered platform_drivers. If any driver's .name
     *   matches "pseudo-char-device", its probe() is called
     *   immediately for each device — provided the driver
     *   module is already loaded.
     *
     *   If the driver is loaded AFTER this, probe() fires
     *   at driver registration time instead.                  */
    /* register platform device */
    //platform_device_register(&platform_pcdev_1);
    //platform_device_register(&platform_pcdev_2);

    ret = platform_add_devices(platform_pcdevs, ARRAY_SIZE(platform_pcdevs));

    if (ret)
    {
        pr_err("platform_add_devices failed\n");
        return ret;  /* propagate error to insmod */
    }

    pr_info("Device setup module loaded\n");
    return 0;
}

/* ============================================================
 * Module Exit — Unregister All Devices
 * ============================================================ */
static void __exit pcdev_platform_exit(void)
{
    int i;

    /* Unregister in reverse order to be safe.
     *
     * platform_device_unregister():
     *   1. Calls the driver's remove() if a driver is bound.
     *   2. Calls device.release() after all refs are dropped.
     *   3. Removes the sysfs entry.
     *
     * IMPORTANT: Unload pcd_device_setup BEFORE pcd_platform_driver.
     *   If you rmmod the driver first, the devices lose their driver
     *   but remain on the bus. That is fine. The reverse (rmmod
     *   devices first) triggers remove() cleanly via the driver.    */
    for (i = 0; i < ARRAY_SIZE(platform_pcdevs); i++)
        platform_device_unregister(platform_pcdevs[i]);
    pr_info("Device setup module unloaded\n");
}

module_init(pcdev_platform_init);
module_exit(pcdev_platform_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUSHANT PATIL");
MODULE_DESCRIPTION("Module Which Registers Platform Devices");
