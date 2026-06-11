/*
 * Simple "Hello World" Linux Kernel Module
 *
 * This is the most basic kernel module. It demonstrates:
 *   - How to write a minimal Linux kernel driver
 *   - Module initialization and cleanup
 *   - Kernel logging using pr_info()
 */

#include <linux/module.h>     /* Needed for all kernel modules */

/* 
 * Print format for kernel messages.
 * Using __func__ helps identify which function printed the message.
 */
#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/* ====================== Module Entry Point ====================== */

/**
 * hello_world_driver_init() - Module initialization function
 *
 * This function is called when the module is loaded into the kernel
 * using `insmod` or when the module is built-in.
 *
 * __init macro:
 *   - Tells kernel that this function is only used during initialization
 *   - Allows kernel to free the memory after init completes (saves RAM)
 *
 * Return: 0 on success, negative error code on failure
 */
static int __init hello_world_driver_init(void)
{
    /*********************************************************************
     * pr_info() - Kernel equivalent of printf()
     *
     * Why pr_info() instead of printk()?
     *   - pr_info() is a wrapper that defaults to KERN_INFO level
     *   - Messages appear in dmesg and kernel log
     *
     * You can see this message using:
     *   dmesg | tail
     *********************************************************************/
    pr_info("Hello world!!\n");

    pr_info("Module loaded successfully\n");
    return 0;   /* Success */
}

/* ====================== Module Exit Point ====================== */

/**
 * hello_world_driver_exit() - Module cleanup function
 *
 * This function is called when the module is removed using `rmmod`.
 *
 * __exit macro:
 *   - Marks function as exit-only (memory can be freed)
 *   - Not used if module is built-in to kernel
 */
static void __exit hello_world_driver_exit(void)
{
    /*********************************************************************
     * Cleanup code goes here.
     *
     * In this simple module:
     *   - No resources were allocated, so nothing to cleanup.
     *
     * In real drivers you would:
     *   - Unregister devices
     *   - Free memory
     *   - Release hardware resources
     *********************************************************************/
    pr_info("Good Bye\n");
    pr_info("Module unloaded successfully\n");
}

/* ====================== Module Registration ====================== */

/*
 * Tell the kernel which functions to call on load and unload.
 *
 * module_init()   → Registers initialization function
 * module_exit()   → Registers cleanup function
 */
module_init(hello_world_driver_init);
module_exit(hello_world_driver_exit);

/* ====================== Module Metadata ====================== */

/*
 * Module information visible with:
 *   modinfo hello_world_driver.ko
 */

MODULE_LICENSE("GPL");                    /* Required - GPL or compatible */
MODULE_AUTHOR("SUSHANT PATIL");           /* Your name */
MODULE_DESCRIPTION("This is my first custom driver");  /* Short description */
