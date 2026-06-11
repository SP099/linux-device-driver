/*
 * GPIO Character Driver with ioctl Support
 *
 * This driver demonstrates how to control a GPIO (LED) using:
 *   - write() method (echo 1 > /dev/testchar)
 *   - ioctl() method (recommended for control commands)
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>    // Modern GPIO API
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/kernel.h>

/* Print format */
#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#define DEVICE_NAME     "testchar"

/* ====================== ioctl Command Definitions ====================== */
/*
 * _IO(type, nr)     : Command without data
 * _IOR(type, nr, size) : Command that reads data from driver
 * _IOW(type, nr, size) : Command that writes data to driver
 */
#define LED_ON      _IO('f', 1)
#define LED_OFF     _IO('f', 2)
#define LED_TOGGLE  _IO('f', 3)
#define LED_STATUS  _IOR('f', 4, int)

static dev_t dev_number;
static struct cdev gpio_cdev;
static struct class *gpio_class;
static struct device *gpio_device;
static struct gpio_desc *led_gpio = NULL;   // GPIO Descriptor

/* ====================== File Operations ====================== */

static int test_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened by user space process\n");
    return 0;
}

static int test_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

/**
 * test_unlocked_ioctl() - Handle custom control commands from user space
 *
 * Purpose of ioctl:
 *   - Send special commands that cannot be expressed through read/write
 *   - Configure device behavior
 *   - Get device status
 */
static long test_unlocked_ioctl(struct file *file, unsigned int cmd,
                                unsigned long arg)
{
    int status;

    switch (cmd) {
    case LED_ON:
        gpiod_set_value(led_gpio, 1);
        pr_info("LED turned ON via ioctl\n");
        break;

    case LED_OFF:
        gpiod_set_value(led_gpio, 0);
        pr_info("LED turned OFF via ioctl\n");
        break;

    case LED_TOGGLE:
        gpiod_set_value(led_gpio, !gpiod_get_value(led_gpio));
        pr_info("LED toggled via ioctl\n");
        break;

    case LED_STATUS:
        status = gpiod_get_value(led_gpio);
        if (copy_to_user((int __user *)arg, &status, sizeof(status))) {
            pr_err("Failed to copy status to user\n");
            return -EFAULT;
        }
        pr_info("LED status sent to user: %d\n", status);
        break;

    default:
        pr_err("Invalid ioctl command: 0x%x\n", cmd);
        return -ENOTTY;   // Not a typewriter (standard error for bad ioctl)
    }
    return 0;
}

static const struct file_operations fops = {
    .owner           = THIS_MODULE,
    .open            = test_open,
    .release         = test_release,
    .unlocked_ioctl  = test_unlocked_ioctl,
};

/* ====================== Module Init / Exit ====================== */

static int __init test_init(void)
{
    int ret;

    pr_info("Driver initialization started...\n");

    /* Step 1: Dynamic major number allocation */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate major number\n");
        return ret;
    }

    /* Step 2: Register character device */
    cdev_init(&gpio_cdev, &fops);
    ret = cdev_add(&gpio_cdev, dev_number, 1);
    if (ret < 0) goto unreg_chrdev;

    /* Step 3: Create device class */
    gpio_class = class_create("gpio_class");
    if (IS_ERR(gpio_class)) goto cdev_del;

    /* Step 4: Create device node /dev/testchar */
    gpio_device = device_create(gpio_class, NULL, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(gpio_device)) goto class_destroy;

    /* Step 5: Get GPIO using modern descriptor API */
    led_gpio = gpiod_get(NULL, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        pr_err("Failed to get GPIO descriptor. Make sure Device Tree has label 'led'\n");
        ret = PTR_ERR(led_gpio);
        goto dev_destroy;
    }

    gpiod_export(led_gpio, false);

    pr_info("Driver loaded successfully!\n");
    pr_info("Device node: /dev/%s\n", DEVICE_NAME);
    pr_info("ioctl commands available: LED_ON, LED_OFF, LED_TOGGLE, LED_STATUS\n");

    return 0;

dev_destroy:
    device_destroy(gpio_class, dev_number);
class_destroy:
    class_destroy(gpio_class);
cdev_del:
    cdev_del(&gpio_cdev);
unreg_chrdev:
    unregister_chrdev_region(dev_number, 1);
    return ret;
}

static void __exit test_cleanup(void)
{
    if (led_gpio) {
        gpiod_unexport(led_gpio);
        gpiod_put(led_gpio);
    }

    device_destroy(gpio_class, dev_number);
    class_destroy(gpio_class);
    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev_number, 1);

    pr_info("Driver removed successfully\n");
}

module_init(test_init);
module_exit(test_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUSHANT PATIL");
MODULE_DESCRIPTION("GPIO Character Driver with ioctl support");
