
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t cluster_t;

/***
 * These functions are defined at ps2mcfs.c
 * because they involve accessing the superblock
 **/
size_t fat_cluster_size(void* data);
cluster_t fat_max_cluster(void* data);
cluster_t fat_get_table_entry(void* data, cluster_t clus);
void fat_set_table_entry(void* data, cluster_t clus, cluster_t newval);

/***
 * Frees all the clusters starting at 'clus' until the terminator is found.
 * if (terminate) 'clus' is turned into a terminator instead
 **/
void fat_free(void* data, cluster_t clus, bool terminate);
/***
 * Expands the chain starting at 'clus' up to 'count' clusters
 * returns the last cluster or 0xFFFFFFFF if no free clusters are found.
 * if (terminate) the last cluster is turned into a terminator
 **/
cluster_t fat_expand(void* data, cluster_t clus, size_t count, bool terminate);

/***
 * Returns the cluster relative to clus0 according to whence:
 * if (whence == SEEK_CUR) move 'count' clusters ahead and return
 * if (whence == SEEK_END) move 'count' clusters before the end and return
 * in any case, if 'count' is greater than the cluster count, return 0xFFFFFFFF
 **/
cluster_t fat_seek(void* data, cluster_t clus0, size_t count, int whence);

/***
 * Returns a free cluster or 0xFFFFFFFF if none is found.
 * 'clus' should be the starting for the search.
 **/
cluster_t fat_find_free_cluster(void* data, cluster_t clus);

/***
 * Makes the chain span 'count' clusters and updates terminators accordingly
 * returns the last cluster or 0xFFFFFFFF if ran out of space while expanding
 **/
cluster_t fat_truncate(void* data, cluster_t clus, size_t count);

/***
 * seek in bytes from the start of the cluster.
 * Stores the resulting relative offset to the first allocatable cluster in bytes inside *dest
 * Returns true on success, false if the offset is beyond the size of the chain
 **/
bool fat_seek_bytes(void* data, cluster_t clus0, off_t offset, off_t* dest);

