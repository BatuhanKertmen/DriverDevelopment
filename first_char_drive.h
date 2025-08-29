    #ifndef FIRST_CHAR_DRIVE_H
    #define FIRST_CHAR_DRIVE_H

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


    #include <linux/cdev.h>
    #include <linux/mutex.h>


    #define DEVICE_NAME "chardriver"
    #define DEVICE_COUNT 3


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
        struct mutex device_mutex;
    };

    #endif