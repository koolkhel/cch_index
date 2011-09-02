
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
	return 0;
}

int cch_index_value_lock(void *value)
{
	int result = 0;
	TRACE_ENTRY();

	PRINT_INFO("value lock on %lx", (unsigned long) value);

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

void cch_index_alloc_new_cluster(void)
{
	TRACE_ENTRY();
	PRINT_INFO("alloc new cluster\n");
	TRACE_EXIT();
	return;
}

void cch_index_free_cluster(void)
{
	TRACE_ENTRY();
	PRINT_INFO("free cluster\n");
	TRACE_EXIT();
	return;
}

void cch_index_start_save_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}

void cch_index_finish_save_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}

void cch_index_entry_save_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}

void cch_index_value_free_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}

void cch_index_load_data_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}

void cch_index_load_entry_fn(void)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return;
}
