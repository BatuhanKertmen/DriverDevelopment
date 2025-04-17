#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#define DEVICE_NAME "chardriver"
#define DEVICE_COUNT 3

static int quantum_size = 4000;
static int quantum_per_set = 1000;
module_param(quantum_size, int, S_IRUGO);
module_param(quantum_per_set, int, S_IRUGO);

typedef char* quantum;

struct qset {
    struct qset * next;
    quantum * data;
};

struct char_device
{
    struct cdev cdev;
    struct qset * qset;
    size_t size;
};

static struct char_device devices[DEVICE_COUNT];
static dev_t dev_number;

static int major_device_num = 0;
static int minor_device_num = 0;
module_param(major_device_num, int, S_IRUGO);
module_param(minor_device_num, int, S_IRUGO);

int init_qset(struct qset * qset, int quantum_per_set, int quantum_size){
    if (qset == NULL) {
        return -EINVAL;
    }

    if (qset->data != NULL) {
        return 0;
    }

    qset->data = kmalloc_array(quantum_per_set, sizeof(quantum), GFP_KERNEL);
    if (qset->data == NULL) {
        return -ENOMEM;
    }

    
    for (int idx = 0; idx < quantum_per_set; ++idx) {
        qset->data[idx] = kmalloc(sizeof(char) * quantum_size, GFP_KERNEL);
        if (qset->data[idx] == NULL) {
            while (idx--)
            {
                kfree(qset->data[idx]);
            }
            
            kfree(qset->data);
            qset->data = NULL;  
            return -ENOMEM;
        }
    }

    return 0;
}

int device_open(struct inode * inode, struct file * filp) {
    struct char_device* device;
    device = container_of(inode->i_cdev , struct char_device, cdev);

    filp->private_data = device;

    struct qset *qs = kmalloc(sizeof(*qs), GFP_KERNEL);
    if (!qs) {
        return -ENOMEM;
    }
    memset(qs, 0, sizeof(*qs));

    if (init_qset(qs, quantum_per_set, quantum_size) < 0) {
        printk(KERN_ERR "Failed to init qset\n");
        kfree(qs);
        return -ENOMEM;
    }

    printk(KERN_INFO "Device with minor %d opened\n", iminor(inode));
    device->qset = qs;
    return 0;
}

int device_release(struct inode * inode, struct file * filp) {
    struct char_device * device = filp->private_data;

    if (device == NULL || device->qset == NULL ) {
        return 0;
    }

    struct qset * head = device->qset;
    struct qset *next;
    while(head) {
        if (head->data) {
            for (int i = 0; i < quantum_per_set; ++i) {
                kfree(head->data[i]);
            }
            kfree(head->data);
        }

        next = head->next;
        kfree(head);
        head = next;
    }

    device->qset = NULL;

    printk(KERN_INFO "Device with minor %d released and memory freed\n", iminor(inode));
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
};

static int setup_char_device(struct char_device* dev, int index) {
    dev->qset = NULL;
    dev->size = 0;

    int err, devno = MKDEV(major_device_num, minor_device_num + index);
    
    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;

    err = cdev_add(&dev->cdev, devno, 1);
    if (err < 0) {
        printk(KERN_ERR "cdev add failed (err=%d)\n", err);
        return err;
    }

    printk(KERN_INFO "cdev initialized");
    return 0;
}


static int __init initialize(void) {
    int err;
    if (major_device_num){
        dev_number = MKDEV(major_device_num, minor_device_num);
        err = register_chrdev_region(dev_number, DEVICE_COUNT, DEVICE_NAME);
    }
    else {
        err = alloc_chrdev_region(&dev_number, 0, DEVICE_COUNT, DEVICE_NAME);
        
    }

    if (err < 0) {
        printk(KERN_ALERT "Device number acquisition failed (err=%d)\n", err);
        return err;
    }

    major_device_num = MAJOR(dev_number);
    minor_device_num = MINOR(dev_number);
    printk(KERN_INFO "Device registered: Major=%d, Minor=%d\n", major_device_num, minor_device_num);


    for (int idx = 0; idx < DEVICE_COUNT; ++idx) {
        err = setup_char_device(&devices[idx], idx);
        if (err < 0) {
            return err;
        }    
    }

    return 0;
}

static void __exit clean(void) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
        cdev_del(&devices[i].cdev);
    }

    unregister_chrdev_region(dev_number, DEVICE_COUNT);
}

module_init(initialize);
module_exit(clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Char Drive");