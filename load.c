#include <linux/kernel.h>
#include <linux/module.h>

/* shouldn't it go to linux source tree? */
#include "cch_index.h"

static int __init reldata_index_init(void)
{
	printk(KERN_INFO "hello, world!\n");
	return 0;
}

static void reldata_index_shutdown(void)
{
}

MODULE_LICENSE("GPL");

module_init(reldata_index_init);
module_exit(reldata_index_shutdown);
