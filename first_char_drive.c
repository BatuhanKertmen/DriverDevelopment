#include "first_char_drive.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h> 
#include <linux/uaccess.h>

static struct class *driver_class;

static unsigned int quantum_size = 4000;
static unsigned int quantum_per_set = 1000;
module_param(quantum_size, uint, 0444);
module_param(quantum_per_set, uint, 0444);


static int device_open(struct inode * inode, struct file * filp);
static int device_release(struct inode *inode, struct file *filp);
static ssize_t device_read(struct file * filp, char __user * buffer, size_t count, loff_t * offset);
static ssize_t device_write(struct file * filp, const char __user * buffer, size_t count, loff_t * offset);
static int free_all_memory(struct char_device * device);
static struct qset* init_qset(size_t quantum_per_set, size_t quantum_size);

static struct char_device devices[DEVICE_COUNT];

static dev_t dev_number;

static int major_device_num = 0;
static int minor_device_num = 0;
module_param(major_device_num, int, 0444);
module_param(minor_device_num, int, 0444);

static struct qset* init_qset(size_t quantum_per_set, size_t quantum_size){
    struct qset *qset = kmalloc(sizeof(struct qset), GFP_KERNEL);
    if (!qset) {
        return NULL;
    }
    memset(qset, 0, sizeof(struct qset));

    qset->data = kmalloc_array(quantum_per_set, sizeof(quantum), GFP_KERNEL);
    if (qset->data == NULL) {
        kfree(qset);
        return NULL;
    }

    
    for (size_t idx = 0; idx < quantum_per_set; ++idx) {
        qset->data[idx] = kzalloc(quantum_size, GFP_KERNEL);
        if (qset->data[idx] == NULL) {
            while (idx--)
            {
                kfree(qset->data[idx]);
            }
            
            kfree(qset->data);
            kfree(qset);
            qset->data = NULL;  
            return NULL;
        }
    }

    CDEBUG("Allocated qset with %u quantums of size %u\n", quantum_per_set, quantum_size);

    return qset;
}

static int device_open(struct inode * inode, struct file * filp) {
    struct char_device* device;
    device = container_of(inode->i_cdev , struct char_device, cdev);

    filp->private_data = device;

    CDEBUG("Device with minor %d opened\n", iminor(inode));
    return 0;
}

static int device_release(struct inode *inode, struct file *filp) {
    CDEBUG("Device with minor %d released\n", iminor(inode));
    return 0;
}

static ssize_t device_read(struct file * filp, char __user * buffer, size_t count, loff_t * offset) {
    struct char_device * device = filp->private_data;
    
    mutex_lock(&device->device_mutex);
    ssize_t result;

    if (count == 0) {
        result = 0;
        goto release_lock;
    }

    loff_t pos = *offset;
    loff_t qset_bytes = (loff_t)quantum_per_set * (loff_t)quantum_size;

    if (pos >= device->size) {
        CDEBUG("eof reached\n");
        result = 0;
        goto release_lock;
    }
    size_t remaining = device->size - pos;
    if (count > remaining) {
        count = remaining;
    }

    CDEBUG("starting\n");

    if (device->qset == NULL) {
        CDEBUG("qset head pointer is null!\n");
        result = 0;
        goto release_lock;
    }

    size_t qset_idx = pos / qset_bytes;
    struct qset * cur = device->qset;
    for (size_t current_qset_idx = 0; current_qset_idx < qset_idx; ++current_qset_idx) {
        if(cur == NULL) {
            CDEBUG("null qset when reading idx: %zu\n", current_qset_idx);
            result = 0;
            goto release_lock;
        }
        
        cur = cur->next;
    }

    CDEBUG("current qset index %zu\n", qset_idx);

    size_t quantum_idx = (pos % qset_bytes) / quantum_size;
    size_t quantum_offset = (pos % qset_bytes) % quantum_size;

    if (quantum_idx >= quantum_per_set) {
        printk(KERN_ERR "quantum_idx %zu exceeds limit (%u)\n", quantum_idx, quantum_per_set);
        result = -EFAULT;
        goto release_lock;
    }
    
    size_t available = quantum_size - quantum_offset;

    CDEBUG("trying to read %zu th quantum with %zu offset\n", quantum_idx, quantum_offset);

    if (!cur->data || !cur->data[quantum_idx]) {
        result = 0;
        goto release_lock;
    }
    
    size_t to_copy = count < available ? count : available;
    
    CDEBUG("copying %zu bytes\n", to_copy);
    

    unsigned long not_copied = copy_to_user(buffer, cur->data[quantum_idx] + quantum_offset, to_copy);
    size_t copied = to_copy - not_copied;
    *offset += copied;
    
    if (copied == 0 && not_copied) {
        result = -EFAULT;
        goto release_lock;
    }

    CDEBUG("success\n");

    result = copied;
    goto release_lock;

release_lock:
    mutex_unlock(&device->device_mutex);
    return result;
}

static ssize_t device_write(struct file * filp, const char __user * buffer, size_t count, loff_t * offset) {
    struct char_device * device = filp->private_data;

    mutex_lock(&device->device_mutex);
    ssize_t result;

    loff_t pos = *offset;
    loff_t qset_bytes = (loff_t)quantum_per_set * (loff_t)quantum_size;

    size_t qset_idx = pos / qset_bytes;
    struct qset * cur = device->qset;

    CDEBUG("current qset index is %zu\n", qset_idx);

    if (!device->qset) {
        device->qset = init_qset(quantum_per_set, quantum_size);
        if (device->qset == NULL) { 
            result = -ENOMEM;
            goto release_lock;
        }

        cur = device->qset;
    }

    
    for (size_t current_qset_idx = 0; current_qset_idx < qset_idx; ++current_qset_idx) {
        if (cur->next == NULL) {
            cur->next = init_qset(quantum_per_set, quantum_size);
            if (cur->next == NULL) {
                result = -ENOMEM;
                goto release_lock;
            }
        }

        cur = cur->next;
    }

    CDEBUG("accessed correct qset\n");

    size_t quantum_idx = (pos % qset_bytes) / quantum_size;
    size_t quantum_offset = (pos % qset_bytes) % quantum_size;

    if (quantum_idx >= quantum_per_set) {
        printk(KERN_ERR "quantum_idx %zu exceeds limit (%u)\n", quantum_idx, quantum_per_set);
        result = -EFAULT;
        goto release_lock;
    }

    size_t available = quantum_size - quantum_offset;
    size_t to_copy = count < available ? count : available;

    CDEBUG("trying to access %zu indexed quantum %zu offset\n", quantum_idx, quantum_offset);

    if (copy_from_user(cur->data[quantum_idx] + quantum_offset, buffer, to_copy)) {
        result = -EFAULT;
        goto release_lock;
    }

    CDEBUG("wrote %zu bytes\n", to_copy);
   
    if (pos + to_copy > device->size) {
        device->size = pos + to_copy;
    }
    *offset += to_copy;

    CDEBUG("offset is updated to %lld\n", *offset);

    result = to_copy;
    goto release_lock;

release_lock:
    mutex_unlock(&device->device_mutex);
    return result;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
    .read = device_read,
    .write = device_write,
    .llseek  = default_llseek,
};

static int setup_char_device(struct char_device* dev, int index, struct class * driver_class) {
    dev->qset = NULL;
    dev->size = 0;
    mutex_init(&dev->device_mutex);

    int err, devno = MKDEV(major_device_num, minor_device_num + index);
    
    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;

    err = cdev_add(&dev->cdev, devno, 1);
    if (err < 0) {
        printk(KERN_ERR "cdev add failed (err=%d)\n", err);
        return err;
    }

    struct device *d = device_create(driver_class, NULL, devno, NULL, "chardriver%d", index);
    if (IS_ERR(d)) {
        long derr = PTR_ERR(d);
        printk(KERN_ERR "device create failed (err=%ld)\n", derr);
        cdev_del(&dev->cdev);
        return (int)derr;
    }

    CDEBUG("cdev initialized\n");

    return 0;
}

static void free_all_memory(struct char_device * device) {
    if (device == NULL || device->qset == NULL ) {
        return;
    }

    struct qset * head = device->qset;
    struct qset *next;
    while(head) {
        if (head->data) {
            for (size_t i = 0; i < quantum_per_set; ++i) {
                kfree(head->data[i]);
            }
            kfree(head->data);
        }

        next = head->next;
        kfree(head);
        head = next;
    }

    device->qset = NULL;

    CDEBUG("Device released and memory freed\n");

    return;
}

static char *devnode_func(const struct device *dev, umode_t *mode) {
    if (mode)
        *mode = 0666;
    return NULL;
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
        printk(KERN_ERR "Device number acquisition failed (err=%d)\n", err);
        return err;
    }

    driver_class = class_create(THIS_MODULE, "chardriver_class");
    if (IS_ERR(driver_class)) {
        unregister_chrdev_region(dev_number, DEVICE_COUNT);
        return PTR_ERR(driver_class);
    }
    driver_class->devnode = devnode_func;

    major_device_num = MAJOR(dev_number);
    minor_device_num = MINOR(dev_number);
    CDEBUG("Device registered: Major=%d, Minor=%d\n", major_device_num, minor_device_num);

    for (int idx = 0; idx < DEVICE_COUNT; ++idx) {
        err = setup_char_device(&devices[idx], idx, driver_class);
        if (err < 0) {
            while (--idx >= 0) {
                device_destroy(driver_class, MKDEV(major_device_num, minor_device_num + idx));
                cdev_del(&devices[idx].cdev);
                free_all_memory(&devices[idx]);
            }
            
            class_destroy(driver_class);
            unregister_chrdev_region(dev_number, DEVICE_COUNT);
            return err;
        }    
    }

    return 0;
}

static void __exit clean(void) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
        device_destroy(driver_class, MKDEV(major_device_num, minor_device_num + i));
        cdev_del(&devices[i].cdev);
        free_all_memory(&devices[i]);
    }

    class_destroy(driver_class);
    unregister_chrdev_region(dev_number, DEVICE_COUNT);
}

module_init(initialize);
module_exit(clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Char Drive - scull alike");