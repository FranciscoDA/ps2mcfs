#ifndef __FAT_H__
#define __FAT_H__


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "vmc_types.h"


/**
 * Returns the physical size of a cluster in bytes (including ECC bytes)
*/
size_t fat_cluster_size(const vmc_meta_t* vmc_meta);

/**
 * Returns the logical size of a cluster in bytes
*/
size_t fat_cluster_capacity(const vmc_meta_t* vmc_meta);

/**
 * Returns the phyisical byte offset for the given allocatable cluster index and a
 * byte offset relative to the start of the memory card
*/
physical_offset_t fat_logical_to_physical_offset(const vmc_meta_t* vmc_meta, cluster_t cluster, logical_offset_t bytes_offset);

/**
 * Returns the FAT table entry value for a cluster
*/
fat_entry_t fat_get_table_entry(const vmc_meta_t* vmc_meta, cluster_t clus);

/**
 * Sets the FAT table entry for a cluster
*/
void fat_set_table_entry(const vmc_meta_t* vmc_meta, cluster_t clus, fat_entry_t newval);

/**
 * Move forward `count` clusters in the chain starting from clus0.
 * Returns CLUSTER_INVALID if tried to move past the end of the chain.
 **/
cluster_t fat_seek(const vmc_meta_t* vmc_meta, cluster_t clus0, size_t count);

/**
 * Returns a free cluster or 0xFFFFFFFF if none is found.
 * 'clus' should be the start cluster for the search.
 **/
cluster_t fat_find_free_cluster(const vmc_meta_t* vmc_meta, cluster_t clus);

/**
 * Makes the linked list span `count` clusters. Frees old clusters or allocates new ones if needed.
 * Returns the last cluster or CLUSTER_INVALID if ran out of space while extending the list.
 **/
cluster_t fat_truncate(const vmc_meta_t* vmc_meta, cluster_t clus, size_t count);

/**
 * Creates a new list spanning 'len' clusters
 * returns the first cluster or 0xFFFFFFFF if not enough space
 **/
cluster_t fat_allocate(const vmc_meta_t* vmc_meta, size_t len);

size_t fat_read_bytes(const vmc_meta_t* vmc_meta, cluster_t clus0, logical_offset_t offset, size_t size, void* buf);
size_t fat_write_bytes(const vmc_meta_t* vmc_meta, cluster_t clus0, logical_offset_t offset, size_t size, const void* buf);

#endif