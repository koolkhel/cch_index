#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#include "cch_index.h"

#define DEBUG

/*
 * distribute "bits" amongst "levels" keeping the results
 */
static void generate_level_descriptions(struct cch_index *index,
					int levels,
					int bits,
					int root_bits,
					int low_bits)
{
	int each_base_size = bits / levels;
	int to_distribute= bits % levels;
	int i = 0;
	int next_level_offset = 0;

	#ifdef DEBUG
	printk("each %d, to distribute -- %d\n", each_base_size, to_distribute);
	#endif
	
	index->levels_desc[index->levels - 1].bits = low_bits;
	index->levels_desc[index->levels - 1].size = 1UL << low_bits;
	index->levels_desc[index->levels - 1].offset = low_bits;
	next_level_offset = 0;
	
	/* walking backwards, lower levels will be thicker */
	for (i = index->levels - 1; i > 0; i--) {
		index->levels_desc[i].bits = each_base_size;
		if (to_distribute) {
			index->levels_desc[i].bits++;
			to_distribute--;
		}

		index->levels_desc[i].size = 1UL << index->levels_desc[i].bits;
		index->levels_desc[i].offset = next_level_offset;
		next_level_offset += index->levels_desc[i].bits;

	}

	index->levels_desc[0].bits = root_bits;
	index->levels_desc[0].size = 1UL << root_bits;
	index->levels_desc[0].offset = next_level_offset;
}

void show_index_description(struct cch_index *index)
{
	int i = 0;
	printk(KERN_INFO "number of levels: %d\n", index->levels);
	for (i = 0; i < index->levels; i++) {
		if (i == 0)
			printk(KERN_INFO "root level: ");
		else if (i == index->levels - 1)
			printk(KERN_INFO "lowest level: ");
		
		printk(KERN_INFO "bits: %d, size: %d, offset: %d\n",
		       index->levels_desc[i].bits,
		       index->levels_desc[i].size,
		       index->levels_desc[i].offset);
	}
}

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
	*out = kmalloc(sizeof(struct cch_index), GFP_KERNEL);
	if (out == NULL) {
		printk(KERN_ERR "vmalloc failed during index create\n");
		goto out_failure;
	}

	/* root + levels + lowest level */
	(*out)->levels = levels + 2;
	(*out)->levels_desc = kmalloc((*out)->levels *
				      sizeof(struct cch_level_desc_entry),
				      GFP_KERNEL);

	if ((*out)->levels_desc == NULL) {
		goto levels_desc_failure;
	}
	
	generate_level_descriptions(*out, levels, bits, root_bits, low_bits);
	#ifdef DEBUG
	show_index_description(*out);
	#endif
	
	return 0;
	
levels_desc_failure:
	kfree(*out);
out_failure:
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_create);

void cch_index_destroy(struct cch_index *index)
{
	BUG_ON(index == NULL);
	kfree(index->levels_desc);
	kfree(index);
}
EXPORT_SYMBOL(cch_index_destroy);

uint64_t cch_index_save(struct cch_index *index)
{
	BUG_ON(index == NULL);
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_save);

int cch_index_load(struct cch_index *index, uint64_t start)
{
	BUG_ON(index == NULL);
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_load);

int cch_index_find(struct cch_index *index, uint64_t key,
		   void **out_value, struct cch_index_entry **index_entry,
		   int *value_offset)
{
	BUG_ON(index == NULL);
	return -ENOENT;
}
EXPORT_SYMBOL(cch_index_find);

int cch_index_find_direct(struct cch_index_entry *entry, int offset,
			  void **out_value,
			  struct cch_index_entry **next_index_entry,
			  int *value_offset)
{
	BUG_ON(entry == NULL);
	return -ENOENT;
}
EXPORT_SYMBOL(cch_index_find_direct);

#define EXTRACT_VALUE(key, offset, bits) \
	((key >> offset) & ((1UL << bits) - 1))

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
	int i = 0;
	BUG_ON(index == NULL);

	printk("key is 0x%.8llx\n", key);
	for (i = 0; i < index->levels; i++) {
		printk("part %d is 0x%.2x\n",
		       i,
		       EXTRACT_VALUE(key, index->levels_desc[i].offset,
				     index->levels_desc[i].bits));
	}
	
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_insert);

int cch_index_insert_direct(struct cch_index_entry *entry,
			    bool replace,
			    void *value,
			    struct cch_index_entry **new_index_entry,
			    int *new_value_offset)
{
	BUG_ON(entry == NULL);
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_insert_direct);

int cch_index_remove(struct cch_index *index, uint64_t key)
{
	BUG_ON(index == NULL);
	return -ENOENT;
}
EXPORT_SYMBOL(cch_index_remove);

int cch_index_remove_direct(struct cch_index_entry *entry, int offset)
{
	BUG_ON(entry == NULL);
	return -ENOENT;
}
EXPORT_SYMBOL(cch_index_remove_direct);

int cch_index_shrink(struct cch_index_entry *index, int max_mem_kb)
{
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_shrink);

int cch_index_restore(struct cch_index_entry *index)
{
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_restore);
