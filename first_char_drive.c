#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>

#define DEVICE_NAME "chardriver"
#define DEVICE_COUNT 1

struct char_device
{
    struct cdev cdev;
};

static struct char_device device;
static dev_t dev_number;

static int static_major_device_num = 0;
static int static_minor_device_num = 0;
module_param(static_major_device_num, int, S_IRUGO);
module_param(static_minor_device_num, int, S_IRUGO);


int device_open(struct inode * inode, struct file * filp) {
    struct char_device* device;
    device = container_of(inode->i_cdev , struct char_device, cdev);

    filp->private_data = device;

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
};

static int setup_char_device(struct char_device* dev) {
    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;

    int err = cdev_add(&dev->cdev, dev_number, DEVICE_COUNT);
    if (err < 0) {
        printk(KERN_ALERT "cdev add failed");
        return err;
    }
    printk(KERN_INFO "cdev initialized");
    return 0;
}


static int __init initialize(void) {
    int err;
    if (static_major_device_num){
        dev_number = MKDEV(static_major_device_num, static_minor_device_num);
        err = register_chrdev_region(dev_number, DEVICE_COUNT, DEVICE_NAME);
    }
    else {
        err = alloc_chrdev_region(&dev_number, 0, DEVICE_COUNT, DEVICE_NAME);
        
    }

    if (err < 0) {
        printk(KERN_ALERT "Device number acquisition failed (err=%d)\n", err);
        return err;
    }

    static_major_device_num = MAJOR(dev_number);
    static_minor_device_num = MINOR(dev_number);
    printk(KERN_INFO "Device registered: Major=%d, Minor=%d\n", static_major_device_num, static_minor_device_num);

    err = setup_char_device(&device);
    if (err < 0) {
        return err;
    }

    return 0;
}

static void __exit clean(void) {
    cdev_del(&device.cdev);
    unregister_chrdev_region(dev_number, 1);
}

module_init(initialize);
module_exit(clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Char Drive");