#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "vmc_types.h"
#include "ecc.h"
#include "ps2mcfs.h" // ps2mcfs_time_to_date_time
#include "utils.h"


const unsigned PAGE_SPARE_PART_SIZE = 16;

static const struct option CLI_OPTIONS[] = {
    {.name = "size",   .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "ecc",    .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = "output", .has_arg = required_argument, .flag = NULL, .val = 0},
    {.name = "help",   .has_arg = no_argument,       .flag = NULL, .val = 0},
    {.name = NULL,     .has_arg = 0,                 .flag = NULL, .val = 0}
};

void usage(FILE* stream, const char* program_name, int exit_code) {
    fprintf(stream, "Usage: %s -o OUTPUT_FILE [-s SIZE] [-e] [-h]\n", program_name);
    exit(exit_code);
}

int copy_optarg(char** dest) {
    if (!optarg)
        return 0;
    size_t optarg_len = strlen(optarg);
    *dest = malloc(optarg_len);
    strcpy(*dest, optarg);
    return optarg_len;
}

void write_page_spare_part(void* spare_part_start, void* page_start) {
    memset(spare_part_start, 0, PAGE_SPARE_PART_SIZE);
    ecc512_calculate(spare_part_start, page_start);
}

int main(int argc, char** argv) {
    // initialize default superblock
    superblock_t superblock;
    memcpy(&superblock, &DEFAULT_SUPERBLOCK, sizeof(superblock_t));

    // parse options
    char* option_output_filename = NULL;
    int opt;
    int long_option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:eo:h", CLI_OPTIONS, &long_option_index)) != -1) {
        // parse -s / --size option
        if ((opt == 0 && long_option_index == 0) || opt == 's') {
            if (strcmp(optarg, "8") == 0) {
                superblock.clusters_per_card = 8192;
            }
            else {
                fprintf(stderr, "Invalid SIZE value: %s. Allowed values: 8.\n", optarg);
                usage(stderr, argv[0], EXIT_FAILURE);
            }
        }
        // parse -e / --ecc option
        else if ((opt == 0 && long_option_index == 1) || opt == 'e') {
            superblock.card_flags |= CF_USE_ECC;
        }
        // parse -o / --output option
        else if ((opt == 0 && long_option_index == 2) || opt == 'o') {
            if (!copy_optarg(&option_output_filename)) {
                fprintf(stderr, "Invalid value: %s.\n", argv[optind]);
                usage(stderr, argv[0], EXIT_FAILURE);
            }
        }
        // parse -h / --help option
        else if ((opt == 0 && long_option_index == 3) || opt == 'h') {
            usage(stdout, argv[0], 0);
        }
        // handle invalid option
        else {
            fprintf(stderr, "Unrecognized option: %s.\n", argv[optind]);
            usage(stderr, argv[0], EXIT_FAILURE);
        }
    }

    if (option_output_filename == NULL) {
        fprintf(stderr, "Missing required argument: -o/--option\n");
        usage(stderr, argv[0], EXIT_FAILURE);
    }

    size_t physical_page_size = superblock.page_size;
    if (superblock.card_flags & CF_USE_ECC) {
        physical_page_size += PAGE_SPARE_PART_SIZE; // account byte spare area
    }
    uint8_t* page_buffer = malloc(physical_page_size);

    FILE* output_file = fopen(option_output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Could not open file for writing: %s\n", option_output_filename);
        exit(EXIT_FAILURE);
    }

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

    const unsigned dirents_per_page = superblock.page_size / sizeof(dir_entry_t);
    const unsigned words_per_cluster = superblock.page_size * superblock.pages_per_cluster / sizeof(uint32_t);
    unsigned root_directory_entries_written = 0;
    unsigned indirect_fat_entries_written = 0;
    unsigned fat_entries_written = 0;
    unsigned allocatable_pages_written = 0;
    const unsigned max_fat_entries = superblock.last_allocatable;
    const unsigned max_indirect_fat_entries = div_ceil(max_fat_entries, words_per_cluster);
    DEBUG_printf("Max indirect fat table entries: %u / %u = %u\n", max_fat_entries, words_per_cluster, max_indirect_fat_entries);

    uint32_t max_indirect_fat_cluster = 0;
    for(int i = 0; i < sizeof(superblock.indirect_fat_clusters) / sizeof(uint32_t); i++)
        if (superblock.indirect_fat_clusters[i] != 0)
            max_indirect_fat_cluster = MAX(max_indirect_fat_cluster, superblock.indirect_fat_clusters[i]);

    #define DEBUG_LOG(fmt, ...) \
        DEBUG_printf(fmt " at offset 0x%lx (cluster: %lu, block: %lu)\n" __VA_OPT__(,) __VA_ARGS__, ftell(output_file), ftell(output_file) / physical_page_size / superblock.pages_per_cluster, ftell(output_file) / physical_page_size / superblock.pages_per_block)
    
    DEBUG_LOG("Writing superblock");
    memset(page_buffer, 0xFF, physical_page_size);
    memcpy(page_buffer, &superblock, sizeof(superblock));
    if (superblock.card_flags & CF_USE_ECC)
        write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
    fwrite(page_buffer, physical_page_size, 1, output_file);

    // fill the rest of the pages of the block with 0xFF plus ECC data
    memset(page_buffer, 0xFF, physical_page_size);
    if (superblock.card_flags & CF_USE_ECC)
        write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
    for (int i = 0; i < superblock.pages_per_block - 1; i++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    while (indirect_fat_entries_written < max_indirect_fat_entries) {
        DEBUG_LOG("Writing indirect FAT table clusters (%u/%u)", indirect_fat_entries_written, max_indirect_fat_entries);
        memset(page_buffer, 0xFF, physical_page_size);
        for (int i = 0; i < superblock.page_size / sizeof(uint32_t) && indirect_fat_entries_written < max_indirect_fat_entries; ++i, ++indirect_fat_entries_written) {
            uint32_t fat_cluster = max_indirect_fat_cluster + 1 + indirect_fat_entries_written;
            memcpy(page_buffer + i * sizeof(fat_cluster), &fat_cluster, sizeof(fat_cluster));
        }
        if (superblock.card_flags & CF_USE_ECC)
            write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }
    while (ftell(output_file) / physical_page_size / superblock.pages_per_cluster < max_indirect_fat_cluster + 1) {
        memset(page_buffer, 0xFF, physical_page_size);
        if (superblock.card_flags & CF_USE_ECC)
            write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    while (fat_entries_written < max_fat_entries) {
        DEBUG_LOG("Writing FAT table");
        memset(page_buffer, 0xFF, physical_page_size);
        for (int i = 0; i < superblock.page_size / sizeof(fat_entry_t) && fat_entries_written < max_fat_entries; ++i, ++fat_entries_written) {
            fat_entry_t entry = {.entry = {.occupied = 0, .next_cluster = CLUSTER_INVALID}};
            if (fat_entries_written == 0) {
                entry.entry.occupied = 1;
            }
            memcpy(page_buffer + i * sizeof(fat_entry_t), &entry, sizeof(fat_entry_t));
        }
        if (superblock.card_flags & CF_USE_ECC)
            write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    while (root_directory_entries_written < ROOT_DIR_ENTRIES[0].length) {
        unsigned copy_count = MIN(dirents_per_page, ROOT_DIR_ENTRIES[0].length - root_directory_entries_written);
        DEBUG_LOG("Writing root dir entries %u-%u", root_directory_entries_written, root_directory_entries_written + copy_count - 1);
        memset(page_buffer, 0xFF, physical_page_size);
        memcpy(page_buffer, &ROOT_DIR_ENTRIES[root_directory_entries_written], sizeof(dir_entry_t) * copy_count);
        if (superblock.card_flags & CF_USE_ECC)
            write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
        fwrite(page_buffer, physical_page_size, 1, output_file);
        root_directory_entries_written += copy_count;
        allocatable_pages_written += 1;
    }

    // write pages containing ECC data for the rest of the erase-block
    DEBUG_LOG("Writing padding data with ECC for erase block");
    memset(page_buffer, 0xFF, physical_page_size);
    if (superblock.card_flags & CF_USE_ECC)
        write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
    while (ftell(output_file) % (superblock.pages_per_block * physical_page_size) != 0) {
        fwrite(page_buffer, physical_page_size, 1, output_file);
        ++allocatable_pages_written;
    }

    DEBUG_LOG("Writing cleared allocatable clusters");
    memset(page_buffer, 0xFF, physical_page_size);
    for (; allocatable_pages_written < superblock.last_allocatable * superblock.pages_per_cluster; allocatable_pages_written++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    DEBUG_LOG("Writing erase block2");
    memset(page_buffer, 0xFF, physical_page_size);
    for (int i = 0; i < superblock.pages_per_block; i++)
        fwrite(page_buffer, physical_page_size, 1, output_file);

    DEBUG_LOG("Writing erase block1");
    for (int i = 0; i < superblock.pages_per_block; i++) {
        memset(page_buffer, 0xFF, physical_page_size);
        // erase block1 contains a copy of the superblock
        if (i == 0)
            memcpy(page_buffer, &superblock, sizeof(superblock));
        if (superblock.card_flags & CF_USE_ECC)
            write_page_spare_part(page_buffer + superblock.page_size, page_buffer);
        fwrite(page_buffer, physical_page_size, 1, output_file);
    }

    fclose(output_file);
}