obj-m += mlindex.o
mlindex-objs := load.o cch_index.o stubs.o

default: mlindex.ko

check:
	linux/scripts/checkpatch.pl --file cch_index.c
	linux/scripts/checkpatch.pl --file cch_index.h
	linux/scripts/checkpatch.pl --file load.c
	linux/scripts/checkpatch.pl --file stubs.c

mlindex.ko: load.c cch_index.c cch_index.h stubs.c
	make -C linux M=`pwd` modules

clean:
	make -C linux M=`pwd` clean

load: mlindex.ko
	sudo insmod mlindex.ko

unload:
	sudo rmmod mlindex