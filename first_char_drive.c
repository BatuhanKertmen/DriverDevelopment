#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#define DEVICE_NAME "chardriver"

static dev_t dev_number;

static int __init initialize(void) {
    int err = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (err) {
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