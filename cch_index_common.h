#ifndef CCH_INDEX_COMMON_H
#define CCH_INDEX_COMMON_H

#include "cch_index.h"

void cch_index_entry_cleanup(
	struct cch_index *index,
	struct cch_index_entry *entry);

void cch_index_entry_remove_value(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset);

int cch_index_entry_insert_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset,
	bool replace,
	void *value);

void cch_index_destroy_lowest_level_entry(
	struct cch_index *index,
	struct cch_index_entry *entry);

void cch_index_destroy_mid_level_entry(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int level);

void cch_index_destroy_root_entry(struct cch_index *index);

void cch_index_destroy_entry(
	struct cch_index *index,
	struct cch_index_entry *entry);

int cch_index_create_path(
	struct cch_index *index,
	uint64_t key,
	struct cch_index_entry **lowest_entry);

int cch_index_walk_path(
	struct cch_index *index,
	uint64_t key,
	struct cch_index_entry **found_entry);

int cch_index_create_lowest_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset);

int cch_index_create_mid_entry(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int offset);

int cch_index_entry_create(
	struct cch_index *index,
	struct cch_index_entry *parent,
	struct cch_index_entry **new_entry,
	int level,
	int offset);

#endif /* CCH_INDEX_COMMON_H */
