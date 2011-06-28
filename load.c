#include <linux/kernel.h>
#include <linux/module.h>

/* shouldn't it go to linux source tree? */
#include "cch_index.h"

/* 
 * Test index creation, insert, remove and destroy with
 * single value and no direct access to index entries.
 */
static int smoke_test(void)
{
	#define INDEX_TEST_KEY 0x0102030401020304ULL
	#define INDEX_TEST_VALUE 0x04030201
	
	struct cch_index *index = NULL;
	int result = 0;
	struct cch_index_entry *new_index_entry;
	int new_value_offset = 0;
	void *found_value = NULL;

	printk(KERN_INFO "index creation start\n");
	result = cch_index_create(/* levels */    4,
				  /* bits */      40,
				  /* root_bits */ 12,
				  /* low_bits */  12, /* 64 total */
				  cch_index_start_save_fn,
				  cch_index_finish_save_fn,
				  cch_index_entry_save_fn,
				  cch_index_value_free_fn,
				  cch_index_load_data_fn,
				  cch_index_load_entry_fn,
				  &index);
	if (result != 0) {
		printk(KERN_INFO "index creation failure, result %d\n", result);
		goto creation_failure;
	} else {
		printk(KERN_INFO "index creation successful\n");
	}

	printk(KERN_INFO "index insert start\n");
	result = cch_index_insert(index,
				  /* key */ INDEX_TEST_KEY,
				  /* value */ (void*) INDEX_TEST_VALUE,
				  /* replace */ false,
				  &new_index_entry,
				  &new_value_offset);

	if (result != 0) {
		printk(KERN_INFO "index insert failure, result %d\n", result);
		goto insert_failure;
	} else {
		printk(KERN_INFO "index insert ok, address %lx, offset %x\n",
		       (unsigned long) new_index_entry,
		       new_value_offset);
	}

	printk("index search start\n");
	result = cch_index_find(index, INDEX_TEST_KEY, &found_value,
				&new_index_entry, &new_value_offset);

	if (result != 0) {
		printk(KERN_INFO "index search failure, result %d\n", result);
		goto search_failure;
	} else {
		printk(KERN_INFO "index search success at address %lx, offset %x, "
		       "with value %lx\n", (unsigned long) new_index_entry,
		       new_value_offset,
		       (unsigned long) found_value);
		if (found_value == (void *) INDEX_TEST_VALUE) {
			printk(KERN_INFO "found exact match\n");
		} else {
			printk(KERN_INFO "found wrong match\n");
		}
	}

search_failure:
	printk(KERN_INFO "index remove start\n");
	result = cch_index_remove(index, INDEX_TEST_KEY);

	if (result != 0) {
		printk(KERN_INFO "index remove failure, result %d\n", result);
		goto remove_failure;
	} else {
		printk(KERN_INFO "index remove successful\n");
	}

insert_failure:
remove_failure:
	printk(KERN_INFO "index destroy start\n");
	cch_index_destroy(index);
	printk(KERN_INFO "index destroy successful\n");
creation_failure:
	return result;
}

static int __init reldata_index_init(void)
{
	int result = 0;
	printk(KERN_INFO "hello, world!\n");
	result = smoke_test();
	if (result != 0) {
		printk(KERN_INFO "smoke test failure\n");
	} else {
		printk(KERN_INFO "we can send that! congragulations!\n");
	}
	return 0;
}

static void reldata_index_shutdown(void)
{
	printk(KERN_INFO "bye!\n");
}

MODULE_LICENSE("GPL");

module_init(reldata_index_init);
module_exit(reldata_index_shutdown);
