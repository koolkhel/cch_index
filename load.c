#include <linux/kernel.h>
#include <linux/module.h>

#define CCH_INDEX_DEBUG
#define LOG_PREFIX "load"
#include "cch_index_debug.h"
#include "cch_index.h"

static int insert_to_index(struct cch_index *index,
			   uint64_t key, void *value,
			   struct cch_index_entry **new_index_entry,
			   int *new_value_offset)
{
	int result;
	TRACE_ENTRY();
	printk(KERN_INFO "index insert start\n");
	result = cch_index_insert(index,
				  /* key */ key,
				  /* value */ value,
				  /* replace */ false,
				  new_index_entry,
				  new_value_offset);

	if (result != 0) {
		printk(KERN_INFO "index insert failure, result %d\n", result);
	} else {
		printk(KERN_INFO "index insert ok, address %lx, offset %x\n",
		       (unsigned long) *new_index_entry,
		       *new_value_offset);
	}
	TRACE_EXIT();
	return result;
}

static int search_index(struct cch_index *index, uint64_t key,
			void **found_value,
			struct cch_index_entry **new_index_entry,
			int *new_value_offset,
			void *reference)
{
	int result;
	TRACE_ENTRY();
	sBUG_ON(found_value == NULL);
	result = cch_index_find(index, key, found_value,
				new_index_entry, new_value_offset);
	if (result != 0) {
		PRINT_INFO("index search failure, result %d\n", result);
	} else {
		sBUG_ON(*found_value == NULL);
		/* we did find it, right ? */
		if (new_index_entry != NULL) {
			/* then we should have the right entry */
			sBUG_ON(*new_index_entry == NULL);
		}
		if (new_value_offset != NULL && new_index_entry != NULL) {
			PRINT_INFO("index search success at address 0x%lx, "
				   "offset 0x%x, with value 0x%lx\n",
				   (unsigned long) *new_index_entry,
				   *new_value_offset,
				   (unsigned long) *found_value);
		} else {
			PRINT_INFO("index search success with value %lx\n",
				   (unsigned long) *found_value);
		}
		if (reference != NULL) {
			if (*found_value == reference)
				PRINT_INFO("found exact match\n");
			else
				PRINT_INFO("found wrong match\n");
		}
	}
	TRACE_EXIT();
	return result;
}

static int index_remove_existing(struct cch_index *index, uint64_t key)
{
	int result;
	void *found_value;

	TRACE_ENTRY();
	PRINT_INFO("remove_existing: step 1: find the entry\n");
	result = search_index(index, key, &found_value,
			      NULL, NULL, NULL);

	if (result) {
		PRINT_ERROR("there is no key %llx in index", key);
		goto failure;
	}

	PRINT_INFO("remove_existing: step 2: remove entry by key\n");
	result = cch_index_remove(index, key);

	if (result != 0)
		PRINT_INFO("index remove failure, result %d\n", result);
	else
		PRINT_INFO("index remove successful\n");

	found_value = 0;
	PRINT_INFO("remove_existing: step 3: find entry again, should fail\n");
	result = search_index(index, key, &found_value,
			      NULL, NULL, NULL);

	if (result == 0) {
		PRINT_ERROR("index remove did not remove it\n");
		PRINT_ERROR("found %lx\n", (unsigned long) found_value);
		result = -EAGAIN; /* remove again :-) */
	} else {
		PRINT_INFO("index remove is actually successful\n");
		result = 0; /* this is expected */
	}
	TRACE_EXIT();
failure:
	return result;
}

/*
 * Test index creation, insert, remove and destroy with
 * single value and no direct access to index entries.
 */
static int smoke_test(void)
{
	#define INDEX_TEST_KEY 0x0102030401020304ULL
	#define INDEX_TEST_VALUE 0x04030201

	struct cch_index *index = NULL;
	int result = 0;
	struct cch_index_entry *new_index_entry;
	int new_value_offset = 0;
	void *found_value = NULL;

	PRINT_INFO("******** create index *************\n");
	result = cch_index_create(/* levels */    6,
				  /* bits */      48,
				  /* root_bits */ 8,
				  /* low_bits */  8, /* 64 total */
				  cch_index_start_save_fn,
				  cch_index_finish_save_fn,
				  cch_index_entry_save_fn,
				  cch_index_value_free_fn,
				  cch_index_load_data_fn,
				  cch_index_load_entry_fn,
				  &index);
	if (result != 0) {
		PRINT_INFO("index creation failure, result %d\n", result);
		goto creation_failure;
	}

	PRINT_INFO("******** insert one index entry ********\n");
	result = insert_to_index(index, INDEX_TEST_KEY,
				 (void *) INDEX_TEST_VALUE,
				 &new_index_entry, &new_value_offset);

	if (result)
		goto insert_failure;

	PRINT_INFO("********** find the index entry by key *********\n");
	result = search_index(index, INDEX_TEST_KEY, &found_value,
			      &new_index_entry, &new_value_offset,
			      (void *) INDEX_TEST_VALUE);

	if (result)
		goto search_failure;

search_failure:
	PRINT_INFO("****** remove entry assuming it exists ***********\n");
	result = index_remove_existing(index, INDEX_TEST_KEY);

	if (result)
		goto remove_failure;

insert_failure:
remove_failure:
	PRINT_INFO("********** attempt to destroy the index **********\n");
	cch_index_destroy(index);
	printk(KERN_INFO "index destroy successful\n");
creation_failure:
	return result;
}

static int __init reldata_index_init(void)
{
	int result = 0;
	printk(KERN_INFO "hello, world!\n");
	result = smoke_test();
	if (result != 0)
		printk(KERN_INFO "smoke test failure\n");
	else
		printk(KERN_INFO "we can send that! congragulations!\n");
	return 0;
}

static void reldata_index_shutdown(void)
{
	printk(KERN_INFO "bye!\n");
}

MODULE_LICENSE("GPL");

module_init(reldata_index_init);
module_exit(reldata_index_shutdown);
