
#include <stdio.h>
#include <string.h>

#include "fat.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))

void fat_free(void* data, cluster_t clus, bool terminate) {
	cluster_t fat_value = fat_get_table_entry(data, clus);
	if (terminate && fat_value >= 0x80000000 && fat_value != 0xFFFFFFFF) {
		fat_set_table_entry(data, clus, 0xFFFFFFFF);
		clus = fat_value - 0x80000000;
		fat_value = fat_get_table_entry(data, clus);
	}
	while (fat_value >= 0x80000000 && fat_value != 0xFFFFFFFF) {
		fat_set_table_entry(data, clus, 0);
		clus = fat_value - 0x80000000;
		fat_value = fat_get_table_entry(data, clus);
	}
	fat_set_table_entry(data, clus, 0);
}
cluster_t fat_expand(void* data, cluster_t clus, size_t count) {
	cluster_t clus0 = fat_seek(data, clus, 0, SEEK_END);
	clus = clus0;
	while (count > 0) {
		cluster_t new_clus = fat_find_free_cluster(data, clus);
		if (new_clus == 0xFFFFFFFF) {
			fat_free(data, clus0, true);
			fat_set_table_entry(data, clus0, 0xFFFFFFFF);
			return 0xFFFFFFFF;
		}
		fat_set_table_entry(data, clus, new_clus + 0x80000000);
		clus = new_clus;
		--count;
	}
	fat_set_table_entry(data, clus, 0xFFFFFFFF);
	return clus;
}

cluster_t fat_allocate(void* data, size_t len) {
	if (len == 0xFFFFFFFF)
		return 0xFFFFFFFF;
	cluster_t start = fat_find_free_cluster(data, 0);
	if (start == 0xFFFFFFFF)
		return 0xFFFFFFFF;
	cluster_t end = fat_expand(data, start, len-1);
	if (end == 0xFFFFFFFF) {
		fat_set_table_entry(data, start, 0);
		return 0xFFFFFFFF;
	}
	return start;
}

cluster_t fat_seek(void* data, cluster_t clus0, size_t count, int whence) {
	cluster_t clus = clus0;
	while(count > 0) {
		cluster_t fat_value = fat_get_table_entry(data, clus);
		if (fat_value == 0xFFFFFFFF || fat_value < 0x80000000)
			return 0xFFFFFFFF; // error: cannot seek beyond the size of the chain
		clus = fat_value - 0x80000000;
		--count;
	}
	// clus is now ahead of clus0 by 'count' bytes

	if (whence == SEEK_CUR || whence == SEEK_SET) {
		return clus;
	}
	else if (whence == SEEK_END) {
		cluster_t fat_value;
		fat_value = fat_get_table_entry(data, clus);
		while (fat_value != 0xFFFFFFFF && fat_value > 0x80000000) {
			clus = fat_value - 0x80000000;
			if (count != 0)
				// no need to verify 'clus0' for terminator because it's behind 'clus'
				clus0 = fat_get_table_entry(data, clus0) - 0x80000000;
			fat_value = fat_get_table_entry(data, clus);
		}
		// if offset is 0, there's no need to advance clus0, just return clus
		return (count != 0) ? clus0 : clus;
	}
	return 0xFFFFFFFF;
}

cluster_t fat_find_free_cluster(void* data, cluster_t clus) {
	for (off_t i = 0; i < fat_max_cluster(data); ++i) {
		cluster_t fat_value = fat_get_table_entry(data, (clus+i) % fat_max_cluster(data));
		if (fat_value < 0x80000000)
			return (clus+i) % fat_max_cluster(data);
	}
	return 0xFFFFFFFF;
}

cluster_t fat_truncate(void* data, cluster_t clus, size_t count) {
	if (count == 0) { // delete a whole chain
		fat_free(data, clus, false);
		return 0xFFFFFFFF;
	}

	// skip pre-allocated clusters
	cluster_t fat_value = fat_get_table_entry(data, clus);
	while (count > 0 && fat_value >= 0x80000000 && fat_value != 0xFFFFFFFF) {
		clus = fat_value - 0x80000000;
		fat_value = fat_get_table_entry(data, clus);
		--count;
	}

	if (count == 0 && fat_value != 0xFFFFFFFF) { // shrink the chain, terminate at clus
		fat_free(data, clus, true);
		return clus;
	}
	else if (count > 1 && (fat_value == 0xFFFFFFFF || fat_value < 0x80000000)) { // expand from clus if exit by fat_value
		return fat_expand(data, clus, count-1);
	}
	// count equals 0 and fat_value is 0xFFFFFFFF
	return clus; // leave as-is
}

off_t fat_seek_bytes(void* data, cluster_t clus0, off_t offset) {
	size_t k = fat_cluster_size(data);
	while (offset >= k) {
		clus0 = fat_seek(data, clus0, 1, SEEK_CUR);
		if (clus0 == 0xFFFFFFFF)
			return 0;
		offset -= k;
	}
	return fat_first_cluster_offset(data) + clus0 * k + offset;
}

/***
 * Copies data from fat to read_buf, then
 * copies data from write_buf to fat.
 * if either read_buf or write_buf, skip their
 * respective data copy operations
 */
size_t fat_rw_bytes(void* data, cluster_t clus0, off_t offset, size_t size, void* restrict read_buf, const void* restrict write_buf) {
	size_t k = fat_cluster_size(data);
	off_t fs_position = fat_seek_bytes(data, clus0, offset);
	if (fs_position == 0)
		return 0;
	size_t buf_position = MIN(size, k-offset%k);
	if (read_buf)
		memcpy(read_buf, data+fs_position, buf_position);
	if (write_buf)
		memcpy(data+fs_position, write_buf, buf_position);
	while(buf_position < size) {
		clus0 = fat_seek(data, clus0, 1, SEEK_CUR);
		fs_position = fat_seek_bytes(data, clus0, 0);
		if (clus0 == 0xFFFFFFFF)
			return buf_position;
		size_t s = MIN(size-buf_position, k);
		if (read_buf)
			memcpy(read_buf+buf_position, data+fs_position, s);
		if (write_buf)
			memcpy(data+fs_position, write_buf+buf_position, s);
		buf_position += s;
	}
	return buf_position;
}

size_t fat_read_bytes(void* data, cluster_t clus0, off_t offset, size_t size, void* buf) {
	return fat_rw_bytes(data, clus0, offset, size, buf, NULL);
}
size_t fat_write_bytes(void* data, cluster_t clus0, off_t offset, size_t size, const void* buf) {
	return fat_rw_bytes(data, clus0, offset, size, NULL, buf);
}

