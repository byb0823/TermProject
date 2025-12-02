obj-m += led_driver.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: module user_program

module:
	make -C $(KDIR) M=$(PWD) modules

user_program:
	gcc -o led_control led_control.c

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f led_control