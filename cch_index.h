#ifndef RELDATA_INDEX_H
#define RELDATA_INDEX_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

/* alignment for kmem_cache */
#define CCH_INDEX_LOW_LEVEL_ALIGN 8
#define CCH_INDEX_MID_LEVEL_ALIGN 8

typedef void (*cch_index_start_save_t)(void);
typedef void (*cch_index_finish_save_t)(void);
typedef void (*cch_index_entry_save_t)(void);
typedef void (*cch_index_value_free_t)(void);
typedef void (*cch_index_load_data_t)(void);
typedef void (*cch_index_entry_load_t)(void);

void cch_index_start_save_fn(void);
void cch_index_finish_save_fn(void);
void cch_index_entry_save_fn(void);
void cch_index_value_free_fn(void);
void cch_index_load_data_fn(void);
void cch_index_load_entry_fn(void);

#define CCH_INDEX_ENTRY_MAGIC 0x117700FF

struct cch_index_entry {
	#ifdef CCH_INDEX_DEBUG
	int magic;
	#endif
	/* how many entries inside / how many children entries */
	int ref_cnt;
	/* NULL for root,
	   bit "0" for lowest_entry
	   bit "1" for locked flag
	   bit "2" for saved flag
	 */
	struct cch_index_entry *parent;
	struct list_head index_lru_list_entry;

	/* index of this entry in parent->v[] table for
	 * leaf-to-root traversal */
	int parent_offset;

	union {
		uint64_t backend_dev_offs;
		struct cch_index_entry *entry;
		void *value;
	} v[];
};

/*
 * Description of a level in multi-level index
 */
struct cch_level_desc_entry {
	/* this many records in this level index entry */
	int size;

	/* addressed by this many bits */
	int bits;

	/* biased to previous record with this offset */
	int offset;
};

struct cch_index {
	struct mutex cch_index_value_mutex;

	spinlock_t index_lru_list_lock;
	struct list_head index_lru_list;

	/* total number of levels -- levels + 1 for root + 1 for lowest */
	int levels;

	/* we need this for determining entry size. These are
	 * indices of levels in levels_dec */
	int root_level;
	int lowest_level;
	int mid_level;

	/* size of each kmem_cache_zalloc */
	int lowest_level_entry_size;
	int mid_level_entry_size;

	/* array, describing each level of index */
	struct cch_level_desc_entry *levels_desc;

	struct kmem_cache *mid_level_kmem;
	struct kmem_cache *lowest_level_kmem;

	atomic_t total_bytes;

	cch_index_start_save_t start_save_fn;
	cch_index_finish_save_t finish_save_fn;
	cch_index_entry_save_t entry_save_fn;
	cch_index_value_free_t value_free_fn;
	cch_index_load_data_t load_data_fn;
	cch_index_entry_load_t entry_load_fn;

	/* Must be last element as it is direction of growing */
	struct cch_index_entry head;
};

/**
 * extract part of key that describes i-th level of index,
 * it can be used as offset of v[] table of index entry
 */
#define EXTRACT_BIASED_VALUE(key, levels_description, i)	\
	((key >> levels_description[i].offset) &		\
	 ((1UL << levels_description[i].bits) - 1))

#define	EXTRACT_LOWEST_OFFSET(index, key)				\
	EXTRACT_BIASED_VALUE(key, index->levels_desc, index->levels - 1)


extern int cch_index_check_lock(void *value);
extern int cch_index_value_lock(void *value);
extern int cch_index_value_unlock(void *value);
extern void cch_index_on_new_entry_alloc(
	struct cch_index *index, int inc_size, int new_size);
extern void cch_index_on_entry_free(
	struct cch_index *index, int dec_size, int new_size);
extern void cch_index_alloc_new_cluster(void);
extern void cch_index_free_cluster(void);

#define ENTRY_LOWEST_ENTRY_BIT (1UL << 0)
#define ENTRY_LOCKED_BIT (1UL << 1)
#define ENTRY_SAVED_BIT (1UL << 2)

static inline void cch_index_entry_lru_update(
	struct cch_index *index,
	struct cch_index_entry *entry)
{
	unsigned long flags;

	/* update LRU */
	spin_lock_irqsave(&(index->index_lru_list_lock), flags);
	list_move_tail(&(entry->index_lru_list_entry),
		       &(index->index_lru_list));
	spin_unlock_irqrestore(&(index->index_lru_list_lock), flags);
}

/* remove entry from LRU list. Required on entry removal */
static inline void cch_index_entry_lru_remove(
	struct cch_index *index,
	struct cch_index_entry *entry)
{
	unsigned long flags;

	spin_lock_irqsave(&(index->index_lru_list_lock), flags);
	list_del(&(entry->index_lru_list_entry));
	spin_unlock_irqrestore(&(index->index_lru_list_lock), flags);
}

static inline int cch_index_entry_is_root(struct cch_index_entry *entry)
{
	return entry->parent == NULL;
}

/* entry contains actual values */
static inline int cch_index_entry_is_lowest_level(struct cch_index_entry *entry)
{
	return (int) ((unsigned long) entry->parent) & ENTRY_LOWEST_ENTRY_BIT;
}

static inline int cch_index_entry_is_mid_level(struct cch_index_entry *entry)
{
	return !(cch_index_entry_is_root(entry) ||
		 cch_index_entry_is_lowest_level(entry));
}

/* locked for load/unload */
static inline int cch_index_entry_is_locked(struct cch_index_entry *entry)
{
	return (int) ((unsigned long) entry->parent) & ENTRY_LOCKED_BIT;
}

static inline int cch_index_entry_is_saved(struct cch_index_entry *entry)
{
	return (int) ((unsigned long) entry->parent) & ENTRY_SAVED_BIT;
}

static inline void cch_index_entry_set_locked(struct cch_index_entry *entry)
{
	entry->parent = (struct cch_index_entry *)
		(((unsigned long) entry->parent) | ENTRY_LOCKED_BIT);
}

static inline void cch_index_entry_set_saved(struct cch_index_entry *entry)
{
	entry->parent = (struct cch_index_entry *)
		(((unsigned long) entry->parent) | ENTRY_SAVED_BIT);
}

static inline void cch_index_entry_clear_locked(struct cch_index_entry *entry)
{
	entry->parent = (struct cch_index_entry *)
		(((unsigned long) entry->parent) & ~ENTRY_LOCKED_BIT);
}

static inline void cch_index_entry_clear_saved(struct cch_index_entry *entry)
{
	entry->parent = (struct cch_index_entry *)
		(((unsigned long) entry->parent) & ~ENTRY_SAVED_BIT);
}

static inline struct cch_index_entry
*cch_index_entry_get_parent(struct cch_index_entry *entry)
{
	/* ENTRY_SAVED_BIT is last in order */
	return (struct cch_index_entry *)
		(((unsigned long) entry->parent) & ~(ENTRY_SAVED_BIT - 1));
}

/* is entry unloaded to backing store */
static inline int cch_index_entry_is_unloaded(struct cch_index_entry *entry)
{
	/* lowest bit is "unloaded flag" */
	return (int) (((unsigned long) entry) & 0x1);
}

/* create and load */

/*
 * get number of elements this entry could hold. Practically,
 * size (in records) of v[] array.
 */
static inline int cch_index_entry_size(struct cch_index *index,
	struct cch_index_entry *entry)
{
	int size;
	if (cch_index_entry_is_lowest_level(entry))
		size = index->levels_desc[index->lowest_level].size;
	else if (cch_index_entry_is_root(entry))
		size = index->levels_desc[index->root_level].size;
	else
		size = index->levels_desc[index->mid_level].size;
	return size;
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
	struct cch_index **out);

/* destroy, deallocate, don't allow used index*/
void cch_index_destroy(struct cch_index *index);

/* save to disk, return offset */
uint64_t cch_index_save(struct cch_index *index);

/* load from disk using "start" offset, return error code */
int cch_index_load(struct cch_index *index, uint64_t start);

/* search */

/*
 * Search on key. Found result to out_value,
 * save index entry for sibling access, value_offset of record
 * inside that index entry
 */
int cch_index_find(struct cch_index *index, uint64_t key,
		   void **out_value, struct cch_index_entry **index_entry,
		   int *value_offset);

/*
 * This function searches for a value in the index using offset,
 * and returns 0 for success or a negative error code for failure.
 * On success, the target value for the key is returned in the
 * *out value pointer.
 *
 * It also returns a pointer to the index entry for the target value
 * into **next index entry location and offset to the value in the
 * index entry into *value offset location. Those values can be used
 * later for subsequent calls to cch_index_find_direct(). Next index
 * entry and/or value offset can be NULL.
 */
int cch_index_find_direct(
	struct cch_index *index,
	struct cch_index_entry *entry, int offset,
	void **out_value,
	struct cch_index_entry **next_index_entry,
	int *value_offset);

/* insertion */

int cch_index_insert(struct cch_index *index,
		     uint64_t key,  /* key of new record */
		     void *value,   /* value of new record */
		     bool replace,  /* should replace record under same key
				     * If not -- -EEXIST    */
		     /* created record */

		     struct cch_index_entry **new_index_entry,
		     int *new_value_offset);  /* created offset */

/* insert to given entry with given offset.
 * If offset too high, insert to sibling, update the offset and entry
 */
int cch_index_insert_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset,
	bool replace,
	void *value,
	struct cch_index_entry **new_index_entry,
	int *new_value_offset);

/* remove from index, return error, check_lock for entry */
int cch_index_remove(struct cch_index *index, uint64_t key);

int cch_index_remove_direct(
	struct cch_index *index,
	struct cch_index_entry *entry,
	int offset);

/* push excessive data to block device, reach max_mem_kb memory usage */
int cch_index_shrink(struct cch_index_entry *index, int max_mem_kb);

/* restore from disk */
int cch_index_restore(struct cch_index_entry *index);

#endif  /* RELDATA_INDEX_H */
