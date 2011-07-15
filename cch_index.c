#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#define CCH_INDEX_DEBUG
#define LOG_PREFIX "cch_index"

#include "cch_index_debug.h"
#include "cch_index.h"

#define DEBUG

#define EXTRACT_BIASED_VALUE(key, levels_desc, sel_level)	\
	((key >> levels_desc[sel_level].offset) &		\
	 ((1UL << levels_desc[sel_level].bits) - 1))

#define	EXTRACT_LOWEST_OFFSET(index, key)				\
	EXTRACT_BIASED_VALUE(key, index->levels_desc, index->levels - 1)

/*
 * distribute "bits" amongst "levels" keeping the results
 */
static int generate_level_descriptions(struct cch_index *index,
	int levels,
	int bits,
	int root_bits,
	int low_bits)
{
	int each_base_size = bits / levels;
	int to_distribute = bits % levels;
	int i = 0;
	int next_level_offset = 0;
	int result = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	PRINT_INFO("each %d, to distribute -- %d\n",
		   each_base_size, to_distribute);

	index->levels_desc[index->levels - 1].bits = low_bits;
	index->levels_desc[index->levels - 1].size = 1UL << low_bits;
	index->levels_desc[index->levels - 1].offset = low_bits;
	index->lowest_level = index->levels - 1; /* staging */

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
		index->mid_level = i;
	}

	index->levels_desc[0].bits = root_bits;
	index->levels_desc[0].size = 1UL << root_bits;
	index->levels_desc[0].offset = next_level_offset;
	index->root_level = 0; /* staging */
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

	TRACE_ENTRY();
	new_index = kzalloc(sizeof(struct cch_index) +
			    (1 << root_bits) * sizeof(void *),
			    GFP_KERNEL);

	new_index->start_save_fn  = cch_index_start_save_fn;
	new_index->finish_save_fn = cch_index_finish_save_fn;
	new_index->entry_save_fn  = cch_index_save_fn;
	new_index->value_free_fn  = cch_index_value_free_fn;
	new_index->load_data_fn   = cch_index_load_data_fn;
	new_index->entry_load_fn  = cch_index_load_entry_fn;

	if (new_index == NULL) {
		PRINT_ERROR("vmalloc failed during index create");
		result = -ENOMEM;
		goto mem_failure;
	}

	/* root + levels + lowest level */
	new_index->levels = levels + 2;
	new_index->levels_desc = kzalloc(
		new_index->levels * sizeof(struct cch_level_desc_entry),
		GFP_KERNEL);

	if (new_index->levels_desc == NULL) {
		result = -ENOMEM;
		goto levels_desc_failure;
	}

	result = generate_level_descriptions(new_index, levels,
		bits, root_bits, low_bits);
	if (result) {
		PRINT_ERROR("error creating caches\n");
		goto mem_failure;
	}
#ifdef CCH_INDEX_DEBUG
	show_index_description(new_index);
#endif
	new_index->low_level_kmem = kmem_cache_create("cch_index_low_level",
		new_index->levels_desc[new_index->lowest_level].size
		* sizeof(new_index->head.v[0]) +
		sizeof(struct cch_index_entry),
		CCH_INDEX_LOW_LEVEL_ALIGN, 0, NULL);
	if (!new_index->low_level_kmem) {
		result = -ENOMEM;
		goto slab_failure;
	}
	PRINT_INFO("cch_index_low_level object size %d",
		   kmem_cache_size(new_index->low_level_kmem));

	new_index->mid_level_kmem = kmem_cache_create("cch_index_mid_level",
		new_index->levels_desc[new_index->mid_level].size
		* sizeof(new_index->head.v[0])
		+ sizeof(struct cch_index_entry),
		CCH_INDEX_MID_LEVEL_ALIGN, 0, NULL);

	if (!new_index->mid_level_kmem) {
		result = -ENOMEM;
		goto slab_failure;
	}
	PRINT_INFO("cch_index_mid_level object size %d",
		   kmem_cache_size(new_index->mid_level_kmem));
	/* FIXME goto's */
	result = 0;
	*out = new_index;
	goto success;
levels_desc_failure:
	kfree(new_index);
slab_failure:
mem_failure:
success:
	TRACE_EXIT();
	return result;
}
EXPORT_SYMBOL(cch_index_create);

static void cch_index_destroy_lowest_level_entry(struct cch_index *index,
	struct cch_index_entry *entry)
{
	int current_size = 0;
	int i = 0;
	TRACE_ENTRY();
	current_size = index->levels_desc[index->lowest_level].size;
	for (i = 0; i < current_size; i++) {
		if (entry->v[i].value != NULL) {
			entry->v[i].value = NULL;
			entry->ref_cnt--;
		}
	}
	PRINT_INFO("refcount is %d", entry->ref_cnt);
	sBUG_ON(entry->ref_cnt != 0);
	kmem_cache_free(index->low_level_kmem, entry);
	TRACE_EXIT();
}

static void cch_index_destroy_mid_level_entry(struct cch_index *index,
	struct cch_index_entry *entry)
{
	int current_size = 0, i = 0;
	TRACE_ENTRY();
	/* questionable, but works */
	BUG_ON(((u8 *) entry)[0] == POISON_FREE);
	current_size = index->levels_desc[index->mid_level].size;
	/* FIXME unloaded entries or shall we? */
	for (i = 0; i < current_size; i++) {
		/* FIXME if_loaded? */
		if (entry->v[i].entry == NULL)
			continue;
		/* how can an entry here be already free? */
		BUG_ON(((u8 *) entry->v[i].entry)[0] == POISON_FREE);

		if (cch_index_entry_is_lowest(entry->v[i].entry)) {
			PRINT_INFO("destroying lowest level 0x%x", i);
			cch_index_destroy_lowest_level_entry(index,
				entry->v[i].entry);
			entry->v[i].entry = NULL;
			entry->ref_cnt--;
		} else {
			PRINT_INFO("destroying mid level 0x%x", i);
			cch_index_destroy_mid_level_entry(index,
				entry->v[i].entry);
			entry->v[i].entry = NULL;
			entry->ref_cnt--;
		}
	}
	PRINT_INFO("refcount is %d", entry->ref_cnt);
	sBUG_ON(entry->ref_cnt != 0);
	kmem_cache_free(index->mid_level_kmem, entry);
	TRACE_EXIT();
}

static void cch_index_destroy_root_entry(struct cch_index *index)
{
	int current_size = 0;
	int i = 0;
	TRACE_ENTRY();
	current_size = index->levels_desc[index->root_level].size;
	for (i = 0; i < current_size; i++) {
		if (index->head.v[i].entry == NULL)
			continue;
		cch_index_destroy_mid_level_entry(index,
			index->head.v[i].entry);
		index->head.v[i].entry = NULL;
		index->head.ref_cnt--;
	}
	PRINT_INFO("remaning reference for root is %d", index->head.ref_cnt);
	BUG_ON(index->head.ref_cnt != 0);
	TRACE_EXIT();
}

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

static int cch_index_create_lowest_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset)
{
	int result = 0;
	int i = 0;
	TRACE_ENTRY();
	BUG_ON(index == NULL);
	BUG_ON(parent == NULL);
	*new_entry = kmem_cache_zalloc(index->low_level_kmem, GFP_KERNEL);
	if (!*new_entry) {
		PRINT_ERROR("low level alloc failure");
		result = -ENOSPC;
		goto mem_failure;
	}
#ifdef CCH_INDEX_DEBUG
	/* check real bounds of new object */
	for (i = 0; i < index->levels_desc[index->lowest_level].size; i++)
		sBUG_ON((*new_entry)->v[i].entry != 0);
#endif
	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent = (struct cch_index_entry *)
		(((unsigned long) parent) | ENTRY_LOWEST_ENTRY_BIT);
	TRACE_EXIT();
mem_failure:
	return result;
}

static int cch_index_create_mid_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset)
{
	int result = 0;
	int i = 0;
	TRACE_ENTRY();
	BUG_ON(index == NULL);
	BUG_ON(parent == NULL);
	*new_entry = kmem_cache_zalloc(index->mid_level_kmem, GFP_KERNEL);
	if (!*new_entry) {
		PRINT_ERROR("mid level alloc failure");
		result = -ENOSPC;
		goto mem_failure;
	}
#ifdef CCH_INDEX_DEBUG
	for (i = 0; i < index->levels_desc[index->mid_level].size; i++)
		sBUG_ON((*new_entry)->v[i].entry != 0);
#endif
	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent = parent;
	TRACE_EXIT();
mem_failure:
	return result;
}

/* traverse key parts through index creating required index entries */
static int cch_index_create_path(struct cch_index *index,
	uint64_t key,
	struct cch_index_entry **lowest_entry)
{
	int result = 0;
	struct cch_index_entry *new_entry;
	struct cch_index_entry *current_entry;
	int record_offset;
	int i = 0;
	/* current iteration of key traverse is next to lowest level of index */
	int next_to_lowest_level;
	TRACE_ENTRY();
	BUG_ON(index == NULL);
	current_entry = &index->head;

	for (i = 0; i < index->levels - 1; i++) {
		sBUG_ON(current_entry == NULL);
		next_to_lowest_level = i == index->levels - 2;

		record_offset = EXTRACT_BIASED_VALUE(key,
			index->levels_desc, i);
		PRINT_INFO("offset is 0x%x", record_offset);
		PRINT_INFO("value is %p",
			   current_entry->v[record_offset].value);

		if (current_entry->v[record_offset].entry == NULL) {
			if (unlikely(next_to_lowest_level)) {
				/* new entry will be at lowest level */
				result = cch_index_create_lowest_entry(
					index, current_entry,
					&new_entry, record_offset);
				if (result)
					goto creation_failure;
			} else {
				result = cch_index_create_mid_entry(
					index, current_entry,
					&new_entry, record_offset);
				if (result)
					goto creation_failure;
			}
			current_entry->ref_cnt++;

			PRINT_INFO("created new index entry at %p",
				current_entry);
		}
		current_entry = current_entry->v[record_offset].entry;
	}

	*lowest_entry = current_entry;

creation_failure:
	TRACE_EXIT();
	return result;
}

static int cch_index_traverse_lowest_entry(struct cch_index *index,
	uint64_t key,
	struct cch_index_entry **found_entry)
{
	int record_offset = 0;
	struct cch_index_entry *current_entry;
	int result = 0;
	int i = 0;
	int next_to_lowest_level = 0;

	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	current_entry = &index->head;

	for (i = 0; i < index->levels - 1; i++) {
		next_to_lowest_level = i == index->levels - 2;
		sBUG_ON(current_entry == NULL);

		record_offset = EXTRACT_BIASED_VALUE(key,
			index->levels_desc, i);
		PRINT_INFO("offset is 0x%x", record_offset);
		PRINT_INFO("value is %p",
			   current_entry->v[record_offset].value);

		if (current_entry->v[record_offset].entry != NULL) {
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

	/*
	 * FIXME cch_index_find_lowest_entry should be renamed to clarify
	 * that it (optionally) creates missing index entries on the way
	 * to lowest level
	 */
	result = cch_index_traverse_lowest_entry(index, key, &current_entry);
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
	PRINT_INFO("offset is 0x%x", lowest_offset);
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

#ifdef CCH_INDEX_DEBUG
	PRINT_INFO("key is 0x%.8llx", key);
	for (i = 0; i < index->levels; i++) {
		PRINT_INFO("part %d is 0x%.2llx", i,
			   EXTRACT_BIASED_VALUE(key, index->levels_desc, i));
	}
#endif

	result = cch_index_create_path(index, key, &current_entry);

	if (result)
		goto not_found;

	sBUG_ON(current_entry == NULL);

	record_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("computed offset is %d", record_offset);

	/* save value to index */
	current_entry->v[record_offset].value = value;

	current_entry->ref_cnt++;

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

	result = cch_index_traverse_lowest_entry(index, key, &current_entry);
	if (result)
		goto not_found;

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("removing at offset 0x%x", lowest_offset);
	current_entry->v[lowest_offset].value = NULL;
	/* FIXME lock */
	PRINT_INFO("refcnt was %d, become %d\n", current_entry->ref_cnt,
		   current_entry->ref_cnt - 1);
	current_entry->ref_cnt--;
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
	/* FIXME locking */
	sBUG_ON(!cch_index_entry_is_lowest(entry));
	entry->v[offset].value = NULL;
	entry->ref_cnt--;
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
