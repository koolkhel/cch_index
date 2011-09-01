obj-m += cchindex.o
cchindex-objs := load.o cch_index.o stubs.o cch_index_debug.o

SOURCES := load.c cch_index.c cch_index.h stubs.c cch_index_debug.c \
cch_index_debug.h cch_index_common.h

MODULE_NAME := cchindex.ko

EXTRA_CFLAGS := -g

# EXTRA_CFLAGS += -DCCH_INDEX_DEBUG

ifeq ($(KVER),)
  ifeq ($(KDIR),)
    KVER = $(shell uname -r)
    KDIR := /lib/modules/$(KVER)/build
  endif
else
  KDIR := /lib/modules/$(KVER)/build
endif


default: $(MODULE_NAME)

release:
	git archive master | gzip - > ../index.tar.gz

check:
	linux/scripts/checkpatch.pl --emacs --file cch_index.c
	linux/scripts/checkpatch.pl --emacs --file cch_index.h
	linux/scripts/checkpatch.pl --emacs --file cch_index_debug.h
	linux/scripts/checkpatch.pl --emacs --file cch_index_debug.c
	linux/scripts/checkpatch.pl --emacs --file cch_index_common.h
	linux/scripts/checkpatch.pl --emacs --file cch_index_common.c
	linux/scripts/checkpatch.pl --emacs --file cch_index_direct.c
	linux/scripts/checkpatch.pl --emacs --file load.c
	linux/scripts/checkpatch.pl --emacs --file stubs.c

clean:
	rm -f *.o *.ko .*.cmd *.mod.c .*.d .depend Modules.symvers \
                Module.symvers Module.markers modules.order
	rm -rf .tmp_versions

dump: $(MODULE_NAME)
	objdump -DglS $(MODULE_NAME) > asm-source.txt

$(MODULE_NAME): $(SOURCES)
	@echo $(KDIR)
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd)

load: $(MODULE_NAME)
	sudo insmod $(MODULE_NAME)

unload:
	sudo rmmod -vwf $(MODULE_NAME)

last-deploy:
	touch last-deploy

deploy: $(MODULE_NAME) last-deploy
	rm -f asm-source.txt
	lftp -f ftp-deploy

gendocs:
	doxygen doc.conf

.PHONY: gendocs deploy unload load clean default dump clean release