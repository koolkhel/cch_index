#include <linux/kernel.h>
#include <linux/module.h>

#define LOG_PREFIX "load"

#include "cch_index.h"
#include "cch_index_debug.h"


/*
 * Wrappers to index function to avoid repeating of error checking
 * in tests.
 *
 */
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

static struct {
	uint64_t key;
	void *value;
} test_values[] = {
	{0x0102030401020304ULL, (void *) 0x04030201},
	{0x0102030401020305ULL, (void *) 0x66666666},
	{0x123456, (void *) 0x234567},
	{0x765432, (void *) 0x542123},
	{0x1, (void *) 0x1},
	{0xdeadbeefdeadbeefULL, (void *) 0xdeadbeef}
};

/*
 * Test index creation, insert, remove and destroy with
 * single value and no direct access to index entries.
 */
static int smoke_test(void)
{
	struct cch_index *index = NULL;
	int result = 0;
	struct cch_index_entry *new_index_entry;
	int new_value_offset = 0;
	void *found_value = NULL;
	int i = 0;

	PRINT_INFO("******** create index *************\n");
	result = cch_index_create(
		/* levels */    6,
		/* total bits */     64,
		/* root_bits */ 8,
		/* low_bits */  8, /* 46 total */
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

	PRINT_INFO("******** insert index entries ********\n");
	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		result = insert_to_index(index, test_values[i].key,
					 test_values[i].value,
					 &new_index_entry, &new_value_offset);

		if (result)
			goto insert_failure;
	}

	PRINT_INFO("********** find the index entry by key *********\n");
	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		result = search_index(index, test_values[i].key, &found_value,
				      &new_index_entry, &new_value_offset,
				      test_values[i].value);

		if (result)
			goto search_failure;
	}

search_failure:
	PRINT_INFO("****** remove entry assuming it exists ***********\n");
	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		result = index_remove_existing(index, test_values[i].key);

		if (result)
			goto remove_failure;
	}

insert_failure:
remove_failure:
	PRINT_INFO("********** attempt to destroy the index **********");
	cch_index_destroy(index);
	PRINT_INFO("index destroy successful");
creation_failure:
	return result;
}

static int direct_test(void)
{
	int result;
	struct cch_index *index;
	struct cch_index_entry *new_index_entry, *index_entry;
	struct cch_index_entry *first_index_entry;
	void *insert_value;
	int new_value_offset, offset;
	int first_offset;
	void *cmp_value;
	int i = 0;
	PRINT_INFO("******** *_direct functions test  *************\n");
	result = cch_index_create(/* levels */    6,
				  /* total bits */      64,
				  /* root_bits */ 8,
				  /* low_bits */  8,
				  cch_index_start_save_fn,
				  cch_index_finish_save_fn,
				  cch_index_entry_save_fn,
				  cch_index_value_free_fn,
				  cch_index_load_data_fn,
				  cch_index_load_entry_fn,
				  &index);
	if (result != 0) {
		PRINT_ERROR("index creation failure, result %d", result);
		goto creation_failure;
	}

	insert_value = (void *) 0xBEEFDEADUL;
	result = insert_to_index(index, 0x0, insert_value,
		&first_index_entry, &first_offset);
	if (result) {
		PRINT_ERROR("index insert failure, result %d", result);
		goto insert_failure;
	}
	index_entry = first_index_entry;
	offset = first_offset;
	#define NUM_TEST_RECORDS 4098
	/* --------------------------------------------------- */
	/* now we should insert entries one by one without key */
	/* --------------------------------------------------- */
	for (i = 0; i < NUM_TEST_RECORDS; i++) {
		offset++;
		PRINT_INFO("inserting %d th record at offset %d",
			   i, offset);
		insert_value = (void *) (((unsigned long) insert_value) + 1);
		result = cch_index_insert_direct(index, index_entry,
			offset, false, insert_value,
			&new_index_entry, &new_value_offset);
		if (result) {
			PRINT_ERROR("couldn't insert direct, result %d",
				result);
			goto insert_failure;
		}
		PRINT_INFO("inserted to entry %p with offset %d",
			   new_index_entry, new_value_offset);

		if (index_entry != new_index_entry)
			PRINT_ERROR("inserted successfully to new index entry");

		index_entry = new_index_entry;
		offset = new_value_offset;
	}

	PRINT_ERROR("insert_direct test successful");

	offset = first_offset;
	index_entry = first_index_entry;
	cmp_value = (void *) 0xBEEFDEADUL;
	for (i = 0; i < NUM_TEST_RECORDS; i++) {
		offset++;
		cmp_value = (void *) (((unsigned long) cmp_value) + 1);
		result = cch_index_find_direct(index, index_entry,
			offset, &insert_value,
			&new_index_entry, &new_value_offset);

		if (result) {
			PRINT_ERROR("error find direct");
			goto search_failure;
		}

		if (cmp_value != insert_value) {
			PRINT_ERROR("got %p instead of %p",
				insert_value, cmp_value);
			goto search_failure;
		}
		index_entry = new_index_entry;
		offset = new_value_offset;
	}

	PRINT_ERROR("find_direct test successful");
search_failure:
insert_failure:
	PRINT_ERROR("%d records were successful", i);
	cch_index_destroy(index);
creation_failure:
	return result;
}

static int __init reldata_index_init(void)
{
	int result = 0;
	PRINT_INFO("hello, world!");
	result = smoke_test();
	if (result != 0)
		PRINT_ERROR("smoke test failure");
	else
		PRINT_ERROR("smoke test OK");
	result = direct_test();
	if (result)
		PRINT_ERROR("direct test failure");
	else
		PRINT_ERROR("direct test success");
	return result;
}

static void reldata_index_shutdown(void)
{
	printk(KERN_INFO "bye!\n");
}

MODULE_LICENSE("GPL");

module_init(reldata_index_init);
module_exit(reldata_index_shutdown);
