
#include <stdio.h>
#include <string.h>

#include "fat.h"
#include "ecc.h"
#include "vmc_types.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Data about the card geometry */

size_t fat_page_size(const vmc_meta_t* vmc_meta)        { return vmc_meta->superblock->page_size + vmc_meta->page_spare_area_size; }
size_t fat_page_capacity(const vmc_meta_t* vmc_meta)    { return vmc_meta->superblock->page_size; }
size_t fat_cluster_size(const vmc_meta_t* vmc_meta)     { return fat_page_size(vmc_meta) * vmc_meta->superblock->pages_per_cluster; }
size_t fat_cluster_capacity(const vmc_meta_t* vmc_meta) { return fat_page_capacity(vmc_meta) * vmc_meta->superblock->pages_per_cluster; }

physical_offset_t fat_logical_to_physical_offset(const vmc_meta_t* vmc_meta, cluster_t cluster, logical_offset_t bytes_offset) {
	const size_t k_capacity = fat_cluster_capacity(vmc_meta);
	const size_t k_size = fat_cluster_size(vmc_meta);
	const size_t p_capacity = fat_page_capacity(vmc_meta);
	const size_t p_size = fat_page_size(vmc_meta);
	cluster = fat_seek(vmc_meta, cluster, bytes_offset / k_capacity);
	bytes_offset = bytes_offset % k_capacity;
	cluster += vmc_meta->superblock->first_allocatable;
	return cluster * k_size + bytes_offset / p_capacity * p_size;
}

fat_entry_t* fat_get_entry_ptr(const vmc_meta_t* vmc_meta, cluster_t clus) {
	size_t cluster_capacity = fat_cluster_capacity(vmc_meta);
	size_t cluster_size = fat_cluster_size(vmc_meta);

	uint32_t k = cluster_capacity / sizeof(fat_entry_t); // fat/indirfat entries per cluster (256 in a typical card)
	uint32_t fat_offset = clus % k;
	uint32_t indirect_index = clus / k;
	uint32_t indirect_offset = indirect_index % k;
	uint32_t dbl_indirect_index = indirect_index / k;
	uint32_t indirect_cluster_num = vmc_meta->superblock->indirect_fat_clusters[dbl_indirect_index];
	uint32_t* fat_cluster_ptr = vmc_meta->raw_data + indirect_cluster_num * cluster_size + indirect_offset * sizeof(fat_entry_t);
	uint32_t fat_cluster_num = *fat_cluster_ptr;
	return (fat_entry_t*) (vmc_meta->raw_data + fat_cluster_num * cluster_size + fat_offset * sizeof(fat_entry_t));
}

fat_entry_t fat_get_table_entry(const vmc_meta_t* vmc_meta, cluster_t clus) {
	return *fat_get_entry_ptr(vmc_meta, clus);
}

void fat_set_table_entry(const vmc_meta_t* vmc_meta, cluster_t clus, fat_entry_t newval) {
	*fat_get_entry_ptr(vmc_meta, clus) = newval;
}

cluster_t fat_allocate(const vmc_meta_t* vmc_meta, size_t len) {
	cluster_t start = fat_find_free_cluster(vmc_meta, 0);
	if (start == CLUSTER_INVALID)
		return CLUSTER_INVALID;
	fat_set_table_entry(vmc_meta, start, FAT_ENTRY_TERMINATOR);
	cluster_t end = fat_truncate(vmc_meta, start, len);
	if (end == CLUSTER_INVALID) {
		fat_set_table_entry(vmc_meta, start, FAT_ENTRY_FREE);
		return CLUSTER_INVALID;
	}
	return start;
}

cluster_t fat_seek(const vmc_meta_t* vmc_meta, cluster_t cluster, size_t count) {
	while(count > 0) {
		fat_entry_t fat_value = fat_get_table_entry(vmc_meta, cluster);
		if (fat_value.raw == FAT_ENTRY_TERMINATOR.raw || !fat_value.entry.occupied)
			return CLUSTER_INVALID;
		cluster = fat_value.entry.next_cluster;
		--count;
	}
	return cluster;
}

/**
 * Get the last cluster in the chain
*/
cluster_t fat_seek_last_cluster(const vmc_meta_t* vmc_meta, cluster_t cluster) {
	cluster_t next_cluster = fat_seek(vmc_meta, cluster, 1);
	while (next_cluster != CLUSTER_INVALID) {
		cluster = next_cluster;
		next_cluster = fat_seek(vmc_meta, cluster, 1);
	}
	return cluster;
}

cluster_t fat_find_free_cluster(const vmc_meta_t* vmc_meta, cluster_t clus) {
	for (uint32_t i = 0; i < vmc_meta->superblock->last_allocatable; ++i) {
		fat_entry_t fat_value = fat_get_table_entry(vmc_meta, (clus+i) % vmc_meta->superblock->last_allocatable);
		if (fat_value.entry.occupied)
			return (clus+i) % vmc_meta->superblock->last_allocatable;
	}
	return CLUSTER_INVALID;
}

cluster_t fat_truncate(const vmc_meta_t* vmc_meta, cluster_t clus, size_t truncated_length) {
	fat_entry_t fat_value = fat_get_table_entry(vmc_meta, clus);
	while (truncated_length > 1 && fat_value.entry.occupied) {
		if (fat_value.raw == FAT_ENTRY_TERMINATOR.raw)
			break;
		clus = fat_value.entry.next_cluster;
		fat_value = fat_get_table_entry(vmc_meta, clus);
		--truncated_length;
	}
	cluster_t last_cluster = clus;

	// case 0: nothing to do
	if (truncated_length == 1 && fat_value.raw == FAT_ENTRY_TERMINATOR.raw)
		return clus;

	// case 1: list length is greater than or equal to the desired `truncated_length`
	// free the rest of the list and set the terminator in `previous_cluster` if required
	else if (truncated_length == 0 || (truncated_length == 1 && fat_value.entry.occupied)) {
		while (fat_value.entry.occupied) {
			if (truncated_length == 1) {
				fat_set_table_entry(vmc_meta, clus, FAT_ENTRY_TERMINATOR);
				--truncated_length;
			}
			else {
				fat_set_table_entry(vmc_meta, clus, FAT_ENTRY_FREE);
			}
			if (fat_value.raw == FAT_ENTRY_TERMINATOR.raw)
				break;
			clus = fat_value.entry.next_cluster;
			fat_value = fat_get_table_entry(vmc_meta, clus);
		}
		return last_cluster;
	}
	// case 2: truncated size is greater than the size of the list
	while (truncated_length > 0) {
		cluster_t new_clus = fat_find_free_cluster(vmc_meta, 0);
		// we might run out of space while allocating new clusters
		// in that case, delete the chain we just built and return
		if (new_clus == CLUSTER_INVALID) {
			fat_truncate(vmc_meta, last_cluster, 1);
			return CLUSTER_INVALID;
		}
		// make `clus` point to `new_clus`
		fat_entry_t new_fat_value = { .entry = { .next_cluster = new_clus, .occupied = 1} };
		fat_set_table_entry(vmc_meta, clus, new_fat_value);
		// make `new_clus` the new terminator of the linked list
		fat_set_table_entry(vmc_meta, new_clus, FAT_ENTRY_TERMINATOR);
		clus = new_clus;
		--truncated_length;
	}
	return clus;
}

/**
 * Copies data from the file that starts at `clus` into read_buf, then copies data from write_buf to the file.
 * If either read_buf or write_buf are NULL, skip their respective data copy operations
 */
size_t fat_rw_bytes(const vmc_meta_t* vmc_meta, cluster_t clus, logical_offset_t offset, size_t buf_size, void* restrict read_buf, const void* restrict write_buf) {
	if (clus == CLUSTER_INVALID)
		return 0;
	const size_t k_capacity = fat_cluster_capacity(vmc_meta);
	const size_t p_capacity = fat_page_capacity(vmc_meta);
	const size_t p_size = fat_page_size(vmc_meta);

	size_t buf_offset = 0;
	while(buf_offset < buf_size) {
		if (clus == CLUSTER_INVALID)
			return buf_offset;

		clus = fat_seek(vmc_meta, clus, offset / k_capacity);
		offset %= k_capacity;
		const physical_offset_t mc_offset = fat_logical_to_physical_offset(vmc_meta, clus, offset);

		// copy until the end of the data part of the current page
		size_t buffer_left = buf_size - buf_offset;
		size_t page_left = p_capacity - offset % p_capacity;
		size_t s = MIN(buffer_left, page_left);

		physical_offset_t page_start = mc_offset / p_size * p_size;
		physical_offset_t spare_start = page_start + p_capacity;

		if (read_buf) {
			memcpy(read_buf + buf_offset, vmc_meta->raw_data + mc_offset, s);
			if (vmc_meta->ecc_bytes == 12) {
				bool ecc_ok = ecc512_check(vmc_meta->raw_data + spare_start, vmc_meta->raw_data + page_start);
				if (!ecc_ok) {
					DEBUG_printf("ECC mismatch at offset 0x%x (ECC data at: 0x%x)\n", page_start, spare_start);
				}
				else {
					DEBUG_printf("Success reading %lu bytes of data from 0x%x\n", s, mc_offset);
				}
			}
		}
		if (write_buf) {
			memcpy(vmc_meta->raw_data + mc_offset, write_buf + buf_offset, s);
			if (vmc_meta->ecc_bytes == 12) {
				ecc512_calculate(vmc_meta->raw_data + spare_start, vmc_meta->raw_data + page_start);
			}
		}
		buf_offset += s;
		offset += s;
	}
	return buf_offset;
}

size_t fat_read_bytes(const vmc_meta_t* vmc_meta, cluster_t clus0, logical_offset_t offset, size_t size, void* buf) {
	return fat_rw_bytes(vmc_meta, clus0, offset, size, buf, NULL);
}
size_t fat_write_bytes(const vmc_meta_t* vmc_meta, cluster_t clus0, logical_offset_t offset, size_t size, const void* buf) {
	return fat_rw_bytes(vmc_meta, clus0, offset, size, NULL, buf);
}

