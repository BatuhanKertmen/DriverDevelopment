#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#define DEVICE_NAME "chardriver"
#define DEVICE_COUNT 1

static dev_t dev_number;

static int static_major_device_num = 0;
static int static_minor_device_num = 0;
module_param(static_major_device_num, int, S_IRUGO);
module_param(static_minor_device_num, int, S_IRUGO);


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
        printk(KERN_ALERT "device number acquisation failed!");
        return err;
    }



    return 0;
}

static void __exit clean(void) {
    unregister_chrdev_region(dev_number, 1);
}

module_init(initialize);
module_exit(clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Char Drive");