obj-m += first_char_drive.o

# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
 	EXTRA_CFLAGS += -O -g -DCHAR_DEBUG
else
 	EXTRA_CFLAGS += -O2
endif


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean