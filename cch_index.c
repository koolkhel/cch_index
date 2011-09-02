#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>

#define LOG_PREFIX "cch_index"

#include "cch_index.h"
#include "cch_index_debug.h"

#ifdef CCH_INDEX_DEBUG
static int trace_flag = TRACE_DEBUG;
#endif

static atomic_t _index_seq_n = ATOMIC_INIT(0);

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

	TRACE_EXIT_RES(result);
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
	int index_seq_n = 0;

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

	mutex_init(&new_index->cch_index_value_mutex);

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

	index_seq_n = atomic_inc_return(&_index_seq_n);

	snprintf(slab_name_buf, CACHE_NAME_BUF_SIZE,
		 "cch_index_low_level_%d", index_seq_n);
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
		 "cch_index_mid_level_%d", index_seq_n);
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
	TRACE_EXIT_RES(result);
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

/**
 * Frees records of lowest level entry, checks if reference
 * count is right, puts entry back to kmem_cache
 *
 * @arg entry entry to be freed
 */
void cch_index_destroy_lowest_level_entry(
	struct cch_index *index,
	struct cch_index_entry *entry)
{
	int current_size = 0;
	int i = 0;

	TRACE_ENTRY();

	// should we LOCK something?

	sBUG_ON(entry == NULL);
	sBUG_ON(POINTER_FREED(entry));
	sBUG_ON(!cch_index_entry_is_lowest(entry));

	current_size = cch_index_entry_size(index, entry);
	for (i = 0; i < current_size; i++) {
		if (entry->v[i].value != NULL) {
			entry->v[i].value = NULL;
			entry->ref_cnt--;
		}
	}
	TRACE(TRACE_DEBUG, "refcount is %d", entry->ref_cnt);
	sBUG_ON(entry->ref_cnt != 0);

	kmem_cache_free(index->low_level_kmem, entry);

	TRACE_EXIT();
	return;
}

/**
 * Call appropriate *_destroy_* function on records of entry,
 * decrease reference count for each freed entry,
 * check if it's zero at moment of destruction,
 * puts entry back to cache.
 *
 * @arg index
 * @arg entry mid-level entry to be freed
 * @arg level -- level of current entry for debug purposes,
 *      children entries get (level + 1)
 *
 */
void cch_index_destroy_mid_level_entry(struct cch_index *index,
	struct cch_index_entry *entry, int level)
{
	int current_size = 0, i = 0;

	TRACE_ENTRY();

	// should we LOCK anything?
	sBUG_ON(entry == NULL);
	sBUG_ON(POINTER_FREED(entry));

	TRACE(TRACE_DEBUG, "destroy mid level %p, level %d, references %d",
	      entry, level, entry->ref_cnt);

	if (!cch_index_entry_is_mid_level(entry)) {
		if (cch_index_entry_is_lowest(entry)) {
			PRINT_INFO("entry is lowest");
			sBUG();
		} else if (cch_index_entry_is_root(entry)) {
			PRINT_INFO("entry is root %p %p", entry, &index->head);
			sBUG();
		} else {
			PRINT_ERROR("unknown type of entry");
			sBUG();
		}
	}

	current_size = cch_index_entry_size(index, entry);
	/* FIXME unloaded entries or shall we? */
	for (i = 0; i < current_size; i++) {
		/* FIXME if_loaded? */
		if (entry->v[i].entry == NULL)
			continue;
		/* how can an entry here be already free? */
		sBUG_ON(POINTER_FREED(entry->v[i].entry));

		if (cch_index_entry_is_lowest(entry->v[i].entry)) {
			PRINT_INFO("destroying lowest level 0x%x", i);
			cch_index_destroy_lowest_level_entry(index,
				entry->v[i].entry);
		} else {
			PRINT_INFO("destroying mid level 0x%x", i);
			cch_index_destroy_mid_level_entry(index,
				entry->v[i].entry, level + 1);
		}
		entry->v[i].entry = NULL;
		entry->ref_cnt--;
	}

	PRINT_INFO("refcount is %d", entry->ref_cnt);
	sBUG_ON(entry->ref_cnt != 0);

	kmem_cache_free(index->mid_level_kmem, entry);

	TRACE_EXIT();
	return;
}

void cch_index_destroy_entry(
	struct cch_index *index,
	struct cch_index_entry *entry)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);

	if (cch_index_entry_is_lowest(entry))
		cch_index_destroy_lowest_level_entry(index, entry);
	else if (cch_index_entry_is_mid_level(entry))
		cch_index_destroy_mid_level_entry(index, entry, 1);
	else
		sBUG();

	/*
	 * we should also delete ourselves from parent, put NULL
	 * down there and decrease reference count
	 *
	 * Finding ourselves in parent table is O(size) for
	 * _every_ entry that is being removed (with appropriate 1/O(size)
	 * of chance to have ref_cnt == 0;
	 */

	TRACE_EXIT();
	return;
}

/**
 * Remove record from given entry at given offset,
 * decreasing reference count and, possibly, freeing all
 * unused index entries on this path.
 * 
 * Supposed to be called under cch_index_value_mutex.
 */
void __cch_index_entry_remove_value(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(entry));

	TRACE(TRACE_DEBUG, "removing at offset 0x%x", offset);
	entry->v[offset].value = NULL;
	/* FIXME lock */
	TRACE(TRACE_DEBUG, "refcnt was %d, become %d\n", entry->ref_cnt,
		   entry->ref_cnt - 1);
	entry->ref_cnt--;

	TRACE_EXIT();
	return;
}

/**
 * Destroy all linked records (mid_level), decrease reference count,
 * check if it's zero at the end. Can't free because it is
 * allocated inside cch_index.
 */
void cch_index_destroy_root_entry(struct cch_index *index)
{
	int current_size = 0;
	int i = 0;

	TRACE_ENTRY();

	current_size = cch_index_entry_size(index, &index->head);
	for (i = 0; i < current_size; i++) {
		if (index->head.v[i].entry == NULL)
			continue;
		cch_index_destroy_mid_level_entry(index,
			index->head.v[i].entry, 1);
		index->head.v[i].entry = NULL;
		index->head.ref_cnt--;
	}

	TRACE(TRACE_DEBUG, "remaning reference for root is %d",
	      index->head.ref_cnt);
	sBUG_ON(index->head.ref_cnt != 0);

	TRACE_EXIT();
	return;
}

/**
 * Create index entry of lowest level, attach it to parent,
 * update reference counts.
 *
 * When debugging, check for common memory allocation problems.
 *
 * @arg parent
 * @arg new_entr
 * @arg offset
 */
int cch_index_create_lowest_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset)
{
	int result = 0;
#ifdef CCH_INDEX_DEBUG
	int i = 0;
#endif

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(parent == NULL);

	*new_entry = kmem_cache_zalloc(index->low_level_kmem, GFP_KERNEL);
	if (!*new_entry) {
		PRINT_ERROR("low level alloc failure");
		result = -ENOMEM;
		goto out;
	}

	// LOCK parent, new_entry
	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent = (struct cch_index_entry *)
		(((unsigned long) parent) | ENTRY_LOWEST_ENTRY_BIT);
	(*new_entry)->parent_offset = offset;
	parent->ref_cnt++;
	// UNLOCK parent, new_entry

#ifdef CCH_INDEX_DEBUG
	/* check real bounds of new object */
	for (i = 0; i < cch_index_entry_size(index, *new_entry); i++)
		sBUG_ON((*new_entry)->v[i].entry != NULL);
	(*new_entry)->magic = CCH_INDEX_ENTRY_MAGIC;
#endif	

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Create index entry of mid level, updating parent and reference counts.
 *
 * @arg parent
 * @arg new_entry
 * @arg offset
 */
int cch_index_create_mid_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset)
{
	int result = 0;
#ifdef CCH_INDEX_DEBUG
	int i = 0;
#endif

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(parent == NULL);

	*new_entry = kmem_cache_zalloc(index->mid_level_kmem, GFP_KERNEL);
	if (!*new_entry) {
		PRINT_ERROR("mid level alloc failure");
		result = -ENOMEM;
		goto out;
	}

	parent->v[offset].entry = *new_entry;
	(*new_entry)->parent_offset = offset;
	(*new_entry)->parent = parent;
	parent->ref_cnt++;

#ifdef CCH_INDEX_DEBUG
	for (i = 0; i < cch_index_entry_size(index, *new_entry); i++)
		sBUG_ON((*new_entry)->v[i].entry != NULL);
	(*new_entry)->magic = CCH_INDEX_ENTRY_MAGIC;
#endif

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Decide whether to create mid or lowest level entry upon given
 * index level number.
 *
 * @arg parent parent index entry to attach new entry to
 * @arg level level number of new entry
 * @arg offset offset of new entry in parent v[] table.
 */
int cch_index_entry_create(
	struct cch_index *index,
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

	if (level == index->levels - 1) {
		/* level is lowest level */
		result = cch_index_create_lowest_entry(
			index, parent, new_entry, offset);
		if (result)
			goto out;
	} else {
		/* next level is mid level */
		result = cch_index_create_mid_entry(
			index, parent, new_entry, offset);
		if (result)
			goto out;
	}

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Create all index entries required for holding @arg key, returning
 * lowest entry of the path.
 * @arg key
 * @arg lowest_etnry
 */
int __cch_index_create_path(
	struct cch_index *index,
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

	sBUG_ON(index == NULL);
	sBUG_ON(lowest_entry == NULL);

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
			result = cch_index_entry_create(
				index, current_entry, &new_entry, i + 1,
				record_offset);
			if (result)
				goto out;

			PRINT_INFO("created new index entry at %p",
				current_entry);
		}
		current_entry = current_entry->v[record_offset].entry;
	}

	*lowest_entry = current_entry;

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Try to walk index using @arg key, returning lowest level entry,
 * if it's found, -ENOENT if not
 * 
 * Should be called under cch_index_value_mutex.
 *
 * @arg key
 * @arg found_entry
 */
int __cch_index_walk_path(
	struct cch_index *index,
	uint64_t key,
	struct cch_index_entry **found_entry)
{
	int record_offset = 0;
	struct cch_index_entry *current_entry;
	int result = 0;
	int i = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(found_entry == NULL);

	// LOCK head?
	current_entry = &index->head;
	/* all levels except last one */
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
			goto out;
		}
		// UNLOCK previous
		// LOCK new
	}

	*found_entry = current_entry;

	sBUG_ON(*found_entry == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(*found_entry));

out:
	TRACE_EXIT_RES(result);
	return result;
}



/**
 * Insert value to given lowest level entry at given offset,
 * replacing old value if required. Updates reference counter.
 *
 * @arg entry index entry to insert to
 * @arg offset insert to entry->v[offset]
 * @arg value value to insert
 */
int __cch_index_entry_insert_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset,
	bool replace,
	void *value)
{
	int result = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(entry));
	sBUG_ON(offset >= cch_index_entry_size(index, entry));
	sBUG_ON(offset < 0);

#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	TRACE(TRACE_DEBUG,
	      "inserting direct to %p at offset %d value %p, "
	      "current value %p",
	      entry, offset, value, entry->v[offset].value);

	if (entry->v[offset].value == NULL) {
		entry->ref_cnt++;
		entry->v[offset].value = value;
	} else if (replace) {
		/* no new value thus no ref_cnt */
		entry->v[offset].value = value;
	} else {
		result = -EEXIST;
		goto out;
	}

	TRACE(TRACE_DEBUG,
	      "result of insert is %p", entry->v[offset].value);

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Checks if entry should be removed by ref_cnt value,
 * check all parents for same problem
 *
 * @arg index
 * @arg entry
 */
void __cch_index_entry_cleanup(
	struct cch_index *index,
	struct cch_index_entry *entry)
{
	struct cch_index_entry *parent, *current_entry;
	int parent_entry_size;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);

	current_entry = entry;

	while (!cch_index_entry_is_root(current_entry)) {
		if (current_entry->ref_cnt != 0)
			goto done;
		parent = cch_index_entry_get_parent(current_entry);
		parent_entry_size = cch_index_entry_size(index, current_entry);

		TRACE(TRACE_DEBUG, "destroy entry %p "
		      "with parent %p, offset = %d",
		      current_entry, parent, current_entry->parent_index);
		parent->v[current_entry->parent_offset].entry = NULL;
		parent->ref_cnt--;
		cch_index_destroy_entry(index, current_entry);
		current_entry = parent;
	}

done:
	TRACE_EXIT();
	return;
}

/**
 * Search first index entry that is capable of holding
 * (i + 1)th branch started at @arg entry.
 *
 * Get parent of current entry, find current entry in parent,
 * see if (i + 1) is possible. If not, go up one level, if yes --
 * that's the result. Repeat till root entry which is capable
 * of holding 2^bits keys with low chances of overflow.
 *
 * Needed to extract this code as it is reusable for *_direct functions.
 *
 * Also, this function can not fail thus no return code.
 * When it fails, it's a programming bug.
 *
 * @arg index
 * @arg entry -- starting entry, always of lowest level
 * @arg subtree_root -- the result (any from parent of "entry" to root entry)
 * @arg offset -- offset of path from "entry" to "subtree_root" in
 *      subtree_root v[] table
 * @arg entry_level -- level of subtree_root, 0 -- root,
 *      index->levels -1 -- lowest
 * @arg direction -- for capabilities of backwards traversal,
 *      this arg specifies if we're searching for (i+1)-capable subtree_root,
 *      or (i-1) one.
 */
void __cch_index_climb_to_first_capable_parent(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **subtree_root,
	int *offset,
	int *entry_level,
	int direction)
{
	struct cch_index_entry *this_entry, *parent_entry;
	int this_entry_level;
	int parent_entry_size;
	int result = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(entry));
	sBUG_ON(entry_level == NULL);
	sBUG_ON(subtree_root == NULL);
	sBUG_ON(offset == NULL);
	sBUG_ON(entry_level == 0);

#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	this_entry = entry;
	parent_entry = cch_index_entry_get_parent(this_entry);
	/* this_entry is at lowest level */
	this_entry_level = index->levels - 1;

	while (!cch_index_entry_is_root(parent_entry)) {
		parent_entry_size = cch_index_entry_size(index, parent_entry);
		result = 0;
		
		/* i now holds this_entry offset in parent_entry */

		this_entry_level--;

		TRACE(TRACE_DEBUG, "climb i is %d and size is %d",
		      this_entry->parent_offset, parent_entry_size);
		if (this_entry->parent_offset + 1 < parent_entry_size)
			break; /* we can use that */

		this_entry = parent_entry;
		parent_entry = cch_index_entry_get_parent(this_entry);
	}

	sBUG_ON(cch_index_entry_is_root(this_entry) && (this_entry_level != 0));
	sBUG_ON(parent_entry->v[this_entry->parent_offset].entry != this_entry);

	*subtree_root = parent_entry;
	*entry_level = this_entry_level;
	*offset = this_entry->parent_offset;

	TRACE_EXIT();
	return;
}

/*
 * Find lowest level index entry that is next in key order to
 * given entry, creating one if required.
 *
 * @arg index
 * @arg entry -- lowest level index_entry which sibling we are looking
 *               for
 * @arg sibling -- found or created (with all levels) sibling
 */
int __cch_index_entry_create_next_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling)
{
	int result = 0;
	struct cch_index_entry *parent_entry, *this_entry;
	int this_entry_level = 0;
	int sibling_offset = 0;
	int i = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(sibling == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(entry));
#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	/*
	 * The function consists of two parts:
	 *
	 * 1. Find index_entry that is root point for (i+1) transition
	 *
	 * 2. Go down from that entry to lowest level, creating
	 *    entries on the way, so we create a lowest_level
	 *    entry that is a (i + 1) sibling to "entry" argument
	 */
	__cch_index_climb_to_first_capable_parent(
		index, entry, &parent_entry, &i, &this_entry_level, 1);

	sibling_offset = i + 1;
	TRACE(TRACE_DEBUG, "we can traverse down from %p at offset %d, "
	      "level %d", parent_entry, sibling_offset, this_entry_level);

	/* all of this complexity is to check if "i + 1" is safe */
	/* now in this_entry we have entry we should climb down
	 * at v[0] entries to get the right sibling
	 */
	while (this_entry_level < index->levels - 1) {
		this_entry = parent_entry->v[sibling_offset].entry;
		this_entry_level++;

		PRINT_INFO("this level is %d", this_entry_level);
		if (this_entry == NULL) {
			result = cch_index_entry_create(
				index, parent_entry, &this_entry,
				this_entry_level, sibling_offset);
			if (result) {
				PRINT_ERROR("couldn't create new entry while "
					"doing next_sibling search\n");
				goto out;
			}

			sBUG_ON(this_entry == NULL);
		}

		parent_entry = this_entry;
		sibling_offset = 0;
	};

	*sibling = this_entry;

	/* this function did effectively nothing if this bug happens */
	sBUG_ON(*sibling == entry);
	/* result should be same level as input -- lowest one */
	sBUG_ON(!cch_index_entry_is_lowest(*sibling));

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Find next (in key order) sibling to given entry, if there is one.
 *
 * The difference with previous (..._create_next_sibling)
 * is that this function doesn't create any new index entries, so
 * it is suitable for search.
 */
int __cch_index_entry_find_next_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling)
{
	int result = 0;
	struct cch_index_entry *parent_entry, *this_entry = NULL;
	int this_entry_level = 0;
	int sibling_offset = 0;
	int i = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(sibling == NULL);
	sBUG_ON(!cch_index_entry_is_lowest(entry));

	/*
	 * The function consists of two parts:
	 *
	 * 1. Find index_entry that is root point for (i+1) transition
	 *
	 * 2. Go down from that entry to lowest level, creating
	 *    entries on the way, so we create a lowest_level
	 *    entry that is a (i + 1) sibling to "entry" argument
	 */
	__cch_index_climb_to_first_capable_parent(
		index, entry, &parent_entry, &i, &this_entry_level, 1);

	TRACE(TRACE_DEBUG,
	      "we can traverse down from %p at offset %d, level %d",
	      parent_entry, i + 1, this_entry_level);

	/* all of this complexity is to check if "i + 1" is safe */
	sibling_offset = i + 1;
	/* now in this_entry we have entry we should climb down
	 * at v[0] entries to get th e right sibling
	 */
	while (this_entry_level < index->levels - 1) {
		this_entry = parent_entry->v[sibling_offset].entry;
		this_entry_level++;

		PRINT_INFO("this level is %d", this_entry_level);
		if (this_entry == NULL) {
			result = -ENOENT;
			goto out;
		}

		parent_entry = this_entry;
		sibling_offset = 0;
	};

	*sibling = this_entry;
	result = 0;

out:
	TRACE_EXIT_RES(result);
	return result;
}

/**
 * Find lowest level index entry that is previous in key order
 * to given entry, creating one if required
 * @arg
 */
int __cch_index_entry_find_prev_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling)
{
	int result = 0;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	sBUG_ON(*sibling == NULL);
#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_remove_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset)
{
	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	/* FIXME locking */
	sBUG_ON(!cch_index_entry_is_lowest(entry));

	mutex_lock(&index->cch_index_value_mutex);

	/*
	 * doesn't seem like we should leap to next entry
	 * on offset overflow. Or should we?
	 */
	__cch_index_entry_remove_value(index, entry, offset);

	mutex_unlock(&index->cch_index_value_mutex);

	TRACE_EXIT();
	return 0;
}
EXPORT_SYMBOL(cch_index_remove_direct);

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

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	/* we can insert entry only to lowest entry */
	sBUG_ON(!cch_index_entry_is_lowest(entry));

#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	mutex_lock(&index->cch_index_value_mutex);
	
	right_entry = entry;
	lowest_entry_size = cch_index_entry_size(index, entry);

	TRACE(TRACE_DEBUG, "insert_direct: offset: %d, size: %d\n",
	      offset, lowest_entry_size);

	/* offset overleaps to next index entry */
	if (unlikely(offset >= lowest_entry_size)) {
		/* forward traversing */
		/* need to find next sibling */
		/* but offset shouldn't leap over one index_entry
		 * as we are searching for next sibling, not for the
		 * arbitrary indexed sibling */
		sBUG_ON(offset >= 2 * lowest_entry_size);
		result = __cch_index_entry_create_next_sibling(
			index, entry, &right_entry);
		if (result)
			goto out_unlock;

		/* we should be sure the search wasn't in vain */
		sBUG_ON(right_entry == entry);
		offset -= lowest_entry_size;

		/* offset overleaps to previous index entry */
	} else if (unlikely(offset < 0)) {
		/* we support backwards traversing */
		/* need to find previous sibling */
		sBUG(); /* not implemented yet */
		result = __cch_index_entry_find_prev_sibling(
			index, entry, &right_entry);
		if (result)
			goto out_unlock;
	}
	/* we can insert right in this entry */
	result = __cch_index_entry_insert_direct(index, right_entry, offset,
		replace, value);
	if (result) {
		if (result == -EEXIST)
			PRINT_INFO("attempt to replace entry");
		goto out_unlock;
	}

	if (new_value_offset)
		*new_value_offset = offset;
	if (new_index_entry)
		*new_index_entry = right_entry;

out_unlock:
	mutex_unlock(&index->cch_index_value_mutex);

	TRACE_EXIT_RES(result);
	return result;
}
EXPORT_SYMBOL(cch_index_insert_direct);

int cch_index_find_direct(
	struct cch_index *index,
	struct cch_index_entry *entry, int offset,
	void **out_value,
	struct cch_index_entry **next_index_entry,
	int *value_offset)
{
	int result = 0;
	int lowest_entry_size = 0;
	struct cch_index_entry *right_entry = NULL;

	TRACE_ENTRY();

	sBUG_ON(index == NULL);
	sBUG_ON(entry == NULL);
	/* starting point is lowest entry */
	sBUG_ON(!cch_index_entry_is_lowest(entry));
	sBUG_ON(out_value == NULL);
	sBUG_ON(*out_value == NULL);
#ifdef CCH_INDEX_DEBUG
	sBUG_ON(entry->magic != CCH_INDEX_ENTRY_MAGIC);
#endif

	mutex_lock(&index->cch_index_value_mutex);

	/* logic is same as in insert_direct, but we must not create
	 * any siblings as we do in insert_direct:
	 *
	 * 1. check if we can't find given offset in current entry */

	/* 2. if not, seek for index_entry that can hold remainder of offset */

	/* 3. with known entry check if it holds value and return it */

	right_entry = entry;
	lowest_entry_size = cch_index_entry_size(index, entry);
	TRACE(TRACE_DEBUG, "reading entry %p sized %d with offset %d",
	      entry, lowest_entry_size, offset);

	/* offset overleaps to next index entry */
	if (unlikely(offset >= lowest_entry_size)) {
		/* forward traversing */
		/* need to find next sibling */
		/* but offset shouldn't leap over one index_entry
		 * as we are searching for next sibling, not for the
		 * arbitrary indexed sibling */
		sBUG_ON(offset >= 2 * lowest_entry_size);
		result = __cch_index_entry_find_next_sibling(
			index, entry, &right_entry);
		if (result)
			goto out_unlock;

		/* we should be sure the search wasn't in vain */
		sBUG_ON(right_entry == entry);
		offset -= lowest_entry_size;

		/* offset overleaps to previous index entry */
	} else if (unlikely(offset < 0)) {
		/* we support backwards traversing */
		/* need to find previous sibling */
		sBUG(); /* not implemented yet */
		result = __cch_index_entry_find_prev_sibling(
			index, entry, &right_entry);
		if (result)
			goto out_unlock;
	}

	/* now, find */

	*out_value = right_entry->v[offset].value;
	
	if (out_value)
		cch_index_value_lock(*out_value);
	else
		result = -ENOENT;

	if (value_offset)
		*value_offset = offset;
	if (next_index_entry)
		*next_index_entry = right_entry;

out_unlock:
	mutex_unlock(&index->cch_index_value_mutex);

	TRACE_EXIT_RES(result);
	return result;
}
EXPORT_SYMBOL(cch_index_find_direct);


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

	mutex_lock(&index->cch_index_value_mutex);

	result = __cch_index_walk_path(index, key, &current_entry);
	if (result) {
		*out_value = 0;
		if (index_entry)
			*index_entry = NULL;
		if (value_offset)
			*value_offset = 0;
		result = -ENOENT;
		goto out_unlock;
	}

	sBUG_ON(current_entry == NULL);

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("offset is 0x%x", lowest_offset);
	*out_value = current_entry->v[lowest_offset].value;

	if (out_value)
		cch_index_value_lock(*out_value);
	else
		result = -ENOENT;

	if (index_entry)
		*index_entry = current_entry;
	if (value_offset)
		*value_offset = lowest_offset;

	PRINT_INFO("found 0x%lx", (long int) *out_value);
	result = (*out_value == NULL) ? -ENOENT : 0;

out_unlock:
	mutex_unlock(&index->cch_index_value_mutex);

	TRACE_EXIT_RES(result);
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

	mutex_lock(&index->cch_index_value_mutex);	

#ifdef CCH_INDEX_DEBUG
	PRINT_INFO("key is 0x%.8llx", key);
	for (i = 0; i < index->levels; i++) {
		PRINT_INFO("part %d is 0x%.2llx", i,
			   EXTRACT_BIASED_VALUE(key, index->levels_desc, i));
	}
#endif

	result = __cch_index_create_path(index, key, &current_entry);

	if (result)
		goto out_unlock;

	sBUG_ON(current_entry == NULL);

	record_offset = EXTRACT_LOWEST_OFFSET(index, key);
	PRINT_INFO("computed offset is %d", record_offset);

	/* save value to index */
	result = __cch_index_entry_insert_direct(
		index, current_entry, record_offset, replace, value);
	if (result)
		goto out_unlock;

	if (new_value_offset)
		*new_value_offset = record_offset;
	if (new_index_entry)
		*new_index_entry = current_entry;

out_unlock:
	mutex_unlock(&index->cch_index_value_mutex);	
	
	TRACE_EXIT_RES(result);
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

	mutex_lock(&index->cch_index_value_mutex);

	result = __cch_index_walk_path(index, key, &current_entry);
	if (result)
		goto out_unlock;

	lowest_offset = EXTRACT_LOWEST_OFFSET(index, key);

	__cch_index_entry_remove_value(index, current_entry, lowest_offset);

	/* and now we check if some entries should be cleaned up */
	__cch_index_entry_cleanup(index, current_entry);

out_unlock:
	mutex_unlock(&index->cch_index_value_mutex);
	
	TRACE_EXIT_RES(result);
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
