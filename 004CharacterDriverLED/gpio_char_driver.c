/*
 * GPIO Character Driver
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>     // Modern GPIO API
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/kernel.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#define DEVICE_NAME     "testchar"
#define GPIO_LABEL      "LED_GPIO21"

static dev_t dev_number;
static struct cdev gpio_cdev;
static struct class *gpio_class;
static struct device *gpio_device;

static struct gpio_desc *led_gpio = NULL;   // GPIO Descriptor

/* ====================== File Operations ====================== */

static int test_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}

static int test_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

static ssize_t test_read(struct file *filp, char __user *buf,
                         size_t len, loff_t *off)
{
    int gpio_state;

    pr_info("Read called\n");

    gpio_state = gpiod_get_value(led_gpio);

    if (copy_to_user(buf, &gpio_state, 1)) {
        pr_err("Failed to copy data to user\n");
        return -EFAULT;
    }

    pr_info("GPIO value = %d\n", gpio_state);
    return 1;
}

static ssize_t test_write(struct file *filp, const char __user *buf,
                          size_t len, loff_t *off)
{
    char rec_buf[2] = {0};

    pr_info("Write called\n");

    if (len > 1)
        len = 1;

    if (copy_from_user(rec_buf, buf, len)) {
        pr_err("Failed to copy from user\n");
        return -EFAULT;
    }

    if (rec_buf[0] == '1') {
        gpiod_set_value(led_gpio, 1);
        pr_info("LED turned ON\n");
    } 
    else if (rec_buf[0] == '0') {
        gpiod_set_value(led_gpio, 0);
        pr_info("LED turned OFF\n");
    } 
    else {
        pr_err("Invalid command! Use 1 or 0 only.\n");
    }

    return len;
}

static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = test_open,
    .release = test_release,
    .read    = test_read,
    .write   = test_write,
};

/* ====================== Module Init ====================== */

static int __init test_init(void)
{
    int ret;

    pr_info("Driver initialization started\n");

    /* 1. Allocate major number */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate major number\n");
        return ret;
    }

    /* 2. Register cdev */
    cdev_init(&gpio_cdev, &fops);
    ret = cdev_add(&gpio_cdev, dev_number, 1);
    if (ret < 0) {
        pr_err("cdev_add failed\n");
        goto unreg_chrdev;
    }

    /* 3. Create class */
    gpio_class = class_create("gpio_class");
    if (IS_ERR(gpio_class)) {
        pr_err("Failed to create class\n");
        ret = PTR_ERR(gpio_class);
        goto cdev_del;
    }

    /* 4. Create device node */
    gpio_device = device_create(gpio_class, NULL, dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(gpio_device)) {
        pr_err("Failed to create device\n");
        ret = PTR_ERR(gpio_device);
        goto class_destroy;
    }

    /* ====================== GPIO Setup (Modern Way) ====================== */
    led_gpio = gpiod_get(NULL, GPIO_LABEL, GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        pr_err("Failed to get GPIO descriptor\n");
        ret = PTR_ERR(led_gpio);
        goto dev_destroy;
    }

    /* Export to sysfs (optional) */
    gpiod_export(led_gpio, false);

    pr_info("Driver loaded successfully!\n");
    pr_info("Device: /dev/%s\n", DEVICE_NAME);
    pr_info("Usage:\n");
    pr_info("  echo 1 > /dev/testchar    # LED ON\n");
    pr_info("  echo 0 > /dev/testchar    # LED OFF\n");
    pr_info("  cat /dev/testchar\n");

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

/* ====================== Module Exit ====================== */

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
MODULE_AUTHOR("SUSHANT PATIL (Updated for Kernel 6.x)");
MODULE_DESCRIPTION("GPIO Character Driver using modern gpiod API");
