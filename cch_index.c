#include <linux/module.h>

#include "cch_index.h"

int cch_index_create(
	int levels,
	int bits,
	int root_bits,
	int low_bits,
	cch_index_start_save_t cch_index_start_save_fn,
	cch_index_finish_save_t cch_index_finish_save_fn,
	cch_index_entry_save_t cch_index_save_fn,
	cch_index_value_free_t cch_index_value_free_fn,
	cch_index_load_data_t cch_entry_load_data_fn,
	cch_index_entry_load_t cch_index_load_entry_fn,
	struct cch_index **out)
{
	*out = vmalloc(sizeof(struct cch_index *));
	if (out == NULL)
		printk(KERN_ERR "vmalloc failed during index create\n");

	return 0;
}
EXPORT_SYMBOL(cch_index_create);

void cch_index_destroy(struct cch_index *index)
{
	BUG_ON(index == NULL);
	vfree(index);
}
EXPORT_SYMBOL(cch_index_destroy);

uint64_t cch_index_save(struct cch_index *index)
{
	BUG_ON(index == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_save);

int cch_index_load(struct cch_index *index, uint64_t start)
{
	BUG_ON(index == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_load);

int cch_index_find(struct cch_index *index, uint64_t key,
		   void **out_value, struct cch_index_entry **index_entry,
		   int *value_offset)
{
	BUG_ON(index == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_find);

int cch_index_find_direct(struct cch_index_entry *entry, int offset,
			  void **out_value,
			  struct cch_index_entry **next_index_entry,
			  int *value_offset)
{
	BUG_ON(entry == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_find_direct);

int cch_index_insert(struct cch_index *index,
		     uint64_t key,  /* key of new record */
		     void *value,   /* value of new record */
		     bool replace,  /* should replace record under same key
				     * If not -- -EEXIST
				     */

		     /* created record */
		     struct cch_index_entry **new_index_entry,
		     /* created offset */
		     int *new_value_offset)
{
	BUG_ON(index == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_insert);

int cch_index_insert_direct(struct cch_index_entry *entry,
			    bool replace,
			    void *value,
			    struct cch_index_entry **new_index_entry,
			    int *new_value_offset)
{
	BUG_ON(entry == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_insert_direct);

int cch_index_remove(struct cch_index *index, uint64_t key)
{
	BUG_ON(index == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_remove);

int cch_index_remove_direct(struct cch_index_entry *entry, int offset)
{
	BUG_ON(entry == NULL);
	return 0;
}
EXPORT_SYMBOL(cch_index_remove_direct);

int cch_index_shrink(struct cch_index_entry *index, int max_mem_kb)
{
	return 0;
}
EXPORT_SYMBOL(cch_index_shrink);

int cch_index_restore(struct cch_index_entry *index)
{
	return 0;
}
EXPORT_SYMBOL(cch_index_restore);
