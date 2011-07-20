obj-m += cchindex.o
cchindex-objs := load.o cch_index.o stubs.o cch_index_debug.o

MODULE_NAME := cchindex.ko 

default: $(MODULE_NAME)

release:
	git archive master | gzip - > ../index.tar.gz

check:
	linux/scripts/checkpatch.pl --emacs --file cch_index.c
	linux/scripts/checkpatch.pl --emacs --file cch_index.h
	linux/scripts/checkpatch.pl --emacs --file cch_index_debug.h
	linux/scripts/checkpatch.pl --emacs --file cch_index_debug.c
	linux/scripts/checkpatch.pl --emacs --file load.c
	linux/scripts/checkpatch.pl --emacs --file stubs.c
EXTRA_CFLAGS:=-g

dump: $(MODULE_NAME)
	objdump -DglS $(MODULE_NAME) > asm-source.txt

$(MODULE_NAME): load.c cch_index.c cch_index.h stubs.c cch_index_debug.c cch_index_debug.h
	make -C linux M=`pwd` modules

clean:
	make -C linux M=`pwd` clean

load: $(MODULE_NAME)
	sudo insmod $(MODULE_NAME)

unload:
	sudo rmmod -vwf $(MODULE_NAME)

last-deploy:
	touch last-deploy

deploy: $(MODULE_NAME) last-deploy
	lftp -f ftp-deploy
