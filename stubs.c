
#define LOG_PREFIX "cch_index_stubs"

#include "cch_index_debug.h"
#include "cch_index.h"



/**
 * This file contains all functions marked as "out of scope of this project"
 * for index to be able to use them
 */

int cch_index_check_lock(void *value)
{
	/* stub implementation */
	TRACE_ENTRY();

	PRINT_INFO("check lock on %lx", (unsigned long) value);

	TRACE_EXIT();
	return 0; /* not locked */
}

int cch_index_value_lock(void *value)
{
	int result = 0;
	TRACE_ENTRY();

	PRINT_INFO("value lock on %lx", (unsigned long) value);

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_value_test_and_lock(void *value)
{
	int result = 0;
	TRACE_ENTRY();

	PRINT_INFO("value try and lock on %lx", (unsigned long) value);

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_value_unlock(void *value)
{
	int result = 0;

	TRACE_ENTRY();

	PRINT_INFO("value unlock on %lx", (unsigned long) value);

	TRACE_EXIT_RES(result);
	return result;
}

void cch_index_on_new_entry_alloc(
	struct cch_index *index, int inc_size, int new_size)
{
	TRACE_ENTRY();

	PRINT_INFO("new index record allocated, more by %d with total %d",
		   inc_size, new_size);

	TRACE_EXIT();
	return;
}

void cch_index_on_entry_free(
	struct cch_index *index, int dec_size, int new_size)
{
	TRACE_ENTRY();

	PRINT_INFO("index record free, less by %d with total %d",
		   dec_size, new_size);

	TRACE_EXIT();
	return;
}

int cch_index_start_full_save(struct cch_index *index)
{
	int result = 0;

	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_finish_full_save(struct cch_index *index)
{
	int result = 0;

	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_write_cluster_data(struct cch_index * index,
	uint64_t offset, const uint8_t *buffer, int buf_len)
{
	int result = 0;
	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_read_cluster_data(struct cch_index * index,
	uint64_t offset, uint8_t *buffer, int buf_len)
{
	int result = 0;

	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_start_transaction(struct cch_index *index)
{
	int result = 0;

	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_finish_transaction(struct cch_index *index)
{
	int result = 0;

	TRACE_ENTRY();

	TRACE_EXIT_RES(result);
	return result;
}
