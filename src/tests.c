#include <stdio.h>
#include <munit/munit.h>

#include "mc_writer.h"
#include "ps2mcfs.h"
#include "vmc_types.h"
#include "utils.h"


size_t count_occupied_clusters(vmc_meta_t* vmc_meta) {
	size_t result = 0;
	for (cluster_t i = 0; i < vmc_meta->superblock->last_allocatable; i++) {
		if (fat_get_table_entry(vmc_meta, i).entry.occupied)
			result += 1;
	}
	return result;
}


static MunitResult test_new_empty_card_with_ecc(const MunitParameter params[], void* data) {
	vmc_meta_t* vmc_meta = data;
	superblock_t* superblock = vmc_meta->raw_data;

	dir_entry_t dirent;
	ps2mcfs_get_child(vmc_meta, superblock->root_cluster, 0, &dirent);
	munit_assert_int(dirent.cluster, ==, 0);
	munit_assert_ulong(dirent.length, ==, 2);
	munit_assert_string_equal(dirent.name, ".");
	ps2mcfs_get_child(vmc_meta, superblock->root_cluster, 1, &dirent);
	munit_assert_int(dirent.cluster, ==, 0);
	munit_assert_string_equal(dirent.name, "..");

	size_t occupied_clusters = count_occupied_clusters(vmc_meta);

	// the number of occupied clusters must be equal to the number of clusters occupied by 2 dirents
	munit_assert_long(occupied_clusters, ==, div_ceil(2 * sizeof(dirent), superblock->page_size * superblock->pages_per_cluster));

	return MUNIT_OK;
}


static MunitResult test_fat_truncate(const MunitParameter params[], void* data) {
  	vmc_meta_t* vmc_meta = data;
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
	// extend
	fat_truncate(vmc_meta, vmc_meta->superblock->root_cluster, 5);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 5);
	// no-op
	fat_truncate(vmc_meta, vmc_meta->superblock->root_cluster, 5);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 5);
	// shrink
	fat_truncate(vmc_meta, vmc_meta->superblock->root_cluster, 2);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 2);
	// return to original size
	fat_truncate(vmc_meta, vmc_meta->superblock->root_cluster, 1);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
	// no-op
	fat_truncate(vmc_meta, vmc_meta->superblock->root_cluster, 1);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
    return MUNIT_OK;
}


static void* fixture_memory_card_with_ecc_setup(const MunitParameter params[], void* user_data) {
	(void) params;

	void* data = malloc(8650752); // 8MB card
	FILE* data_file = fmemopen(data, 8650752, "w");
	superblock_t superblock = DEFAULT_SUPERBLOCK;
	superblock.card_flags |= CF_USE_ECC;
	mc_writer_write_empty(&superblock, data_file);
	fclose(data_file);

	vmc_meta_t* vmc_meta = malloc(sizeof(vmc_meta_t));
	vmc_meta->ecc_bytes = 12;
	vmc_meta->page_spare_area_size = 16;
	vmc_meta->raw_data = data;
	vmc_meta->superblock = data;

	return vmc_meta;
}

static void fixture_vmc_meta_teardown(void* fixture) {
  vmc_meta_t* vmc_meta = fixture;
  free(vmc_meta->raw_data);
  free(vmc_meta);
}

static MunitTest test_suite_tests[] = {
	{ (char*) "/mkfsps2", test_new_empty_card_with_ecc, fixture_memory_card_with_ecc_setup, fixture_vmc_meta_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ (char*) "/fat/truncate", test_fat_truncate, fixture_memory_card_with_ecc_setup, fixture_vmc_meta_teardown, MUNIT_TEST_OPTION_NONE, NULL },

	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};


static const MunitSuite test_suite = {
	(char*) "", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

/* This is only necessary for EXIT_SUCCESS and EXIT_FAILURE */
#include <stdlib.h>

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
	return munit_suite_main(&test_suite, NULL, argc, argv);
}
