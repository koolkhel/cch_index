#include "linux/slab.h"

#define LOG_PREFIX "cch_index_common"

#include "cch_index.h"
#include "cch_index_common.h"
#include "cch_index_debug.h"

#ifdef CCH_INDEX_DEBUG
static int trace_flag = TRACE_DEBUG;
#endif

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
 */
void cch_index_entry_remove_value(
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
 * Create all index entries required for holding @arg key, returning
 * lowest entry of the path.
 * @arg key
 * @arg lowest_etnry
 */
int cch_index_create_path(
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
 * @arg key
 * @arg found_entry
 */
int cch_index_walk_path(
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
 * Insert value to given lowest level entry at given offset,
 * replacing old value if required. Updates reference counter.
 *
 * @arg entry index entry to insert to
 * @arg offset insert to entry->v[offset]
 * @arg value value to insert
 */
int cch_index_entry_insert_direct(
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
void cch_index_entry_cleanup(
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
