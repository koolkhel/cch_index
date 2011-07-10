obj-m += mlindex.o
mlindex-objs := load.o cch_index.o stubs.o cch_index_debug.o

default: mlindex.ko

check:
	linux/scripts/checkpatch.pl --file cch_index.c
	linux/scripts/checkpatch.pl --file cch_index.h
	linux/scripts/checkpatch.pl --file cch_index_debug.h
	linux/scripts/checkpatch.pl --file cch_index_debug.c
	linux/scripts/checkpatch.pl --file load.c
	linux/scripts/checkpatch.pl --file stubs.c

mlindex.ko: load.c cch_index.c cch_index.h stubs.c cch_index_debug.c cch_index_debug.h
	make -C linux M=`pwd` modules

clean:
	make -C linux M=`pwd` clean

load: mlindex.ko
	sudo insmod mlindex.ko

unload:
	sudo rmmod -f mlindex

last-deploy:
	touch last-deploy

deploy: mlindex.ko last-deploy
	lftp -f ftp-deploy
