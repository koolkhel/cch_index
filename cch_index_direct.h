#ifndef CCH_INDEX_DIRECT_H
#define CCH_INDEX_DIRECT_H

void __cch_index_climb_to_first_capable_parent(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **subtree_root,
	int *offset,
	int *entry_level,
	int direction);

int __cch_index_entry_create_next_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling);

int __cch_index_entry_find_next_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling);

int __cch_index_entry_find_prev_sibling(
	struct cch_index *index,
	struct cch_index_entry *entry,
	struct cch_index_entry **sibling);

#endif /* CCH_INDEX_DIRECT_H */
