#include <stdio.h>
#include <munit/munit.h>

#include "mc_writer.h"
#include "ps2mcfs.h"
#include "vmc_types.h"
#include "utils.h"


size_t count_occupied_clusters(struct vmc_meta* vmc_meta) {
	size_t result = 0;
	for (cluster_t i = 0; i < vmc_meta->superblock.last_allocatable; i++) {
		if (fat_get_table_entry(vmc_meta, i).entry.occupied)
			result += 1;
	}
	return result;
}


static MunitResult test_new_empty_card_with_ecc(const MunitParameter params[], void* data) {
	struct vmc_meta* vmc_meta = data;

	dir_entry_t dirent;

	// check that everything looks OK for the first direntry (the `.` dummy entry)
	ps2mcfs_get_child(vmc_meta, vmc_meta->superblock.root_cluster, 0, &dirent);
	munit_assert_int(dirent.cluster, ==, 0);
	munit_assert_ulong(dirent.length, ==, 2);
	munit_assert_string_equal(dirent.name, ".");

	// ditto for the `..` dummy entry
	ps2mcfs_get_child(vmc_meta, vmc_meta->superblock.root_cluster, 1, &dirent);
	munit_assert_int(dirent.cluster, ==, 0);
	munit_assert_string_equal(dirent.name, "..");

	// the number of occupied clusters must be equal to the number of clusters occupied by 2 dirents
	size_t occupied_clusters = count_occupied_clusters(vmc_meta);
	size_t expected_occupied_clusters = div_ceil(2 * sizeof(dirent), vmc_meta->superblock.page_size * vmc_meta->superblock.pages_per_cluster);
	munit_assert_long(occupied_clusters, ==, expected_occupied_clusters);

	return MUNIT_OK;
}


static MunitResult test_fat_truncate(const MunitParameter params[], void* data) {
  	struct vmc_meta* vmc_meta = data;
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
	// extend
	fat_truncate(vmc_meta, vmc_meta->superblock.root_cluster, 5);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 5);
	// no-op
	fat_truncate(vmc_meta, vmc_meta->superblock.root_cluster, 5);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 5);
	// shrink
	fat_truncate(vmc_meta, vmc_meta->superblock.root_cluster, 2);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 2);
	// return to original size
	fat_truncate(vmc_meta, vmc_meta->superblock.root_cluster, 1);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
	// no-op
	fat_truncate(vmc_meta, vmc_meta->superblock.root_cluster, 1);
	munit_assert_long(count_occupied_clusters(vmc_meta), ==, 1);
    return MUNIT_OK;
}


static void* fixture_memory_card_with_ecc_setup(const MunitParameter params[], void* user_data) {
	(void) params;

	struct vmc_meta* vmc_meta = malloc(sizeof(struct vmc_meta));
	vmc_meta->file = fmemopen(NULL, 8650752, "w+");// 8MB card
	superblock_t superblock = DEFAULT_SUPERBLOCK;
	superblock.card_flags |= CF_USE_ECC;
	mc_writer_write_empty(&superblock, vmc_meta->file);

	vmc_meta->ecc_bytes = 12;
	vmc_meta->page_spare_area_size = 16;
	fseek(vmc_meta->file, 0, SEEK_SET);
	fread(&vmc_meta->superblock, sizeof(vmc_meta->superblock), 1, vmc_meta->file);
	return vmc_meta;
}

static void fixture_vmc_meta_teardown(void* fixture) {
  struct vmc_meta* vmc_meta = fixture;
  fclose(vmc_meta->file);
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
