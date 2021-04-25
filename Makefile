obj-m := the_tag_service.o
the_tag_service-objs += tag_service.o ./lib/usctm.o ./lib/vtpm.o tag_device_driver.o

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
PWD=$(ROOT_DIR)
KDIR=/lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

monta:
	sudo insmod the_tag_service.ko

smonta:
	sudo rmmod the_tag_service

# la @ permette di evitare di stampare il comando quando si esegue il goal
# la \ permette di andare a capo. Nelle stringhe, se prima di \ ci sono degli spazi, vengono stampati anche quelli.
params:
	@printf "1. free_entries = %s\n\
	2. sys_call_table = 0x%x \n\
	3. sys_ni_syscall = 0x%x\n" \
	$(shell sudo cat /sys/module/the_tag_service/parameters/free_entries) \
	$(shell sudo cat /sys/module/the_tag_service/parameters/sys_call_table_address) \
	$(shell sudo cat /sys/module/the_tag_service/parameters/sys_ni_syscall_address)


compile_test:
	@gcc user/user_tests.c user/tag_ctl_test.c user/tag_get_test.c user/tag_receive_test.c user/tag_send_test.c user/test_definitions.c -o user/test.out -lpthread

run: compile_test
	@./user/test.out

zudo_run: compile_test
	@sudo ./user/test.out

destroy_dmesg:
	sudo dmesg -C

#permette di controllare l'esistenza dei char device. Il primo controlla i nodi in /dev, il secondo in /class
devices:
	ls -la /sys/class/ts_class/
	ls /dev/ts*

fresh_run: smonta destroy_dmesg monta run

compile_user:
	@gcc user/user_program.c user/test_definitions.c -o user/user.out

user_app: compile_user
	@./user/user.out