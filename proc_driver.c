#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BATUHAN");
MODULE_DESCRIPTION("First Kernel Module");

static char* init_word = "file is empty bro\n";
module_param(init_word, charp, S_IRUGO);
                               
void set_content(const char * src);
ssize_t my_custom_read_function(struct file * file_pointer, char * user_space_buffer, size_t count, loff_t * offset);
ssize_t my_custom_write_function(struct file * file_pointer, const char * user_space_buffer, size_t count, loff_t * offset);

static struct proc_dir_entry * proc_entry;
static char * content;

ssize_t my_custom_read_function (struct file * file_pointer,
                                 char * user_space_buffer,
                                 size_t count,
                                 loff_t * offset) {
    if (!content) {
        int len = strlen(init_word);

        if (*offset >= len) {
            return 0;
        }

        int err = copy_to_user(user_space_buffer, init_word, len);
        if(err) {
            return -EFAULT;
        }
        *offset += len;
        return len;
    }

    int len = strlen(content);

    if (*offset >= len) {
        return 0;
    }

    if (copy_to_user(user_space_buffer, content, len))
        return -EFAULT;

    *offset += len;
    return len;
}

ssize_t	my_custom_write_function (struct file * file_pointer,
                                  const char * user_space_buffer,
                                  size_t count,
                                  loff_t * offset) {
    char local_buf[128];

    if (count >= sizeof(local_buf))
        return -EINVAL;

    if (copy_from_user(local_buf, user_space_buffer, count))
        return -EFAULT;

    local_buf[count] = '\0';  // Null-terminate

    set_content(local_buf);

    return count;  // ✅ Must return count if success
}

struct proc_ops ops = {
    .proc_read = my_custom_read_function,
    .proc_write = my_custom_write_function
};


static int __init batuhan_module_init (void) {
    proc_entry = proc_create(
        "batuhan_driver",
        0666,
        NULL,
        &ops);

    if (proc_entry == NULL) {
           return -1;
    }

    return 0;
}

static void __exit batuhan_module_exit (void) {
    proc_remove(proc_entry);
}

void set_content(const char * src) {
    char * dest = kmalloc(strlen(src) + 1, GFP_KERNEL);
    if (!dest) {
        return;
    }

    strcpy(dest, src);

    if (content) {
        kfree(content);
    }
    content = dest;
}


module_init(batuhan_module_init);
module_exit(batuhan_module_exit);