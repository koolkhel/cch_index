#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#define LOG_PREFIX "cch_index"

#include "cch_index.h"
#include "cch_index_debug.h"
#include "cch_index_common.h"

#define DEBUG

/**
 * Generate level description structure with given parameters.
 * BUG if requested configuration requires non-equal sizes
 * of middle level entries.
 *
 * @arg index
 * @arg levels
 * @arg bits
 * @arg root_bits
 * @arg low_bits
 */
static int generate_level_descriptions(struct cch_index *index,
	int levels,
	int bits,
	int root_bits,
	int low_bits)
{
	int mid_level_size = (bits - (root_bits + low_bits)) / levels;
	int misbits = (bits - (root_bits + low_bits)) % levels;
	int i = 0;
	int next_level_offset = 0;
	int result = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	/* there is an assumption that all mid level cache entries are
	   of the same size, so there should be no leftover
	 */
	sBUG_ON(misbits != 0);

	PRINT_INFO("each %d\n", mid_level_size);

	index->levels_desc[index->levels - 1].bits = low_bits;
	index->levels_desc[index->levels - 1].size = 1UL << low_bits;
	index->levels_desc[index->levels - 1].offset = low_bits;
	index->lowest_level = index->levels - 1; /* probably a bad idea */

	next_level_offset = 0;

	/* walking backwards, lower levels will be thicker */
	for (i = index->levels - 1; i > 0; i--) {
		index->levels_desc[i].bits = mid_level_size;

		index->levels_desc[i].size = 1UL << index->levels_desc[i].bits;
		index->levels_desc[i].offset = next_level_offset;
		next_level_offset += index->levels_desc[i].bits;
		index->mid_level = i; /* probably a bad idea */
	}

	index->levels_desc[0].bits = root_bits;
	index->levels_desc[0].size = 1UL << root_bits;
	index->levels_desc[0].offset = next_level_offset;
	index->root_level = 0; /* probably a bad idea */

	TRACE_EXIT();
	return result;
}

#ifdef CCH_INDEX_DEBUG
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
	return;
}
#endif

int cch_index_create(
	int levels,
	int bits,
	int root_bits,
	int low_bits,
	cch_index_start_save_t cch_index_start_save_fn,
	cch_index_finish_save_t cch_index_finish_save_fn,
	cch_index_entry_save_t cch_index_save_fn,
	cch_index_value_free_t cch_index_value_free_fn,
	cch_index_load_data_t cch_index_load_data_fn,
	cch_index_entry_load_t cch_index_load_entry_fn,
	struct cch_index **out)
{
	int result;
	struct cch_index *new_index = NULL;
#define CACHE_NAME_BUF_SIZE 30
	char slab_name_buf[CACHE_NAME_BUF_SIZE];

	TRACE_ENTRY();

	new_index = kzalloc(sizeof(struct cch_index) +
			    (1 << root_bits) * sizeof(uint64_t),
			    GFP_KERNEL);
	if (new_index == NULL) {
		PRINT_ERROR("vmalloc failed during index create");
		result = -ENOMEM;
		goto out;
	}

	new_index->start_save_fn  = cch_index_start_save_fn;
	new_index->finish_save_fn = cch_index_finish_save_fn;
	new_index->entry_save_fn  = cch_index_save_fn;
	new_index->value_free_fn  = cch_index_value_free_fn;
	new_index->load_data_fn   = cch_index_load_data_fn;
	new_index->entry_load_fn  = cch_index_load_entry_fn;

	/* root + levels + lowest level */
	new_index->levels = levels + 2;
	new_index->levels_desc = kzalloc(
		new_index->levels * sizeof(struct cch_level_desc_entry),
		GFP_KERNEL);
	if (new_index->levels_desc == NULL) {
		result = -ENOMEM;
		goto out_free_index;
	}

	result = generate_level_descriptions(new_index, levels,
		bits, root_bits, low_bits);
	if (result) {
		PRINT_ERROR("error creating caches\n");
		goto out_free_index;
	}

#ifdef CCH_INDEX_DEBUG
	show_index_description(new_index);
#endif

	snprintf(slab_name_buf, CACHE_NAME_BUF_SIZE,
		 "cch_index_low_level_%p", new_index);
	/* FIXME unique index name */
	new_index->low_level_kmem = kmem_cache_create(slab_name_buf,
		new_index->levels_desc[new_index->lowest_level].size *
		sizeof(new_index->head.v[0]) +
		sizeof(struct cch_index_entry),
		CCH_INDEX_LOW_LEVEL_ALIGN, 0, NULL);
	if (new_index->low_level_kmem == NULL) {
		result = -ENOMEM;
		goto out_free_descriptions;
	}

	PRINT_INFO("cch_index_low_level object size %d",
		   kmem_cache_size(new_index->low_level_kmem));

	snprintf(slab_name_buf, CACHE_NAME_BUF_SIZE,
		 "cch_index_mid_level_%p", new_index);
	new_index->mid_level_kmem = kmem_cache_create(slab_name_buf,
		new_index->levels_desc[new_index->mid_level].size *
		sizeof(new_index->head.v[0]) +
		sizeof(struct cch_index_entry),
		CCH_INDEX_MID_LEVEL_ALIGN, 0, NULL);
	if (!new_index->mid_level_kmem) {
		result = -ENOMEM;
		goto out_free_low_level_kmem;
	}

	PRINT_INFO("cch_index_mid_level object size %d",
		   kmem_cache_size(new_index->mid_level_kmem));

	*out = new_index;
out:
	TRACE_EXIT();
	return result;

out_free_low_level_kmem:
	kmem_cache_destroy(new_index->low_level_kmem);
out_free_descriptions:
	kfree(new_index->levels_desc);
out_free_index:
	kfree(new_index);
	goto out;
}
EXPORT_SYMBOL(cch_index_create);

void cch_index_destroy(struct cch_index *index)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);

	/* FIXME check usage */
	/* FIXME locking */
	cch_index_destroy_root_entry(index);
	kmem_cache_destroy(index->low_level_kmem);
	kmem_cache_destroy(index->mid_level_kmem);
	kfree(index->levels_desc);
	kfree(index);

	TRACE_EXIT();
	return;
}
EXPORT_SYMBOL(cch_index_destroy);

uint64_t cch_index_save(struct cch_index *index)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);

	TRACE_EXIT();
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_save);

int cch_index_load(struct cch_index *index, uint64_t start)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);

	TRACE_EXIT();
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_load);

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

	result = cch_index_walk_path(index, key, &current_entry);
	if (result) {
		*out_value = 0;
		if (index_entry)
			*index_entry = NULL;
		if (value_offset)
			*value_offset = 0;
		result = -ENOENT;
		goto out;
	}

	sBUG_ON(current_entry == NULL);

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("offset is 0x%x", lowest_offset);
	*out_value = current_entry->v[lowest_offset].value;
	if (index_entry)
		*index_entry = current_entry;
	if (value_offset)
		*value_offset = lowest_offset;

	PRINT_INFO("found 0x%lx", (long int) *out_value);
	result = (*out_value == NULL) ? -ENOENT : 0;

out:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_find);

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
#ifdef CCH_INDEX_DEBUG
	int i = 0;
#endif
	int result = 0;
	struct cch_index_entry *current_entry = NULL;
	int record_offset = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);

#ifdef CCH_INDEX_DEBUG
	PRINT_INFO("key is 0x%.8llx", key);
	for (i = 0; i < index->levels; i++) {
		PRINT_INFO("part %d is 0x%.2llx", i,
			   EXTRACT_BIASED_VALUE(key, index->levels_desc, i));
	}
#endif

	result = cch_index_create_path(index, key, &current_entry);

	if (result)
		goto out;

	sBUG_ON(current_entry == NULL);

	record_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("computed offset is %d", record_offset);

	/* save value to index */
	result = cch_index_entry_insert_direct(
		index, current_entry, record_offset, replace, value);
	if (result)
		goto out;

	if (new_value_offset)
		*new_value_offset = record_offset;
	if (new_index_entry)
		*new_index_entry = current_entry;
	result = 0;

out:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_insert);

int cch_index_remove(struct cch_index *index, uint64_t key)
{
	struct cch_index_entry *current_entry;
	int result = 0;
	int lowest_offset = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);

	result = cch_index_walk_path(index, key, &current_entry);
	if (result)
		goto not_found;

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);

	cch_index_entry_remove_value(index, current_entry, lowest_offset);

	/* and now we check if some entries should be cleaned up */
	cch_index_entry_cleanup(index, current_entry);

	result = 0;

not_found:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_remove);

int cch_index_shrink(struct cch_index_entry *index, int max_mem_kb)
{
	TRACE_ENTRY();

	sBUG_ON(index == 0);

	TRACE_EXIT();
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_shrink);

int cch_index_restore(struct cch_index_entry *index)
{
	TRACE_ENTRY();

	sBUG_ON(index == 0);

	TRACE_EXIT();
	return -ENOMEM;
}
EXPORT_SYMBOL(cch_index_restore);
