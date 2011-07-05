#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#include "cch_index.h"

#define DEBUG

#define EXTRACT_BIASED_VALUE(key, levels_desc, sel_level) \
	((key >> levels_desc[sel_level].offset) & \
	 ((1UL << levels_desc[sel_level].bits) - 1))

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
	int to_distribute = bits % levels;
	int i = 0;
	int next_level_offset = 0;

	#ifdef DEBUG
	printk(KERN_INFO "each %d, to distribute -- %d\n",
	       each_base_size, to_distribute);
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
	*out = vzalloc(sizeof(struct cch_index) +
		       (1 << root_bits) * sizeof(void *));
	if (out == NULL) {
		printk(KERN_ERR "vmalloc failed during index create\n");
		goto out_failure;
	}

	/* root + levels + lowest level */
	(*out)->levels = levels + 2;
	(*out)->levels_desc = vzalloc((*out)->levels *
				      sizeof(struct cch_level_desc_entry));

	if ((*out)->levels_desc == NULL)
		goto levels_desc_failure;

	generate_level_descriptions(*out, levels, bits, root_bits, low_bits);
	#ifdef DEBUG
	show_index_description(*out);
	#endif

	return 0;

levels_desc_failure:
	vfree(*out);
out_failure:
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_create);

void cch_index_destroy(struct cch_index *index)
{
	struct cch_index_entry *entry;
	int i = 0, j = 0;
	BUG_ON(index == NULL);
	entry = &index->head;
	/* FIXME: pages cleanup */
	vfree(index->levels_desc);
	vfree(index);
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

static int cch_index_find_lowest_entry(struct cch_index *index,
				       uint64_t key,
				       bool create_new,
				       struct cch_index_entry **found_entry)
{
	int record_offset = 0;
	struct cch_index_entry *current_entry;
	int i = 0;

	BUG_ON(index == NULL);
	current_entry = &index->head;

	for (i = 0; i < index->levels - 1; i++) {
		BUG_ON(current_entry == NULL);

		record_offset = EXTRACT_BIASED_VALUE(key,
						     index->levels_desc, i);
#ifdef DEBUG
		printk(KERN_DEBUG "offset is 0x%x\n", record_offset);
		printk(KERN_DEBUG "value is %p\n",
		       current_entry->v[record_offset].value);
#endif

		if (current_entry->v[record_offset].entry != NULL) {
			current_entry = current_entry->v[record_offset].entry;
		} else if (create_new) {
			current_entry->v[record_offset].value =
				vzalloc(sizeof(struct cch_index_entry) +
					index->levels_desc[i + 1].size *
					sizeof(void *));

			printk(KERN_INFO "created new index entry at %p\n",
			       current_entry->v[record_offset].entry);

			if (current_entry->v[record_offset].entry == NULL) {
				printk(KERN_ERR "index malloc failed\n");
				return -ENOSPC;
			}

			/* FIXME: parent */
			/* FIXME: everything */

			current_entry = current_entry->v[record_offset].entry;
		} else {
			return -ENOENT;
		}
	}

	*found_entry = current_entry;
	return 0;
}

int cch_index_find(struct cch_index *index, uint64_t key,
		   void **out_value, struct cch_index_entry **index_entry,
		   int *value_offset)
{
	struct cch_index_entry *current_entry;
	int result = 0;
	int lowest_offset = 0;

	BUG_ON(index == NULL);
	result = cch_index_find_lowest_entry(index, key,
					     /* should not create new entries */
					     false,
					     &current_entry);
	if (result)
		goto not_found;

	lowest_offset = EXTRACT_BIASED_VALUE(key, index->levels_desc,
					     index->levels);
	*out_value = current_entry->v[lowest_offset].value;
	*value_offset = lowest_offset;

	return *out_value == NULL ? -ENOENT : 0;
not_found:
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
	int result = 0;
	struct cch_index_entry *current_entry = NULL;
	int record_offset = 0;

	BUG_ON(index == NULL);

	#ifdef DEBUG
	printk(KERN_DEBUG "key is 0x%.8llx\n", key);
	for (i = 0; i < index->levels; i++) {
		printk(KERN_DEBUG "part %d is 0x%.2llx\n", i,
		       EXTRACT_BIASED_VALUE(key,
					    index->levels_desc,
					    i));
	}
	#endif

	result = cch_index_find_lowest_entry(index, key,
					     /* should create new index
					      entries? */ true, &current_entry);

	if (result)
		goto not_found;

/* now at lowest level */
	record_offset = EXTRACT_BIASED_VALUE(key,
					     index->levels_desc,
					     index->levels - 1);
	current_entry->v[record_offset].value = value;

	return 0;
not_found:
	return result;
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
	struct cch_index_entry *current_entry;
	int result = 0;
	int lowest_offset = 0;

	BUG_ON(index == NULL);

	result = cch_index_find_lowest_entry(index, key,
					     /* should not create new entries */
					     false,
					     &current_entry);
	if (result)
		goto not_found;

	lowest_offset = EXTRACT_BIASED_VALUE(key, index->levels_desc,
					     index->levels);
	current_entry->v[lowest_offset].value = NULL;

	return 0;
not_found:
	return result;
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
