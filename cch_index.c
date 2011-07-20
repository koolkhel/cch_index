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

/* 
 * insert into found entry with offset inside this entry,
 * should not be called by external API thus "__" in the name
 */
static int __cch_index_entry_insert_direct(
	struct cch_index_entry *entry,
	int offset,
	bool replace,
	void *value)
{
	int result = 0;
	TRACE_ENTRY();
	if (entry->v[offset].value != NULL) {
		if (replace) {
			entry->v[offset].value = value;
			result = 0;
		} else {
			result = -EEXIST;
			goto failure;
		}
	}
	entry->ref_cnt++;
failure:
	TRACE_EXIT();
	return result;
}

/*
 * get number of elements this entry could hold. Practically,
 * size of v[] array.
 */
static inline int __cch_index_entry_size(struct cch_index *index,
	struct cch_index_entry *entry)
{
	int size;
	TRACE_ENTRY();
	if (cch_index_entry_is_lowest(entry)) {
		size = index->levels_desc[index->lowest_level].size;
	} else if (cch_index_entry_is_root(entry)) {
		size = index->levels_desc[index->root_level].size;
	} else {
		size = index->levels_desc[index->mid_level].size;
	}
	TRACE_EXIT();
	return size;
}

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
	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent = (struct cch_index_entry *)
		(((unsigned long) parent) | ENTRY_LOWEST_ENTRY_BIT);
	parent->ref_cnt++;
#ifdef CCH_INDEX_DEBUG
	/* check real bounds of new object */
	for (i = 0; i < __cch_index_entry_size(index, *new_entry); i++)
		sBUG_ON((*new_entry)->v[i].entry != 0);
#endif

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
	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent = parent;
	parent->ref_cnt++;

#ifdef CCH_INDEX_DEBUG
	for (i = 0; i < __cch_index_entry_size(index, *new_entry); i++)
		sBUG_ON((*new_entry)->v[i].entry != 0);
#endif
	TRACE_EXIT();
mem_failure:
	return result;
}

/* 
 * decides whether to create mid or lowest level entry upon given
 * level number
 */
static inline int __cch_index_entry_create(struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int level,
	int offset)
{
	int result;
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	sBUG_ON(parent == NULL);
	sBUG_ON(new_entry == NULL);
	if (level == index->levels - 2) {
		/* next level is lowest level */
		result = cch_index_create_lowest_entry(
			index, parent, new_entry, offset);
		if (result)
			goto failure;
	} else {
		/* next level is mid level */
		result = cch_index_create_mid_entry(
			index, parent, new_entry, offset);
		if (result)
			goto failure;
	}
	result = 0;
failure:
	TRACE_EXIT();
	return result;
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
			result = __cch_index_entry_create(
				index, current_entry, &new_entry, i,
				record_offset);
			if (result)
				goto creation_failure;

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

	result = cch_index_traverse_lowest_entry(index, key, &current_entry);
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
	/* step 1. Fast check if we could find entry right in given entry */
	/* step 2. If not, find the right entry to search */
	/* and search */
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
	__cch_index_entry_insert_direct(current_entry,
		record_offset, replace, value);

	if (new_value_offset)
		*new_value_offset = record_offset;
	if (new_index_entry)
		*new_index_entry = current_entry;
	result = 0;
not_found:
	return result;
}
EXPORT_SYMBOL(cch_index_insert);

/* 
 * Find lowest level index entry that is next in key order to
 * given entry, creating one if required.
 * 
 * FIXME find_or_create_next_sibling or whatever
 */
static int __cch_index_entry_find_next_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling)
{
	int result = 0;
	struct cch_index_entry *parent_entry, *this_entry;
	int this_entry_level = 0;
	int parent_entry_size = 0;
	int sibling_offset = 0;
	int i = 0;
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(sibling == NULL);
	/* 
	 * idea is as follows:
	 * Return to parent node
	 * Find this entry in parent v[] table
	 * See if there is a next record
	 * If yes, return it into *sibling
	 * If not, allocate one and return it
	 * If there is no space to allocate it in this index_entry,
	 * try to do so in his parent
	 */
	parent_entry = cch_index_entry_get_parent(entry);
	this_entry = entry;
	this_entry_level = index->levels - 1;
	/* incorrect. We could return to parents more than once */
	while (!cch_index_entry_is_root(parent_entry)) {
		parent_entry_size = __cch_index_entry_size(index, entry);
		for (i = 0; i < parent_entry_size; i++) {
			/* 1/size probability of success */
			if (unlikely(parent_entry->v[i].entry ==
				this_entry)) {
				/* ok, we found it, fall out of "for" */
				break;
			}

			goto entry_not_found;
		}
		/* i now holds this_entry offset in parent_entry */
		if (i + 1 >= parent_entry_size) {
			this_entry = parent_entry;
			parent_entry = cch_index_entry_get_parent(entry);
			this_entry_level--;
		} else
			break; /* we can use sibling of this entry */
	}
	/* all of this complexity is to check if "i + 1" is safe */
	sibling_offset = i + 1;
	this_entry = parent_entry->v[sibling_offset].entry;
	/* now in this_entry we have entry we should climb down
	 * at v[0] entries to get the right sibling
	 */
	do {
		if (this_entry == NULL) {
			result = __cch_index_entry_create(
				index, parent_entry, &this_entry,
				this_entry_level, sibling_offset);
			if (result)
				goto failure;
		}
		parent_entry = this_entry;
		this_entry = this_entry->v[0].entry;
		this_entry_level++;
		sibling_offset = 0;
	} while (!cch_index_entry_is_lowest(this_entry));
	*sibling = this_entry;
	result = 0;
	goto done;
failure:
done:
		
	TRACE_EXIT();
	return result;
entry_not_found:
	/* 
	 * We did not find it, which is error, parents should always
	 * be connected with their children
	 */
	sBUG();
	
}

/* 
 * Find lowest level index entry that is previous in key order
 * to given entry, creating one if required
 */
static int __cch_index_entry_find_prev_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling)
{
	int result = 0;
	TRACE_ENTRY();
	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(*sibling == NULL);
	TRACE_EXIT();
	return result;
}

int cch_index_insert_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset,
	bool replace,
	void *value,
	struct cch_index_entry **new_index_entry,
	int *new_value_offset)
{
	int result = 0;
	int lowest_entry_size = 0;
	struct cch_index_entry *right_entry = NULL;
	TRACE_ENTRY();
	sBUG_ON(entry == NULL);
	/* we can insert entry only to lowest entry */
	sBUG_ON(!cch_index_entry_is_lowest(entry));
	right_entry = entry;
	lowest_entry_size = __cch_index_entry_size(index, entry);

/* offset overleaps to next index entry */
	if (unlikely(offset > lowest_entry_size)) {
		/* forward traversing */
		/* need to find next sibling */
		/* but offset shouldn't leap over one index_entry 
		 * as we are searching for next sibling, not for the
		 * arbitrary indexed sibling */
		sBUG_ON(offset > 2 * lowest_entry_size);
		result = __cch_index_entry_find_next_sibling(
			index, entry, &right_entry);
		if (result)
			goto failure;
		offset -= lowest_entry_size;

/* offset overleaps to previous index entry */
	} else if (unlikely(offset < 0)) {
		/* we support backwards traversing */
		/* need to find previous sibling */
		sBUG(); /* not implemented yet */
		result = __cch_index_entry_find_prev_sibling(
			index, entry, &right_entry);
		if (result)
			goto failure;
	}
	/* we can insert right in this entry */
	result = __cch_index_entry_insert_direct(right_entry, offset,
		replace, value);
	if (result)
		goto failure;
	
failure:
	TRACE_EXIT();
	return result;
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
