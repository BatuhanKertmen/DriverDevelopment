#undef CDEBUG /* undef it, just in case */
#ifdef CHAR_DEBUG
    #ifdef __KERNEL__
        /* This one if debugging is on, and kernel space */
        #define CDEBUG(fmt, ...) printk( KERN_INFO "char_device: %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
    # else
        /* This one for user space */
        # define CDEBUG(fmt, ...) fprintf(stderr, fmt,  ##__VA_ARGS__)
    # endif
#else
    # define CDEBUG(fmt, ...) /* not debugging: nothing */
#endif

#undef CDEBUGG
#define CDEBUGG(fmt, ...) /* nothing: it's a placeholder */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h> 
#include <asm/uaccess.h>

#define DEVICE_NAME "chardriver"
#define DEVICE_COUNT 3

static struct class *driver_class;

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

int device_open(struct inode * inode, struct file * filp);
int device_release(struct inode *inode, struct file *filp);
ssize_t device_read(struct file * filp, char __user * buffer, size_t count, loff_t * offset);
ssize_t device_write(struct file * filp, const char __user * buffer, size_t count, loff_t * offset);
int free_all_memory(struct char_device * device);
struct qset* init_qset(int quantum_per_set, int quantum_size);

static struct char_device devices[DEVICE_COUNT];

static dev_t dev_number;

static int major_device_num = 0;
static int minor_device_num = 0;
module_param(major_device_num, int, S_IRUGO);
module_param(minor_device_num, int, S_IRUGO);

struct qset* init_qset(int quantum_per_set, int quantum_size){
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

    
    for (int idx = 0; idx < quantum_per_set; ++idx) {
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

    CDEBUG("Allocated qset with %d quantums of size %d\n", quantum_per_set, quantum_size);

    return qset;
}

int device_open(struct inode * inode, struct file * filp) {
    struct char_device* device;
    device = container_of(inode->i_cdev , struct char_device, cdev);

    filp->private_data = device;

    CDEBUG("Device with minor %d opened\n", iminor(inode));
    return 0;
}

int device_release(struct inode *inode, struct file *filp) {
    CDEBUG("Device with minor %d released\n", iminor(inode));
    return 0;
}

ssize_t device_read(struct file * filp, char __user * buffer, size_t count, loff_t * offset) {
    struct char_device * device = filp->private_data;

    loff_t pos = *offset;
    loff_t qset_bytes = (loff_t)quantum_per_set * quantum_size;

    if (*offset >= device->size) {
        CDEBUG("eof reached");
        return 0;
    }

    CDEBUG("starting");

    int qset_idx = pos / qset_bytes;
    struct qset * cur = device->qset;
    for (int current_qset_idx = 0; current_qset_idx < qset_idx; ++current_qset_idx) {
        cur = cur->next;
    }

    CDEBUG("current qset index %d", qset_idx);

    size_t quantum_idx = (pos % qset_bytes) / quantum_size;
    size_t quantum_offset = (pos % qset_bytes) % quantum_size;

    if (quantum_idx >= quantum_per_set) {
        printk(KERN_ERR "quantum_idx %zu exceeds limit (%d)\n", quantum_idx, quantum_per_set);
        return -EFAULT;
    }
    
    int available = quantum_size - quantum_offset;

    CDEBUG("trying to read %zu th quantum with %zu offset", quantum_idx, quantum_offset);

    if (!cur->data || !cur->data[quantum_idx]) {
        return 0;
    }
    
    int to_copy = count < available ? count : available;
    
    CDEBUG("copying %d bytes", to_copy);
    
    if (copy_to_user(buffer, cur->data[quantum_idx] + quantum_offset, to_copy)) {
        return -EFAULT;
    }
    *offset += to_copy;
    
    CDEBUG("success");

    return to_copy;
}

ssize_t device_write(struct file * filp, const char __user * buffer, size_t count, loff_t * offset) {
    struct char_device * device = filp->private_data;

    loff_t pos = *offset;
    loff_t qset_bytes = (loff_t)quantum_per_set * quantum_size;

    int qset_idx = pos / qset_bytes;
    struct qset * cur = device->qset;

    CDEBUG("current qset index is %d", qset_idx);

    if (!device->qset) {
        device->qset = init_qset(quantum_per_set, quantum_size);
        if (device->qset == NULL) {
            return -ENOMEM;
        }

        cur = device->qset;
    }

    
    for (int current_qset_idx = 1; current_qset_idx < qset_idx; ++current_qset_idx) {
        if (cur->next == NULL) {
            cur->next = init_qset(quantum_per_set, quantum_size);
            if (cur->next == NULL) {
                return -ENOMEM;
            }
        }

        cur = cur->next;
    }

    CDEBUG("accessed correct qset");

    size_t quantum_idx = (pos % qset_bytes) / quantum_size;
    size_t quantum_offset = (pos % qset_bytes) % quantum_size;

    if (quantum_idx >= quantum_per_set) {
        printk(KERN_ERR "quantum_idx %zu exceeds limit (%d)\n", quantum_idx, quantum_per_set);
        return -EFAULT;
    }

    int available = quantum_size - quantum_offset;
    int to_copy = count < available ? count : available;

    CDEBUG("trying to access %zu indexed quantum %zu offset", quantum_idx, quantum_offset);

    if (copy_from_user(cur->data[quantum_idx] + quantum_offset, buffer, to_copy)) {
        return -EFAULT;
    }

    CDEBUG("wrote %d bytes", to_copy);
   
    if (pos + to_copy > device->size) {
        device->size = pos + to_copy;
    }
    *offset += to_copy;

    CDEBUG("offset is updated to %lld", *offset);

    return to_copy;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
    .read = device_read,
    .write = device_write,
};

static int setup_char_device(struct char_device* dev, int index, struct class * driver_class) {
    dev->qset = NULL;
    dev->size = 0;

    int err, devno = MKDEV(major_device_num, minor_device_num + index);
    
    device_create(driver_class, NULL, devno, NULL, "chardriver%d", index);

    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;

    err = cdev_add(&dev->cdev, devno, 1);
    if (err < 0) {
        printk(KERN_ERR "cdev add failed (err=%d)\n", err);
        return err;
    }

    CDEBUG("cdev initialized");

    return 0;
}

int free_all_memory(struct char_device * device) {
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

    CDEBUG("Device released and memory freed\n");

    return 0;
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

    driver_class = class_create("chardriver_class");
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
            return err;
        }    
    }

    return 0;
}

static void __exit clean(void) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
        free_all_memory(&devices[i]);
        cdev_del(&devices[i].cdev);
        device_destroy(driver_class, MKDEV(major_device_num, minor_device_num + i));
    }

    class_destroy(driver_class);
    unregister_chrdev_region(dev_number, DEVICE_COUNT);
}

module_init(initialize);
module_exit(clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Char Drive - scull alike");