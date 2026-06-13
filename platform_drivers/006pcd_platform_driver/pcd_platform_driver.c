/* ============================================================
 * pcd_platform_driver.c
 *
 * PURPOSE:
 *   This is the DRIVER side. It knows HOW to operate a
 *   "pseudo-char-device". It does NOT know how many devices
 *   exist — it discovers them dynamically via probe().
 *
 *   Every time pcd_device_setup registers a platform_device
 *   whose .name == "pseudo-char-device", the kernel calls
 *   pcd_platform_driver_probe() with that device's details.
 *
 * KEY DESIGN CHOICE — devm_* managed resources:
 *   Any allocation prefixed with devm_ is automatically freed
 *   by the kernel when the device is unbound from the driver.
 *   This means remove() can be very short — just stop hardware.
 *   No manual kfree() / cdev_del() needed for devm_ allocations.
 * ============================================================ */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/slab.h>
#include "platform.h"

/* Redefine pr_fmt: every pr_info/pr_err prepends the function name.
 * Output example:  "pcd_open :minor access = 2"                     */
#undef pr_fmt
#define pr_fmt(fmt) "%s :" fmt,__func__

/* Maximum number of devices this driver will ever manage.
 * We reserve this many minor numbers upfront in alloc_chrdev_region. */
#define MAX_DEVICES 10

/* ============================================================
 * Data Structures
 * ============================================================ */

/* Per-device private data.
 * One instance is allocated (via devm_kzalloc) for EACH
 * platform device that gets probed.
 *
 * Stored in pdev->dev.driver_data via dev_set_drvdata() so
 * every file operation can retrieve it with:
 *   pcdev_data = (struct pcdev_private_data *) filp->private_data;
 */
struct pcdev_private_data
{
    struct pcdev_platform_data pdata;    /* copy of size/perm/serial from pdev  */
    char *buff;                          /* kernel buffer — actual device memory */
    dev_t dev_num;                       /* this device's major:minor number     */             
    struct cdev cdev;                    /* character device kernel object       */
};

/* Driver-wide (global) data — ONE instance for the whole driver.
 * Holds things that are shared across ALL device instances.         */
struct pcdrv_private_data
{
    int total_devices;                  /* count of successfully probed devices   */
    dev_t device_num_base;              /* first major:minor in our reserved range*/
    struct class *class_pcd;            /* /sys/class/pcd_class — parent for udev */
    struct device *device_pcd;          /* last created /dev/pcdev-N node */
};

/* Global instance — lives for the lifetime of the driver module.    */
struct pcdrv_private_data pcdrv_data;

/* ============================================================
 * Permission Helper
 *
 * Called from pcd_open() to decide whether the access mode
 * requested by the user matches what the device allows.
 *
 * dev_perm : RDWR / RDONLY / WRONLY (from platform data)
 * acc_mode : filp->f_mode bits set by VFS based on O_RDONLY etc.
 *
 * Returns 0 if access is allowed, -EPERM if denied.
 * ============================================================ */
static int check_permission(int dev_perm, int acc_mode)
{
    if(dev_perm == RDWR)
        return 0;
    if((dev_perm == RDONLY) && ((acc_mode & FMODE_READ) && !(acc_mode & FMODE_WRITE)))
        return 0;
    if((dev_perm == WRONLY) && ( (acc_mode & FMODE_WRITE) && !(acc_mode & FMODE_READ) ) )
        return 0;
    return -EPERM;
}

/* ============================================================
 * File Operations
 *
 * These functions form the "vtable" of the character device.
 * The VFS calls them when userspace calls open/read/write/etc.
 * ============================================================ */

/* ----------------------------------------------------------
 * pcd_lseek — handle lseek() / fseek() from userspace
 *
 * filp   : the open file (holds f_pos = current position)
 * offset : how far to move
 * whence : SEEK_SET / SEEK_CUR / SEEK_END
 *
 * Returns new file position, or -EINVAL for bad arguments.
 * ---------------------------------------------------------- */

static loff_t pcd_lseek(struct file *filp, loff_t offset, int whence)
{
    /* Retrieve per-device data stored by pcd_open() */
    struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;

    /* max_size: the buffer capacity for THIS device instance */
    int max_size = pcdev_data->pdata.size;
    loff_t temp;

    pr_info("lseek requested\n");
    pr_info("current value of the file position = %lld\n", filp->f_pos);

    switch(whence)
    {
        case SEEK_SET:
            /* Move to absolute position from start of buffer.
             * Reject if offset is out of [0, max_size] range.    */
            if((offset > max_size) || (offset < 0))
                return -EINVAL;
            filp->f_pos = offset;
            break;
        case SEEK_CUR:
            /* Move relative to current position.
             * Calculate new position in temp first to detect
             * overflow before modifying the actual f_pos.        */
            temp = filp->f_pos + offset;
            if(temp > max_size || (temp < 0))
                return -EINVAL;
            filp->f_pos = temp;
            break;
        case SEEK_END:
            /* Move relative to end of buffer.
             * SEEK_END + 0 → position at end (ready for append).
             * SEEK_END - n → n bytes before end.                 */
            temp = max_size + offset;
            if(temp > max_size || (temp < 0))
                return -EINVAL;
            filp->f_pos = temp;
            break;
        default:
            return -EINVAL;
    }

    pr_info("new value of the file position = %lld\n", filp->f_pos);
    return filp->f_pos;  /* lseek must return new position */
}

/* ----------------------------------------------------------
 * pcd_read — copy data from kernel buffer → userspace
 *
 * filp   : open file (has f_pos and private_data)
 * buff   : userspace destination buffer pointer
 * count  : how many bytes userspace wants
 * f_pos  : pointer to current file position (kernel manages)
 *
 * Returns bytes copied, 0 on EOF, negative errno on error.
 * ---------------------------------------------------------- */
static ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
    struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;
    int max_size = pcdev_data->pdata.size;

    pr_info("read requested for %zu bytes\n", count);
    pr_info("current file positon = %lld\n", *f_pos);

    /* Clamp count so we never read past end of buffer.
     * If f_pos is already at max_size, count becomes 0
     * and we return 0 below → signals EOF to userspace. */
    if((*f_pos + count) > max_size)
        count = max_size - *f_pos;

    /* copy_to_user(dest_user, src_kernel, n):
     *   Safely copies n bytes from kernel buffer to userspace.
     *   Handles page faults and SMAP (Supervisor Mode Access Prevention).
     *   Returns number of bytes NOT copied (0 = full success).
     *
     *   IMPORTANT: pcdev_data->buff is the kernel buffer start.
     *   We offset by *f_pos to read from the correct position.
     *   The '&' bug (using &pcdev_data->buff) was removed here —
     *   that would pass the address of the pointer, not the data. */
    if(copy_to_user(buff, pcdev_data->buff + (*f_pos), count))
    {
        return -EFAULT;
    }

    /* Advance the file position by exactly how many bytes we read */
    *f_pos += count;

    pr_info("Number of bytes successfully read = %zu\n", count);
    pr_info("Updated file position = %lld\n", *f_pos);

    /* Return bytes read. Caller calls read() again until we return 0. */
    return count;
}

/* ----------------------------------------------------------
 * pcd_write — copy data from userspace → kernel buffer
 *
 * filp   : open file
 * buff   : userspace source buffer pointer (__user annotation
 *          tells sparse checker this is a userspace pointer)
 * count  : how many bytes userspace wants to write
 * f_pos  : pointer to file position
 *
 * Returns bytes written, negative errno on error.
 * ---------------------------------------------------------- */
static ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
    struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;
    int max_size = pcdev_data->pdata.size;

    pr_info("write requested for %zu bytes\n", count);
    pr_info("current file positon = %lld\n", *f_pos);

    /* Clamp: don't write more than remaining space in buffer */
    if((*f_pos + count) > max_size)
        count = max_size - *f_pos;

    /* If clamping reduced count to 0, buffer is completely full.
     * Return -ENOMEM (no space left) instead of writing 0 bytes,
     * which would look like a successful empty write.            */
    if(!count)
        return -ENOMEM;

    /* copy_from_user(dest_kernel, src_user, n):
     *   Safely copies n bytes from userspace to kernel buffer.
     *   Returns number of bytes NOT copied (0 = full success).  */
    if(copy_from_user(pcdev_data->buff + (*f_pos), buff, count))
    {
        return -EFAULT;
    }

    /* Advance position by bytes actually written */
    *f_pos += count;

    pr_info("Number of bytes successfully written = %zu\n", count);
    pr_info("Updated file position = %lld\n", *f_pos);

    return count;
}

/* ----------------------------------------------------------
 * pcd_open — called when userspace calls open("/dev/pcdev-N")
 *
 * inode  : kernel object representing the file on disk/devfs.
 *          inode->i_rdev holds the major:minor of the device.
 *          inode->i_cdev points to the cdev we registered.
 * filp   : the open file descriptor created for this open().
 *          filp->private_data is ours to use — we store
 *          the per-device pointer here for all future ops.
 *
 * Returns 0 on success, -EPERM if permissions don't match.
 * ---------------------------------------------------------- */
static int pcd_open(struct inode *inode, struct file *filp)
{
    int ret;
    int minor_n;
    struct pcdev_private_data *pcdev_data;

    /* MINOR(inode->i_rdev):
     *   Extracts the minor number from the device number.
     *   Minor number = index of this device (0..3).
     *   Used here only for logging.                          */
    minor_n = MINOR(inode->i_rdev);
    pr_info("minor access = %d\n", minor_n);

    /* container_of(ptr, type, member):
     *   Given a pointer to a MEMBER inside a struct, get the
     *   pointer to the CONTAINING struct.
     *
     *   inode->i_cdev points to pcdev_private_data.cdev.
     *   container_of gives us back the full pcdev_private_data.
     *
     *   This is how the VFS, which only knows about cdev,
     *   gives us access to our per-device state.            */
    pcdev_data = container_of(inode->i_cdev, struct pcdev_private_data, cdev);

    /* Store in filp->private_data so read/write/lseek/release
     * can access per-device state without calling container_of
     * repeatedly.                                            */
    filp->private_data = pcdev_data;

    /* Check if the access mode requested (O_RDONLY, O_WRONLY,
     * O_RDWR) is compatible with this device's permission.
     * filp->f_mode contains FMODE_READ and/or FMODE_WRITE.  */
    ret = check_permission(pcdev_data->pdata.perm, filp->f_mode);
    (!ret)?pr_info("open was successful\n"):pr_info("open was unsuccessful\n");

    return ret;   /* 0 → VFS proceeds; -EPERM → open() fails in userspace */
}

/* ----------------------------------------------------------
 * pcd_release — called when userspace calls close(fd)
 *               or when the last reference to the file is dropped.
 *
 * No cleanup needed here because we didn't allocate anything
 * in open(). The per-device buffer lives until remove().
 * ---------------------------------------------------------- */
static int pcd_release (struct inode *inode, struct file *filp)
{
    pr_info("release was successful\n");
    return 0;
}

/* ============================================================
 * File Operations Table
 *
 * This struct is the "vtable" — it maps system calls to our
 * handler functions. Registered with cdev_init() in probe().
 * NULL entries use safe kernel defaults (e.g. no llseek →
 * kernel uses generic_file_llseek or no-op).
 * ============================================================ */
static struct file_operations pcd_fops = {
    .open    = pcd_open,
    .write   = pcd_write,
    .llseek  = pcd_lseek,
    .read    = pcd_read,
    .release = pcd_release,
    .owner   = THIS_MODULE  /* prevents unload while device is open */
};

/* ============================================================
 * Platform Driver remove() — VERSION COMPATIBILITY
 *
 * Linux 6.11+ changed the remove() signature from int → void.
 * We use a compile-time check to provide the correct signature.
 *
 * remove() is called when:
 *   a) pcd_device_setup is rmmod'd (device unregistered), OR
 *   b) pcd_platform_driver is rmmod'd (driver unregistered)
 * ============================================================ */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,11,0)
static void pcd_platform_driver_remove(struct platform_device *pdev)
{
    struct pcdev_private_data *dev_data = dev_get_drvdata(&pdev->dev);

    /* FIX: Added sanity check wrapper to prevent null pointer dereferences on probe failures */
    if (dev_data) {
        device_destroy(pcdrv_data.class_pcd, dev_data->dev_num);
        cdev_del(&dev_data->cdev);
    }
    
    pcdrv_data.total_devices--;
    pr_info("platform device removed\n");
}
#else
static int pcd_platform_driver_remove(struct platform_device *pdev)
{
    /* dev_get_drvdata(&pdev->dev):
     *   Retrieves the pointer we stored with dev_set_drvdata()
     *   in probe(). This gives us back our pcdev_private_data. */
    struct pcdev_private_data *dev_data = dev_get_drvdata(&pdev->dev);

    /* Sanity check: if probe() failed early (before set_drvdata),
     * dev_data could be NULL. Guard to avoid null-pointer oops. */
    if (dev_data) {
        /* Remove the /dev/pcdev-N node created in probe().
         * After this, userspace can no longer open the device.  */
        device_destroy(pcdrv_data.class_pcd, dev_data->dev_num);

        /* Unlink the cdev from the kernel character device table.
         * After this, the major:minor is no longer dispatched to
         * our file_operations.                                   */
        cdev_del(&dev_data->cdev);

        /* NOTE: We do NOT kfree(dev_data) or kfree(dev_data->buff).
         * Both were allocated with devm_kzalloc in probe(). The
         * kernel automatically frees all devm_ allocations when
         * the device is unbound — right after remove() returns.  */
    }
    
    /* Decrement our live device counter */
    pcdrv_data.total_devices--;
    pr_info("platform device removed\n");
    return 0;
}
#endif

/* ============================================================
 * Platform Driver probe()
 *
 * probe() is called ONCE PER DEVICE by the platform bus when:
 *   a) A new platform_device is registered (if driver already loaded), OR
 *   b) The platform_driver is registered (for each already-registered device)
 *
 * pdev: pointer to the platform device just matched to this driver.
 *       pdev->id  = device instance number (0..3)
 *       pdev->dev = embedded struct device (use for devm_*, dev_info)
 *
 * Returns 0 on success. Negative errno aborts binding — the device
 * will NOT be associated with this driver, and remove() will NOT be called.
 * ============================================================ */
static int pcd_platform_driver_probe(struct platform_device *pdev)
{
    int ret;
    struct pcdev_private_data *dev_data;
    struct pcdev_platform_data *pdata;

    /* ---- Step 1: Read platform data ---- */
    /* dev_get_platdata(&pdev->dev):
     *   Retrieves pdev->dev.platform_data — the pcdev_platform_data
     *   pointer we set in pcd_device_setup.c.
     *   In DT-based drivers this is replaced by of_property_read_*().
     *   If no platform data was set, this returns NULL.              */
    pdata = (struct pcdev_platform_data*) dev_get_platdata(&pdev->dev);
    if(!pdata){
        pr_info("No platform data available\n");
        return -EINVAL;
    }

    /* ---- Step 2: Allocate per-device private data ---- */
    /* devm_kzalloc(&pdev->dev, size, flags):
     *   Managed version of kzalloc.
     *   - Allocates size bytes, zeroed.
     *   - Tied to the device lifecycle: automatically freed
     *     when pdev->dev is unbound (after remove() returns).
     *   - GFP_KERNEL: normal allocation, may sleep. OK in probe().
     *   - No kfree() needed anywhere in this driver.               */
    dev_data = devm_kzalloc(&pdev->dev, sizeof(*dev_data), GFP_KERNEL);
    if(!dev_data){
        pr_info("Cannot allocate memory\n");
        return -ENOMEM;
    }

    /* dev_set_drvdata(&pdev->dev, dev_data):
     *   Stores our per-device pointer inside the device object.
     *   Retrieved later in remove() and file ops via
     *   dev_get_drvdata() or filp->private_data.                   */
    dev_set_drvdata(&pdev->dev, dev_data);

    /* Copy platform data fields into our private struct.
     * We keep a local copy so we don't depend on the platform_device
     * staying around (good defensive practice).                     */
    dev_data->pdata.size = pdata->size;
    dev_data->pdata.perm = pdata->perm;
    dev_data->pdata.serial_number = pdata->serial_number;

    pr_info("Device serial number = %s\n", dev_data->pdata.serial_number);
    pr_info("Device size = %d\n", dev_data->pdata.size);
    pr_info("Device permission = %d\n", dev_data->pdata.perm);

    /* ---- Step 3: Allocate the device I/O buffer ---- */
    /* This is the actual memory that stores data written to the device.
     * Size comes from platform data (512 or 1024 bytes per device).
     * Also managed by devm_ — freed automatically on unbind.         */
    dev_data->buff = devm_kzalloc(&pdev->dev, dev_data->pdata.size, GFP_KERNEL);
    if(!dev_data->buff){
        pr_info("Cannot allocate memory\n");
        return -ENOMEM; 
        /* dev_data itself is also freed automatically here —
         * devm_ cleans up ALL prior devm_ allocations for this device */
    }

    /* ---- Step 4: Assign a major:minor number ---- */
    /* pcdrv_data.device_num_base: the first dev_t in our reserved range.
     * Adding pdev->id (0..3) gives each device its own unique minor.
     *
     * Example: if base = MKDEV(239, 0) and pdev->id = 2
     *          then dev_data->dev_num = MKDEV(239, 2)              */
    dev_data->dev_num = pcdrv_data.device_num_base + pdev->id;
    
    /* ---- Step 5: Register the character device ---- */
    /* cdev_init(&cdev, &fops):
     *   Initialises the cdev struct and links it to our file_operations.
     *   The cdev is the kernel's internal representation of a char device. */
    cdev_init(&dev_data->cdev, &pcd_fops);

    /* THIS_MODULE: ensures the module can't be unloaded while the
     * device is open (reference counting).                          */
    dev_data->cdev.owner = THIS_MODULE;
    
    /* cdev_add(&cdev, dev_num, count):
     *   Adds the cdev to the kernel's character device table.
     *   After this call, the kernel will route VFS calls for
     *   major:minor dev_data->dev_num to our pcd_fops.
     *   count=1 means we're registering exactly one minor number.  */
    ret = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
    if(ret < 0){
        pr_err("Cdev add failed\n");
        return ret; 
        /* auto-frees dev_data and dev_data->buff */
    }

    /* ---- Step 6: Create the /dev/pcdev-N device node ---- */
    /* device_create(class, parent, devt, drvdata, fmt, ...):
     *   Creates a struct device in sysfs under class_pcd.
     *   udev (userspace daemon) sees the sysfs event and
     *   automatically creates /dev/pcdev-N with correct permissions.
     *
     *   pcdrv_data.class_pcd  = /sys/class/pcd_class  (created in init)
     *   NULL                  = no parent device
     *   dev_data->dev_num     = major:minor for this node
     *   NULL                  = no driver data attached to this device node
     *   "pcdev-%d", pdev->id  = name → /dev/pcdev-0, /dev/pcdev-1 ...    */
    pcdrv_data.device_pcd = device_create(pcdrv_data.class_pcd, NULL, dev_data->dev_num, NULL, "pcdev-%d", pdev->id);
    if(IS_ERR(pcdrv_data.device_pcd)){
        pr_err("Device create failed\n");
        ret = PTR_ERR(pcdrv_data.device_pcd);  /* extract errno from error pointer */
        cdev_del(&dev_data->cdev);             /* undo cdev_add */
        return ret;
        /* devm_ will clean up dev_data and dev_data->buff */
    }

    /* Count successfully initialised devices */
    pcdrv_data.total_devices++;
    pr_info("Platform device probed\n");
    return 0;
}

/* ============================================================
 * Platform Driver Registration Table
 *
 * .driver.name MUST exactly match the .name field in every
 * platform_device this driver should bind to.
 * The platform bus does the strcmp() at registration time.
 * ============================================================ */
struct platform_driver pcd_platform_driver = {
    .probe = pcd_platform_driver_probe,
    .remove = pcd_platform_driver_remove,
    .driver = {
        .name = "pseudo-char-device"  /* matches platform_device.name */
    }
};

/* ============================================================
 * Module Init — Driver-level Setup
 *
 * This runs ONCE when insmod pcd_platform_driver.ko is called.
 * Sets up global resources shared by all device instances.
 * ============================================================ */
static int __init pcd_platform_driver_init(void)
{
    int ret;

    /* ---- Step A: Reserve a range of character device numbers ----
     *
     * alloc_chrdev_region(dev_t *out, baseminor, count, name):
     *   Asks the kernel to dynamically pick an unused major number
     *   and reserve 'count' consecutive minor numbers starting at
     *   baseminor.
     *
     *   out       → pcdrv_data.device_num_base will be filled with
     *               the allocated MKDEV(major, baseminor).
     *   baseminor → 0  (start minors from 0)
     *   count     → MAX_DEVICES (10 slots, even if only 4 used now)
     *   name      → "pcdevs" (appears in /proc/devices)
     *
     *   After this call:
     *     MAJOR(pcdrv_data.device_num_base) = kernel-chosen major (e.g. 239)
     *     MINOR(pcdrv_data.device_num_base) = 0
     *
     *   In probe(), each device gets:
     *     dev_num = device_num_base + pdev->id
     *             = MKDEV(239, 0), MKDEV(239, 1), MKDEV(239, 2), MKDEV(239, 3)
     *
     *   WHY DYNAMIC? Using alloc_chrdev_region instead of
     *   register_chrdev_region avoids hard-coding a major number that
     *   might already be in use on another system.                     */
    ret = alloc_chrdev_region(&pcdrv_data.device_num_base, 
                               0,               /* baseminor */
                               MAX_DEVICES,     /* count */
                               "pcdevs");       /* name in /proc/devices */
    if(ret < 0){
        pr_err("Alloc chrdev failed\n");
        return ret;
    }

    /* ---- Step B: Create the device class ----
     *
     * class_create(owner, name):
     *   Creates /sys/class/pcd_class directory.
     *   This is the parent "class" under which all our
     *   /dev/pcdev-N nodes will appear in sysfs.
     *   udev uses the class to apply permission rules.
     *
     *   Returns an error pointer on failure — use IS_ERR() to check,
     *   PTR_ERR() to extract the errno.                              */
    pcdrv_data.class_pcd = class_create("pcd_class");
    if(IS_ERR(pcdrv_data.class_pcd)){
        pr_err("Class creation failed\n");
        ret = PTR_ERR(pcdrv_data.class_pcd);

        /* Undo Step A before returning */
        unregister_chrdev_region(pcdrv_data.device_num_base, MAX_DEVICES);
        return ret;
    }

    /* ---- Step C: Register the platform driver ----
     *
     * platform_driver_register(&driver):
     *   Registers this driver on the platform bus.
     *   The bus immediately scans all already-registered
     *   platform_devices looking for name matches.
     *   For each match found, probe() is called right here.
     *
     *   If pcd_device_setup is loaded AFTER this, probe() fires
     *   at device registration time instead.                       */
    ret = platform_driver_register(&pcd_platform_driver);
    if(ret < 0){
        class_destroy(pcdrv_data.class_pcd);
        unregister_chrdev_region(pcdrv_data.device_num_base, MAX_DEVICES);
        return ret;
    }
    
    pr_info("pcd platform driver loaded\n");
    return 0;
}

/* ============================================================
 * Module Exit — Driver-level Teardown
 *
 * Runs when rmmod pcd_platform_driver is called.
 * Undoes Steps A, B, C in reverse order.
 *
 * NOTE: platform_driver_unregister() triggers remove() for
 * every currently bound device BEFORE returning.
 * So all /dev/pcdev-N nodes and cdevs are cleaned up first,
 * then we destroy the class and release the chrdev range.
 * ============================================================ */
static void __exit pcd_platform_driver_cleanup(void)
{
    /* Step C undo: unregister driver, triggers remove() for all devices */
    platform_driver_unregister(&pcd_platform_driver);

    /* Step B undo: remove /sys/class/pcd_class */
    class_destroy(pcdrv_data.class_pcd);

    /* Step A undo: release the reserved major:minor range */
    unregister_chrdev_region(pcdrv_data.device_num_base, MAX_DEVICES);
    pr_info("module unloaded\n");
}

module_init(pcd_platform_driver_init);
module_exit(pcd_platform_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUSHANT PATIL");
MODULE_DESCRIPTION("A pseudo character driver handles n devices");
