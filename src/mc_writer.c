#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mc_writer.h"
#include "ps2mcfs.h" // ps2mcfs_time_to_date_time
#include "vmc_types.h"
#include "ecc.h"
#include "utils.h"


int mc_writer_write_empty(const superblock_t* superblock, FILE* output_file) {
    size_t physical_page_size = superblock->page_size;
    if (superblock->card_flags & CF_USE_ECC) {
        physical_page_size += PAGE_SPARE_PART_SIZE; // account byte spare area
    }
    uint8_t* page_buffer = malloc(physical_page_size);
    const time_t the_time = time(NULL);
    date_time_t datetime;
    ps2mcfs_time_to_date_time(the_time, &datetime);

    const dir_entry_t ROOT_DIR_ENTRIES[] = {
        {
            .mode = DF_DIRECTORY | DF_EXISTS | DF_READ | DF_WRITE | DF_EXECUTE | DF_0400,
            .cluster = 0,
            .dir_entry = 0,
            .length = 2, // Two entries: "." and ".."
            .attributes = 0,
            .name = ".",
            .creation = datetime,
            .modification = datetime,
        },
        {
            .mode = DF_DIRECTORY | DF_EXISTS | DF_WRITE | DF_EXECUTE | DF_0400 | DF_HIDDEN,
            .cluster = 0,
            .length = 0,
            .length = 0,
            .attributes = 0,
            .name = "..",
            .creation = datetime,
            .modification = datetime,
        }
    };

    const unsigned dirents_per_page = superblock->page_size / sizeof(dir_entry_t);
    const unsigned words_per_cluster = superblock->page_size * superblock->pages_per_cluster / sizeof(uint32_t);
    const unsigned clusters_per_block = superblock->pages_per_block / superblock->pages_per_cluster;
    unsigned root_directory_entries_written = 0;
    unsigned indirect_fat_entries_written = 0;
    unsigned fat_entries_written = 0;
    unsigned allocatable_pages_written = 0;
    const unsigned max_fat_entries = superblock->last_allocatable;
    const unsigned max_indirect_fat_entries = div_ceil(max_fat_entries, words_per_cluster);
    const unsigned max_indirect_fat_clusters = div_ceil(max_indirect_fat_entries, words_per_cluster);
    #define DEBUG_LOG(fmt, ...) \
        DEBUG_printf( \
            "[off: 0x%06lx   clus: %4lu   block: %4lu] " fmt "\n", \
            ftell(output_file), \
            ftell(output_file) / physical_page_size / superblock->pages_per_cluster, \
            ftell(output_file) / physical_page_size / superblock->pages_per_block __VA_OPT__(,) \
            __VA_ARGS__ \
        )

    #define WRITE_ECC() if (superblock->card_flags & CF_USE_ECC) { \
        memset(page_buffer + superblock->page_size, 0, PAGE_SPARE_PART_SIZE); \
        ecc512_calculate(page_buffer + superblock->page_size, page_buffer); \
    }

    DEBUG_LOG("Writing superblock");
    memset(page_buffer, 0xFF, physical_page_size);
    memcpy(page_buffer, superblock, sizeof(superblock_t));
    WRITE_ECC();
    fwrite(page_buffer, physical_page_size, 1, output_file);

    // fill the rest of the pages of the block with 0xFF plus ECC data
    memset(page_buffer, 0xFF, physical_page_size);
    WRITE_ECC();
    for (int i = 0; i < superblock->pages_per_block - 1; i++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    DEBUG_printf("Max indirect FAT table entries: %d\n", max_indirect_fat_entries);
    while (indirect_fat_entries_written < max_indirect_fat_entries) {
        DEBUG_LOG("Writing indirect FAT table clusters (%u / %u)", indirect_fat_entries_written + 1, max_indirect_fat_entries);
        memset(page_buffer, 0xFF, physical_page_size);
        for (int i = 0; i < superblock->page_size / sizeof(uint32_t) && indirect_fat_entries_written < max_indirect_fat_entries; ++i, ++indirect_fat_entries_written) {
            // add an offset corresponding to 1 block (the superblock)
            uint32_t fat_cluster = clusters_per_block + max_indirect_fat_clusters + indirect_fat_entries_written;
            memcpy(page_buffer + i * sizeof(fat_cluster), &fat_cluster, sizeof(fat_cluster));
        }
        WRITE_ECC();
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }
    while (ftell(output_file) / physical_page_size / superblock->pages_per_cluster < clusters_per_block + max_indirect_fat_clusters) {
        memset(page_buffer, 0xFF, physical_page_size);
        WRITE_ECC();
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    while (fat_entries_written < max_fat_entries) {
        DEBUG_LOG("Writing FAT table (%u / %u)", fat_entries_written + 1, max_fat_entries);
        memset(page_buffer, 0xFF, physical_page_size);
        for (int i = 0; i < superblock->page_size / sizeof(union fat_entry) && fat_entries_written < max_fat_entries; ++i, ++fat_entries_written) {
            union fat_entry entry = {.entry = {.occupied = 0, .next_cluster = CLUSTER_INVALID}};
            if (fat_entries_written == 0) {
                entry.entry.occupied = 1;
            }
            memcpy(page_buffer + i * sizeof(union fat_entry), &entry, sizeof(union fat_entry));
        }
        WRITE_ECC();
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    while (root_directory_entries_written < ROOT_DIR_ENTRIES[0].length) {
        DEBUG_LOG("Writing root dir entry (%u / %u)", root_directory_entries_written + 1, ROOT_DIR_ENTRIES[0].length);
        unsigned copy_count = MIN(dirents_per_page, ROOT_DIR_ENTRIES[0].length - root_directory_entries_written);
        memset(page_buffer, 0xFF, physical_page_size);
        memcpy(page_buffer, &ROOT_DIR_ENTRIES[root_directory_entries_written], sizeof(dir_entry_t) * copy_count);
        WRITE_ECC();
        fwrite(page_buffer, physical_page_size, 1, output_file);
        root_directory_entries_written += copy_count;
        allocatable_pages_written += 1;
    }

    // write pages containing ECC data for the rest of the erase-block
    DEBUG_LOG("Writing padding data with ECC for erase block");
    memset(page_buffer, 0xFF, physical_page_size);
    WRITE_ECC();
    while (ftell(output_file) % (superblock->pages_per_block * physical_page_size) != 0) {
        fwrite(page_buffer, physical_page_size, 1, output_file);
        ++allocatable_pages_written;
    }

    DEBUG_LOG("Writing cleared allocatable clusters");
    memset(page_buffer, 0xFF, physical_page_size);
    for (; allocatable_pages_written < superblock->last_allocatable * superblock->pages_per_cluster; allocatable_pages_written++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    DEBUG_LOG("Writing erase block2");
    memset(page_buffer, 0xFF, physical_page_size);
    for (int i = 0; i < superblock->pages_per_block; i++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    DEBUG_LOG("Writing erase block1");
    for (int i = 0; i < superblock->pages_per_block; i++) {
        memset(page_buffer, 0xFF, physical_page_size);
        // erase block1 contains a copy of the superblock
        if (i == 0)
            memcpy(page_buffer, superblock, sizeof(superblock_t));
        WRITE_ECC();
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    free(page_buffer);
    return 0;

    #undef DEBUG_LOG
    #undef WRITE_ECC
}
