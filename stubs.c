#include <linux/slab.h>

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

/*
 * As for stubbing I/O:
 * 
 * Let's put all write request into a linked list of
 * clusters with known address. When write is done,
 * a new entry appends to the list. When read is done,
 * list is searched for corresponding offset (should
 * be same as write offset, we write clusters in a whole,
 * not just parts of them, at least at this point of time)
 */

struct kmem_cache *cch_written_cluster_cache_stub = NULL;
int stub_cluster_size;
LIST_HEAD(cch_written_cluster_head);

struct cch_written_cluster_stub {
	struct list_head cch_written_cluster_entry;
	uint64_t offset;
	int len; /* should be multiple of cluster size
		  * which is yet unknown
		  */
	uint8_t data[];
};

int cch_index_io_stub_setup(int cluster_size)
{
	int result = 0;
	TRACE_ENTRY();

	cch_written_cluster_cache_stub = kmem_cache_create(
		"cch_stub_cluster_cache", cluster_size +
		sizeof(struct cch_written_cluster_stub),
		8, 0, NULL);

	if (!cch_written_cluster_cache_stub)
		sBUG();

	stub_cluster_size = cluster_size;
	
	TRACE_EXIT_RES(result);
	return result;
}

void cch_index_io_stub_shutdown(void)
{
	struct cch_written_cluster_stub *cluster, *next;
	
	
	TRACE_ENTRY();

	list_for_each_entry_safe(cluster, next,
		&cch_written_cluster_head,
		cch_written_cluster_entry) {

		list_del(&(cluster->cch_written_cluster_entry));
		kmem_cache_free(cch_written_cluster_cache_stub, cluster);
	};

	kmem_cache_destroy(cch_written_cluster_cache_stub);

	TRACE_EXIT();
	return;
}

int cch_index_write_cluster_data(struct cch_index * index,
	uint64_t offset, const uint8_t *buffer, int buf_len)
{
	int result = 0;
	int overwrite = 0; /* should we overwrite existing cluster */
	struct cch_written_cluster_stub *cluster;
	TRACE_ENTRY();

	/* we assume we speak in terms of single clusters now */
	sBUG_ON(buf_len != stub_cluster_size);
	/* we can't check that offset is multiple of cluster size
	 * because division is prohibited
	 */
	
	/* case 1. seek for given offset, write there if any */
	list_for_each_entry(cluster, &cch_written_cluster_head,
			    cch_written_cluster_entry) {
		if (cluster->offset == offset) {
			overwrite = 1;
			break;
		}
	};

	if (overwrite)
		goto write_cluster;

	/* case 2. create one, append to list, write to it */
	PRINT_INFO("creating new cluster");
	cluster = kmem_cache_zalloc(cch_written_cluster_cache_stub,
		GFP_KERNEL);
	list_add(&(cluster->cch_written_cluster_entry),
		 &cch_written_cluster_head);

	cluster->offset = offset;
	cluster->len = buf_len;

write_cluster:
	memcpy(cluster->data, buffer, buf_len);

	TRACE_EXIT_RES(result);
	return result;
}

int cch_index_read_cluster_data(struct cch_index * index,
	uint64_t offset, uint8_t *buffer, int buf_len)
{
	int result = 0;
	int found = 0;
	struct cch_written_cluster_stub *cluster;

	TRACE_ENTRY();

	/* again, only reads at cluster start and of
	 * single cluster size are supported */
	sBUG_ON(buf_len != stub_cluster_size);

	/* seek for cluster having given offset and
	 * return it
	 */

	list_for_each_entry(cluster, &cch_written_cluster_head,
			    cch_written_cluster_entry) {
		if (cluster->offset == offset) {
			found = 1;
			break;
		}
	};

	if (!found) {
		/* pretty bad handling, but we live in assumptions */
		result = -ENOENT;
		goto out;
	}

	memcpy(buffer, cluster->data, buf_len);

out:
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
