#define LOG_PREFIX "cch_index_stubs"

#include "cch_index_debug.h"
#include "cch_index.h"

/**
 * This file contains all functions marked as "out of scope of this project"
 * for index to be able to use them
 */

int cch_index_check_lock(void *value)
{
	/* stub implementation */
	printk(KERN_INFO "check lock on %lx\n", (unsigned long) value);
	return 0;
}

int cch_index_value_lock(void *value)
{
	printk(KERN_INFO "value lock on %lx\n", (unsigned long) value);
	return 0;
}

int cch_index_value_unlock(void *value)
{
	printk(KERN_INFO "value unlock on %lx\n", (unsigned long) value);
	return 0;
}

void cch_index_on_new_entry_alloc(struct cch_index_entry *index,
				  int inc_size, int new_size)
{
	printk(KERN_INFO "new index record allocated, %d -> %d\n",
	       inc_size, new_size);
}

void cch_index_on_entry_free(struct cch_index_entry *index,
				    int dec_size, int new_size)
{
	printk(KERN_INFO "index record free, %d -> %d\n", dec_size, new_size);
}

void cch_index_alloc_new_cluster(void)
{
	printk(KERN_INFO "alloc new cluster\n");
}

void cch_index_free_cluster(void)
{
	printk(KERN_INFO "free cluster\n");
}

void cch_index_start_save_fn(void)
{
}

void cch_index_finish_save_fn(void)
{
}

void cch_index_entry_save_fn(void)
{
}

void cch_index_value_free_fn(void)
{
}

void cch_index_load_data_fn(void)
{
}

void cch_index_load_entry_fn(void)
{
}
