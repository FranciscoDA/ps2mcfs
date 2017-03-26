
#include <stdio.h>

#include "fat.h"

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
cluster_t fat_expand(void* data, cluster_t clus, size_t count, bool terminate) {
	cluster_t clus0 = fat_seek(data, clus, 0, SEEK_END);
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
	if (terminate)
		fat_set_table_entry(data, clus, 0xFFFFFFFF);
	return clus;
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

	if (whence == SEEK_CUR) {
		return clus;
	}
	else if (whence == SEEK_END) {
		cluster_t fat_value;
		while ((fat_value = fat_get_table_entry(data, clus)) != 0xFFFFFFFF) {
			clus = fat_value - 0x80000000;
			if (count != 0) // no need to verify 'clus0' for terminator because it's behind 'clus'
				clus0 = fat_get_table_entry(data, clus0) - 0x80000000;
		}
		// if offset is 0, there's no need to advance clus0, just return clus
		return (count != 0) ? clus0 : clus;
	}
	// seek_set is not supported :(
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
	if (count == 0) { // we cannot delete a whole chain, so it's not possible to truncate to zero
		return 0xFFFFFFFF; // return error in this case
	}

	// skip pre-allocated clusters
	cluster_t fat_value = fat_get_table_entry(data, clus);
	while (count > 1 && fat_value >= 0x80000000 && fat_value != 0xFFFFFFFF) {
		clus = fat_value - 0x80000000;
		fat_value = fat_get_table_entry(data, clus);
		--count;
	}

	if (count == 1) { // shrink the chain, terminate at clus
		fat_free(data, clus, true);
		return clus;
	}
	return fat_expand(data, clus, count-1, true); // else expand by count-1
}

bool fat_seek_bytes(void* data, cluster_t clus0, off_t offset, off_t* dest) {
	size_t k = fat_cluster_size(data);
	while (offset >= k) {
		clus0 = fat_seek(data, clus0, 1, SEEK_CUR);
		if (clus0 == 0xFFFFFFFF)
			return false;
		offset -= k;
	}
	*dest = clus0 * k + offset;
	return true;
}
