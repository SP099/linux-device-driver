/*
 * Single Pseudo Character Driver
 *
 * This kernel module creates one virtual character device: /dev/pcd
 * It acts like a small RAM-based file (512 bytes) that you can read/write
 * using standard file operations (cat, echo, dd, etc.).
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/err.h>

/* Print format for all kernel messages - makes dmesg output clear */
#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/* ====================== Configuration ====================== */
#define DEV_MEM_SIZE    512     /* Size of virtual device memory in bytes */

/* ====================== Global Buffer ====================== */
/*
 * This is the actual kernel memory (RAM) used by our virtual device.
 * Think of it as a small RAM disk. All read/write operations happen here.
 */
static char device_buff[DEV_MEM_SIZE];

/* ====================== Driver Variables ====================== */
static dev_t device_number;           /* Holds major + minor number */
static struct cdev pcd_cdev;          /* Character device structure */
static struct class *class_pcd;       /* Device class for /sys/class */
static struct device *device_pcd;     /* Device entry for /dev/pcd */

/* ====================== File Operations ====================== */

static loff_t pcd_lseek(struct file *filp, loff_t offset, int whence)
{
    loff_t temp;

    pr_info("lseek requested\n");
    pr_info("current value of the file position = %lld\n", filp->f_pos);

    /*********************************************************************
     * lseek() allows user programs to change the current file position.
     *
     * Three modes supported (standard POSIX):
     *   SEEK_SET -> absolute position from beginning
     *   SEEK_CUR -> relative to current position
     *   SEEK_END -> relative to end of device
     *
     * Why bound checking?
     *   - Prevent reading/writing outside our allocated 512-byte buffer
     *   - Return -EINVAL as per standard for invalid arguments
     *********************************************************************/
    switch (whence) {
    case SEEK_SET:
        if ((offset > DEV_MEM_SIZE) || (offset < 0))
            return -EINVAL;
        filp->f_pos = offset;
        break;

    case SEEK_CUR:
        temp = filp->f_pos + offset;
        if (temp > DEV_MEM_SIZE || temp < 0)
            return -EINVAL;
        filp->f_pos = temp;
        break;

    case SEEK_END:
        temp = DEV_MEM_SIZE + offset;
        if (temp > DEV_MEM_SIZE || temp < 0)
            return -EINVAL;
        filp->f_pos = temp;
        break;

    default:
        return -EINVAL;
    }

    pr_info("new value of the file position = %lld\n", filp->f_pos);
    return filp->f_pos;
}

static ssize_t pcd_read(struct file *filp, char __user *buff,
                        size_t count, loff_t *f_pos)
{
    size_t bytes_to_read = count;

    pr_info("read requested for %zu bytes\n", count);
    pr_info("current file position = %lld\n", *f_pos);

    /*********************************************************************
     * Handle reading beyond device size:
     *   If user asks for more data than available, truncate the count.
     *
     * Why?
     *   - Prevent buffer overflow
     *   - Standard behavior for character devices
     *********************************************************************/
    if ((*f_pos + count) > DEV_MEM_SIZE)
        bytes_to_read = DEV_MEM_SIZE - *f_pos;

    if (bytes_to_read == 0)
        return 0;   /* EOF - no more data */

    /*********************************************************************
     * copy_to_user():
     *   Safely copies data from kernel space (device_buff) to user space.
     *
     * Why use device_buff + *f_pos instead of &device_buff[*f_pos]?
     *   - Both are equivalent here, but pointer arithmetic (device_buff + offset)
     *     is clearer and more common in kernel code.
     *
     * Return value:
     *   Non-zero if copy failed (bad user pointer or page fault)
     *********************************************************************/
    if (copy_to_user(buff, device_buff + *f_pos, bytes_to_read)) {
        return -EFAULT;
    }

    *f_pos += bytes_to_read;

    pr_info("Number of bytes successfully read = %zu\n", bytes_to_read);
    pr_info("Updated file position = %lld\n", *f_pos);
    return bytes_to_read;
}

static ssize_t pcd_write(struct file *filp, const char __user *buff,
                         size_t count, loff_t *f_pos)
{
    size_t bytes_to_write = count;

    pr_info("write requested for %zu bytes\n", count);
    pr_info("current file position = %lld\n", *f_pos);

    /*********************************************************************
     * Prevent writing beyond device size
     *********************************************************************/
    if ((*f_pos + count) > DEV_MEM_SIZE)
        bytes_to_write = DEV_MEM_SIZE - *f_pos;

    if (bytes_to_write == 0)
        return 0;   /* No space left */

    /*********************************************************************
     * copy_from_user():
     *   Safely copies data from user space to kernel space.
     *
     * Security reason:
     *   - User could pass malicious pointer; this function validates it.
     *********************************************************************/
    if (copy_from_user(device_buff + *f_pos, buff, bytes_to_write)) {
        return -EFAULT;
    }

    *f_pos += bytes_to_write;

    pr_info("Number of bytes successfully written = %zu\n", bytes_to_write);
    pr_info("Updated file position = %lld\n", *f_pos);
    return bytes_to_write;
}

static int pcd_open(struct inode *inode, struct file *filp)
{
    /*********************************************************************
     * open() is called when user does open("/dev/pcd", O_RDWR) etc.
     *
     * Here we do nothing extra because this is a simple RAM device.
     * In real drivers you might:
     *   - Allocate resources
     *   - Check permissions
     *   - Initialize hardware
     *********************************************************************/
    pr_info("open was successful\n");
    return 0;
}

static int pcd_release(struct inode *inode, struct file *filp)
{
    /*********************************************************************
     * release() is called when the last file descriptor is closed.
     *
     * Purpose:
     *   - Cleanup any resources allocated in open()
     *   - Here: nothing to cleanup for this simple driver
     *********************************************************************/
    pr_info("release was successful\n");
    return 0;
}

/* File operations structure - This tells the kernel which functions to call */
static struct file_operations pcd_fops = {
    .owner   = THIS_MODULE,
    .open    = pcd_open,
    .read    = pcd_read,
    .write   = pcd_write,
    .llseek  = pcd_lseek,
    .release = pcd_release,
};

/* ====================== Module Init / Exit ====================== */

static int __init pcd_driver_init(void)
{
    int ret;

    pr_info("Module loading started...\n");

    /*********************************************************************
     * Step 1: alloc_chrdev_region()
     *
     * Why?
     *   - Dynamically allocate a major number (recommended)
     *   - Reserves 1 minor number (we need only one device)
     *********************************************************************/
    ret = alloc_chrdev_region(&device_number, 0, 1, "pcd_devices");
    if (ret < 0) {
        pr_err("chardev alloc failed\n");
        goto out;
    }

    pr_info("Device number <major>:<minor> = %d:%d\n",
            MAJOR(device_number), MINOR(device_number));

    /*********************************************************************
     * Step 2: cdev_init() + cdev_add()
     *
     * Purpose:
     *   - Initialize character device structure
     *   - Link it with our file_operations (pcd_fops)
     *   - Register with kernel so VFS knows how to handle this device
     *********************************************************************/
    cdev_init(&pcd_cdev, &pcd_fops);
    pcd_cdev.owner = THIS_MODULE;

    ret = cdev_add(&pcd_cdev, device_number, 1);
    if (ret < 0) {
        pr_err("cdev add failed\n");
        goto unreg_chrdev;
    }

    /*********************************************************************
     * Step 3: class_create()
     *
     * Creates /sys/class/pcd_class/
     * Required for udev to automatically create /dev/pcd node
     *********************************************************************/
    class_pcd = class_create("pcd_class");
    if (IS_ERR(class_pcd)) {
        pr_err("class creation failed\n");
        ret = PTR_ERR(class_pcd);
        goto cdev_del;
    }

    /*********************************************************************
     * Step 4: device_create()
     *
     * Creates the actual device node: /dev/pcd
     *********************************************************************/
    device_pcd = device_create(class_pcd, NULL, device_number, NULL, "pcd");
    if (IS_ERR(device_pcd)) {
        pr_err("Device create failed\n");
        ret = PTR_ERR(device_pcd);
        goto class_del;
    }

    pr_info("Module init was successful\n");
    return 0;

    /* Error handling labels - cleanup in reverse order */
class_del:
    class_destroy(class_pcd);
cdev_del:
    cdev_del(&pcd_cdev);
unreg_chrdev:
    unregister_chrdev_region(device_number, 1);
out:
    pr_info("module insertion failed\n");
    return ret;
}

static void __exit pcd_driver_exit(void)
{
    /*********************************************************************
     * Cleanup order is very important!
     * Must be reverse of initialization order to avoid kernel warnings.
     *********************************************************************/
    device_destroy(class_pcd, device_number);
    class_destroy(class_pcd);
    cdev_del(&pcd_cdev);
    unregister_chrdev_region(device_number, 1);

    pr_info("module unloaded\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUSHANT PATIL (Enhanced with detailed comments by Grok)");
MODULE_DESCRIPTION("A simple pseudo character driver - single device");
MODULE_VERSION("1.1");
