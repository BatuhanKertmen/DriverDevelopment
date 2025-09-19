#ifndef FIRST_CHAR_IOCTL_H
#define FIRST_CHAR_IOCTL_H

#define FCD_IOC_MAGIC 'f'

#define FCD_IOCRESET _IO(FCD_IOC_MAGIC, 0)
#define FCD_IOCRESET _IO(FCD_IOC_MAGIC, 0)

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */

 #define FCD_IOCRESET _IOS(FCD_IOC_MAGIC, 1, int)

 #define FCD_MAX_NUMBER 1

#endif