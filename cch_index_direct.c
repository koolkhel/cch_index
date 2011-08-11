#include <linux/module.h>

#define LOG_PREFIX "cch_index_direct"

#include "cch_index.h"
#include "cch_index_common.h"
#include "cch_index_debug.h"
#include "cch_index_direct.h"

static int trace_flag = TRACE_DEBUG;

int cch_index_remove_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset)
{
	TRACE_ENTRY();

	sBUG_ON(entry == NULL);
	/* FIXME locking */
	sBUG_ON(!cch_index_entry_is_lowest(entry));

	/*
	 * doesn't seem like we should leap to next entry
	 * on offset overflow
	 */
	cch_index_entry_remove_value(index, entry, offset);

	TRACE_EXIT();

	return -ENOENT;
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
			goto failure;

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
			goto failure;
	}
	/* we can insert right in this entry */
	result = cch_index_entry_insert_direct(index, right_entry, offset,
		replace, value);
	if (result) {
		if (result == -EEXIST)
			PRINT_INFO("attempt to replace entry");
		goto failure;
	}
	if (new_value_offset)
		*new_value_offset = offset;
	if (new_index_entry)
		*new_index_entry = right_entry;

failure:
	TRACE_EXIT();

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
			goto failure;

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
			goto failure;
	}

	/* now, find */

	*out_value = right_entry->v[offset].value;

	if (out_value == NULL)
		result = -ENOENT;
	if (value_offset)
		*value_offset = offset;
	if (next_index_entry)
		*next_index_entry = right_entry;

failure:
	TRACE_EXIT();

	return result;
}
EXPORT_SYMBOL(cch_index_find_direct);

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
	int i = 0;
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
		/* find this_entry in parent_entry's v[] at position "i" */
		for (i = 0; i < parent_entry_size; i++) {
			/* 1/size probability of success */
			if (unlikely(parent_entry->v[i].entry == this_entry)) {
				/* ok, we found it, fall out of "for" */
				result = 0;
				break;
			}
		}

		if (unlikely(result == -ENOENT)) {
			/* index corruption here */
			PRINT_ERROR("find_next_sibling: couldn't find entry %p "
				"in its parent table: %p",
				this_entry, parent_entry);
			goto entry_not_found;
		}
		/* i now holds this_entry offset in parent_entry */

		this_entry_level--;

		TRACE(TRACE_DEBUG, "climb i is %d and size is %d",
		      i, parent_entry_size);
		if (i + 1 < parent_entry_size)
			break; /* we can use that */

		this_entry = parent_entry;
		parent_entry = cch_index_entry_get_parent(this_entry);
	}

	sBUG_ON(cch_index_entry_is_root(this_entry) && (this_entry_level != 0));
	sBUG_ON(parent_entry->v[i].entry != this_entry);

	*subtree_root = parent_entry;
	*entry_level = this_entry_level;
	*offset = i;

	TRACE_EXIT();

	return;
entry_not_found:
	/*
	 * We did not find it, which is error (index corruption),
	 * parents should always be connected with their children
	 */
	sBUG();
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
				goto create_entry_failure;
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

create_entry_failure:
	TRACE_EXIT();

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
	 * at v[0] entries to get the right sibling
	 */
	while (this_entry_level < index->levels - 1) {
		this_entry = parent_entry->v[sibling_offset].entry;
		this_entry_level++;

		PRINT_INFO("this level is %d", this_entry_level);
		if (this_entry == NULL) {
			result = -ENOENT;
			goto not_found;
		}

		parent_entry = this_entry;
		sibling_offset = 0;
	};

	*sibling = this_entry;
	result = 0;
not_found:
	TRACE_EXIT();

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

	TRACE_EXIT();

	return result;
}
