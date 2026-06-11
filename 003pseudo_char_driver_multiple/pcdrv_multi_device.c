/*
 * Pseudo Character Driver supporting multiple devices
 *
 * This kernel module creates 4 virtual character devices:
 *   /dev/pcd-0 (Read Only)
 *   /dev/pcd-1 (Write Only)
 *   /dev/pcd-2 (Read-Write)
 *   /dev/pcd-3 (Read-Write)
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/err.h>

/* Print format for all kernel messages */
#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/* ====================== Configuration ====================== */
#define NO_OF_DEVICES           4
#define MEM_SIZE_MAX_PCDEV1     1024
#define MEM_SIZE_MAX_PCDEV2     1024
#define MEM_SIZE_MAX_PCDEV3     1024
#define MEM_SIZE_MAX_PCDEV4     1024

/* Permission flags for each device */
#define DEV_PERM_RDONLY         0x01
#define DEV_PERM_WRONLY         0x10
#define DEV_PERM_RDWR           0x11

/* ====================== Global Buffers ====================== */
/*
 * These are the actual memory areas (RAM) used by each virtual device.
 * Think of them as small RAM disks for each /dev/pcd-X device.
 */
static char device_buff_pcdev1[MEM_SIZE_MAX_PCDEV1];
static char device_buff_pcdev2[MEM_SIZE_MAX_PCDEV2];
static char device_buff_pcdev3[MEM_SIZE_MAX_PCDEV3];
static char device_buff_pcdev4[MEM_SIZE_MAX_PCDEV4];

/* ====================== Data Structures ====================== */

/**
 * struct pcdev_private_data - Private data for EACH device
 *
 * Why do we need this structure?
 *   - Each of the 4 devices must maintain its own state (buffer, size, permission, etc.)
 *   - We embed 'struct cdev' inside it so that from file operations (open/read/write)
 *     we can retrieve this private data using container_of() macro.
 */
struct pcdev_private_data {
    char *buffer;               /* Pointer to this device's memory buffer */
    unsigned int size;          /* Size of the buffer (1024 bytes) */
    const char *serial_number;  /* Unique ID for debugging */
    int permission;             /* RDONLY / WRONLY / RDWR */
    struct cdev cdev;           /* Embedded cdev - kernel's representation of char device */
};

/**
 * struct pcdrv_private_data - Driver wide information
 *
 * Purpose:
 *   - Holds information common to the whole driver (major number, class, etc.)
 *   - Contains array of per-device private data
 */
struct pcdrv_private_data {
    int total_devices;
    dev_t dev_number;                    /* Major number + base minor number */
    struct class *class_pcd;             /* Device class for /sys/class/pcd_class */
    struct device *device_pcd[NO_OF_DEVICES];  /* Array of device pointers for cleanup */
    struct pcdev_private_data pcdev_data[NO_OF_DEVICES];
};

/* Global driver instance */
static struct pcdrv_private_data pcdrv_data = {
    .total_devices = NO_OF_DEVICES,
    .pcdev_data = {
        [0] = {
            .buffer         = device_buff_pcdev1,
            .size           = MEM_SIZE_MAX_PCDEV1,
            .serial_number  = "PCDEV1XYZ123",
            .permission     = DEV_PERM_RDONLY,
        },
        [1] = {
            .buffer         = device_buff_pcdev2,
            .size           = MEM_SIZE_MAX_PCDEV2,
            .serial_number  = "PCDEV2XYZ123",
            .permission     = DEV_PERM_WRONLY,
        },
        [2] = {
            .buffer         = device_buff_pcdev3,
            .size           = MEM_SIZE_MAX_PCDEV3,
            .serial_number  = "PCDEV3XYZ123",
            .permission     = DEV_PERM_RDWR,
        },
        [3] = {
            .buffer         = device_buff_pcdev4,
            .size           = MEM_SIZE_MAX_PCDEV4,
            .serial_number  = "PCDEV4XYZ123",
            .permission     = DEV_PERM_RDWR,
        },
    }
};

/* ====================== Permission Check ====================== */

/**
 * check_permission() - Validate access mode against device permission
 *
 * Why this function?
 *   - Different devices have different access rights (e.g., pcd-0 is read-only)
 *   - We must enforce this during open() to prevent unauthorized access
 *
 * How it works:
 *   - RDWR devices allow everything
 *   - RDONLY allows only read
 *   - WRONLY allows only write
 */
static int check_permission(int dev_perm, fmode_t acc_mode)
{
    if (dev_perm == DEV_PERM_RDWR)
        return 0;

    if ((dev_perm == DEV_PERM_RDONLY) &&
        ((acc_mode & FMODE_READ) && !(acc_mode & FMODE_WRITE)))
        return 0;

    if ((dev_perm == DEV_PERM_WRONLY) &&
        ((acc_mode & FMODE_WRITE) && !(acc_mode & FMODE_READ)))
        return 0;

    return -EPERM;   /* Permission denied */
}

/* ====================== File Operations ====================== */

static int pcd_open(struct inode *inode, struct file *filp)
{
    int ret;
    struct pcdev_private_data *pcdev_data;

    /*********************************************************************
     * container_of() magic:
     *
     * The kernel gives us inode->i_cdev.
     * But we embedded struct cdev inside struct pcdev_private_data.
     * container_of() calculates the address of the containing structure.
     *
     * Why do we need this?
     *   - Allows us to access per-device private data (buffer, size, permission)
     *     from any file operation.
     *********************************************************************/
    pcdev_data = container_of(inode->i_cdev, struct pcdev_private_data, cdev);

    /* Store it in filp->private_data for fast access in read/write/llseek */
    filp->private_data = pcdev_data;

    ret = check_permission(pcdev_data->permission, filp->f_mode);

    pr_info("Open device minor=%d %s\n",
            MINOR(inode->i_rdev), ret ? "FAILED" : "SUCCESS");

    return ret;
}

static loff_t pcd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct pcdev_private_data *pcdev_data = filp->private_data;
    loff_t new_pos = 0;

    pr_info("lseek called: offset=%lld, whence=%d, current_pos=%lld\n",
            offset, whence, filp->f_pos);

    /*********************************************************************
     * SEEK_SET : Set position to absolute offset
     * SEEK_CUR : Set position relative to current position
     * SEEK_END : Set position relative to end of device (size)
     *
     * Why check bounds?
     *   - Prevent reading/writing outside allocated buffer
     *   - Return -EINVAL (Invalid argument) as per POSIX
     *********************************************************************/
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = filp->f_pos + offset;
        break;
    case SEEK_END:
        new_pos = pcdev_data->size + offset;
        break;
    default:
        return -EINVAL;
    }

    /* Validate new position is within device memory */
    if (new_pos < 0 || new_pos > pcdev_data->size)
        return -EINVAL;

    filp->f_pos = new_pos;
    pr_info("New file position: %lld\n", filp->f_pos);

    return new_pos;
}

static ssize_t pcd_read(struct file *filp, char __user *ubuf,
                        size_t count, loff_t *f_pos)
{
    struct pcdev_private_data *pcdev_data = filp->private_data;
    size_t bytes_to_read;

    pr_info("Read requested: %zu bytes, pos=%lld\n", count, *f_pos);

    /*********************************************************************
     * EOF Handling:
     *   If current position >= device size → return 0 (End of File)
     *
     * Why min(count, remaining)?
     *   - User may ask for more bytes than available
     *   - Prevent buffer overflow
     *********************************************************************/
    if (*f_pos >= pcdev_data->size)
        return 0;

    bytes_to_read = min(count, (size_t)(pcdev_data->size - *f_pos));

    /*********************************************************************
     * copy_to_user():
     *   Copies data from kernel space -> user space
     *
     * Why check return value?
     *   - It returns number of bytes NOT copied
     *   - Non-zero → partial copy or fault (bad user pointer)
     *********************************************************************/
    if (copy_to_user(ubuf, pcdev_data->buffer + *f_pos, bytes_to_read))
        return -EFAULT;

    *f_pos += bytes_to_read;

    pr_info("Successfully read %zu bytes, new pos=%lld\n", bytes_to_read, *f_pos);
    return bytes_to_read;
}

static ssize_t pcd_write(struct file *filp, const char __user *ubuf,
                         size_t count, loff_t *f_pos)
{
    struct pcdev_private_data *pcdev_data = filp->private_data;
    size_t bytes_to_write;

    pr_info("Write requested: %zu bytes, pos=%lld\n", count, *f_pos);

    if (*f_pos >= pcdev_data->size)
        return 0;   /* No space left - treat as EOF for write */

    bytes_to_write = min(count, (size_t)(pcdev_data->size - *f_pos));

    /*********************************************************************
     * copy_from_user():
     *   Copies data from user space -> kernel space
     *
     * Safety:
     *   - Prevents user from reading kernel memory (security)
     *   - Handles page faults gracefully
     *********************************************************************/
    if (copy_from_user(pcdev_data->buffer + *f_pos, ubuf, bytes_to_write))
        return -EFAULT;

    *f_pos += bytes_to_write;

    pr_info("Successfully wrote %zu bytes, new pos=%lld\n", bytes_to_write, *f_pos);
    return bytes_to_write;
}

static int pcd_release(struct inode *inode, struct file *filp)
{
    pr_info("Release called for minor=%d\n", MINOR(inode->i_rdev));
    return 0;
}

/* File operations structure - This is the bridge between VFS and our driver */
static const struct file_operations pcd_fops = {
    .owner   = THIS_MODULE,
    .open    = pcd_open,
    .read    = pcd_read,
    .write   = pcd_write,
    .llseek  = pcd_llseek,
    .release = pcd_release,
};

/* ====================== Module Init / Exit ====================== */

static int __init pcd_driver_init(void)
{
    int ret, i;

    pr_info("Module loading started...\n");

    /*********************************************************************
     * Step 1: alloc_chrdev_region()
     *
     * Why?
     *   - Dynamically allocate major number (preferred over static)
     *   - Reserves NO_OF_DEVICES minor numbers starting from 0
     *
     * Return: dev_number contains MAJOR + base MINOR
     *********************************************************************/
    ret = alloc_chrdev_region(&pcdrv_data.dev_number, 0, NO_OF_DEVICES, "pcdevs");
    if (ret < 0) {
        pr_err("Failed to allocate device numbers\n");
        return ret;
    }

    /*********************************************************************
     * Step 2: class_create()
     *
     * Purpose:
     *   - Creates entry in /sys/class/pcd_class/
     *   - Used by udev to automatically create /dev nodes
     *********************************************************************/
    pcdrv_data.class_pcd = class_create("pcd_class");
    if (IS_ERR(pcdrv_data.class_pcd)) {
        pr_err("Failed to create device class\n");
        ret = PTR_ERR(pcdrv_data.class_pcd);
        goto err_unreg;
    }

    /* Step 3: Create each device */
    for (i = 0; i < NO_OF_DEVICES; i++) {
        struct pcdev_private_data *pcdev = &pcdrv_data.pcdev_data[i];
        dev_t dev = pcdrv_data.dev_number + i;

        pr_info("Creating pcd-%d (major:minor = %d:%d)\n",
                i, MAJOR(dev), MINOR(dev));

        /*********************************************************************
         * cdev_init() + cdev_add()
         *
         * Why?
         *   - Links our file_operations (pcd_fops) with this device
         *   - Tells kernel how to handle open/read/write on this cdev
         *********************************************************************/
        cdev_init(&pcdev->cdev, &pcd_fops);
        pcdev->cdev.owner = THIS_MODULE;

        ret = cdev_add(&pcdev->cdev, dev, 1);
        if (ret < 0) {
            pr_err("cdev_add failed for pcd-%d\n", i);
            goto err_cleanup;
        }

        /*********************************************************************
         * device_create()
         *
         * Creates the actual /dev/pcd-X node
         * Parameters: class, parent, devt, drvdata, format
         *********************************************************************/
        pcdrv_data.device_pcd[i] = device_create(pcdrv_data.class_pcd, NULL,
                                                 dev, NULL, "pcd-%d", i);
        if (IS_ERR(pcdrv_data.device_pcd[i])) {
            pr_err("device_create failed for pcd-%d\n", i);
            ret = PTR_ERR(pcdrv_data.device_pcd[i]);
            cdev_del(&pcdev->cdev);
            goto err_cleanup;
        }
    }

    pr_info("Module loaded successfully! %d devices created.\n", NO_OF_DEVICES);
    return 0;

err_cleanup:
    /* Rollback: destroy devices created so far */
    for (--i; i >= 0; i--) {
        device_destroy(pcdrv_data.class_pcd, pcdrv_data.dev_number + i);
        cdev_del(&pcdrv_data.pcdev_data[i].cdev);
    }
    class_destroy(pcdrv_data.class_pcd);

err_unreg:
    unregister_chrdev_region(pcdrv_data.dev_number, NO_OF_DEVICES);
    return ret;
}

static void __exit pcd_driver_exit(void)
{
    int i;

    /*********************************************************************
     * Cleanup order is reverse of init order (important!)
     * 1. Destroy /dev nodes
     * 2. Delete cdevs
     * 3. Destroy class
     * 4. Unregister chrdev region
     *********************************************************************/
    for (i = 0; i < NO_OF_DEVICES; i++) {
        device_destroy(pcdrv_data.class_pcd, pcdrv_data.dev_number + i);
        cdev_del(&pcdrv_data.pcdev_data[i].cdev);
    }

    class_destroy(pcdrv_data.class_pcd);
    unregister_chrdev_region(pcdrv_data.dev_number, NO_OF_DEVICES);

    pr_info("Module unloaded successfully\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUSHANT PATIL)");
MODULE_DESCRIPTION("Multi-device pseudo character driver");
MODULE_VERSION("2.1");
