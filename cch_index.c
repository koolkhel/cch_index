#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#define CCH_INDEX_DEBUG
#define LOG_PREFIX "cch_index"

#include "cch_index_debug.h"
#include "cch_index.h"

#define DEBUG

#define EXTRACT_BIASED_VALUE(key, levels_desc, sel_level) \
	((key >> levels_desc[sel_level].offset) & \
	 ((1UL << levels_desc[sel_level].bits) - 1))

#define	EXTRACT_LOWEST_OFFSET(index, key)			\
	EXTRACT_BIASED_VALUE(key, index->levels_desc, index->levels - 1)

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

	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	PRINT_INFO("each %d, to distribute -- %d\n",
	       each_base_size, to_distribute);

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

	TRACE_EXIT();
}

void show_index_description(struct cch_index *index)
{
	int i = 0;
	TRACE_ENTRY();
	PRINT_INFO("number of levels: %d\n", index->levels);
	for (i = 0; i < index->levels; i++) {
		if (i == 0)
			PRINT_INFO("root level: ");
		else if (i == index->levels - 1)
			PRINT_INFO("lowest level: ");

		PRINT_INFO("bits: %d, size: %d, offset: %d",
		       index->levels_desc[i].bits,
		       index->levels_desc[i].size,
		       index->levels_desc[i].offset);
	}
	TRACE_EXIT();
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
	int result;
	struct cch_index *new_index = NULL;

	TRACE_ENTRY();
	new_index = kzalloc(sizeof(struct cch_index) +
		       (1 << root_bits) * sizeof(void *),
		       GFP_KERNEL);

	if (new_index == NULL) {
		printk(KERN_ERR "vmalloc failed during index create\n");
		result = -ENOMEM;
		goto mem_failure;
	}

	/* root + levels + lowest level */
	new_index->levels = levels + 2;
	new_index->levels_desc = kzalloc(new_index->levels *
				      sizeof(struct cch_level_desc_entry),
				      GFP_KERNEL);

	if (new_index->levels_desc == NULL) {
		result = -ENOMEM;
		goto levels_desc_failure;
	}

	generate_level_descriptions(new_index, levels,
				    bits, root_bits, low_bits);
	#ifdef DEBUG
	show_index_description(new_index);
	#endif

	/* FIXME goto's */
	result = 0;
	*out = new_index;
	goto success;
levels_desc_failure:
	kfree(new_index);
mem_failure:
success:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_create);

void cch_index_destroy(struct cch_index *index)
{
	struct cch_index_entry *entry;
	int i = 0, j = 0;
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	entry = &index->head;
	/* FIXME: pages cleanup, need to do a tree walk */
	kfree(index->levels_desc);
	kfree(index);
	TRACE_EXIT();
}
EXPORT_SYMBOL(cch_index_destroy);

uint64_t cch_index_save(struct cch_index *index)
{
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	TRACE_EXIT();
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_save);

int cch_index_load(struct cch_index *index, uint64_t start)
{
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	TRACE_EXIT();
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
	int result = 0;
	int i = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	current_entry = &index->head;

	for (i = 0; i < index->levels - 1; i++) {
		sBUG_ON(current_entry == NULL);

		record_offset = EXTRACT_BIASED_VALUE(key,
						     index->levels_desc, i);
		PRINT_INFO("offset is 0x%x", record_offset);
		PRINT_INFO("value is %p",
		       current_entry->v[record_offset].value);

		if (current_entry->v[record_offset].entry != NULL) {
			current_entry = current_entry->v[record_offset].entry;
		} else if (create_new) {
			current_entry->v[record_offset].value =
				kzalloc(sizeof(struct cch_index_entry) +
					index->levels_desc[i + 1].size *
					sizeof(void *), GFP_KERNEL);

			PRINT_INFO("created new index entry at %p\n",
			       current_entry->v[record_offset].entry);

			if (current_entry->v[record_offset].entry == NULL) {
				PRINT_ERROR("index malloc failed\n");
				*found_entry = NULL;
				result = -ENOSPC;
				goto not_found;
			}

			/* FIXME: parent */
			/* FIXME: everything */

			current_entry = current_entry->v[record_offset].entry;
		} else {
			result = -ENOENT;
			goto not_found;
		}
	}

	*found_entry = current_entry;
	sBUG_ON(found_entry == NULL);
	result = 0;
not_found:
	TRACE_EXIT();
	return 0;
}

int cch_index_find(struct cch_index *index, uint64_t key,
		   void **out_value, struct cch_index_entry **index_entry,
		   int *value_offset)
{
	struct cch_index_entry *current_entry;
	int result = 0;
	int lowest_offset = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	/* we need to dump the result somewhere */
	sBUG_ON(out_value == NULL);
	result = cch_index_find_lowest_entry(index, key,
					     /* should not create new entries */
					     false,
					     &current_entry);
	current_entry->v[0].value = 0;
	if (result) {
		*out_value = 0;
		if (index_entry)
			*index_entry = NULL;
		if (value_offset)
			*value_offset = 0;
		result = -ENOENT;
		goto not_found;
	}
	sBUG_ON(current_entry == NULL);

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("offset is 0x%d", lowest_offset);
	*out_value = current_entry->v[lowest_offset].value;
	if (index_entry)
		*index_entry = current_entry;
	if (value_offset)
		*value_offset = lowest_offset;

	PRINT_INFO("found 0x%lx", (long int) *out_value);
	result = (*out_value == NULL) ? -ENOENT : 0;
not_found:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_find);

int cch_index_find_direct(struct cch_index_entry *entry, int offset,
			  void **out_value,
			  struct cch_index_entry **next_index_entry,
			  int *value_offset)
{
	sBUG_ON(entry == NULL);
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

	TRACE_ENTRY();
	sBUG_ON(index == NULL);

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
	record_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("computed offset is %d", record_offset);
	current_entry->v[record_offset].value = value;
	if (new_value_offset)
		*new_value_offset = record_offset;
	if (new_index_entry)
		*new_index_entry = current_entry;
	result = 0;
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
	TRACE_ENTRY();
	sBUG_ON(entry == NULL);
	TRACE_EXIT();
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_insert_direct);

int cch_index_remove(struct cch_index *index, uint64_t key)
{
	struct cch_index_entry *current_entry;
	int result = 0;
	int lowest_offset = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);

	result = cch_index_find_lowest_entry(index, key,
					     /* should not create new entries */
					     false,
					     &current_entry);
	if (result)
		goto not_found;

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("removing at offset 0x%x", lowest_offset);
	current_entry->v[lowest_offset].value = NULL;
	/* FIXME deref parent, do parent logic */

	result = 0;
not_found:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_remove);

int cch_index_remove_direct(struct cch_index_entry *entry, int offset)
{
	TRACE_ENTRY();
	sBUG_ON(entry == NULL);
	TRACE_EXIT();
	return -ENOENT;
}
EXPORT_SYMBOL(cch_index_remove_direct);

int cch_index_shrink(struct cch_index_entry *index, int max_mem_kb)
{
	TRACE_ENTRY();
	sBUG_ON(index == 0);
	TRACE_EXIT();
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_shrink);

int cch_index_restore(struct cch_index_entry *index)
{
	TRACE_ENTRY();
	sBUG_ON(index == 0);
	TRACE_EXIT();
	return -ENOSPC;
}
EXPORT_SYMBOL(cch_index_restore);
